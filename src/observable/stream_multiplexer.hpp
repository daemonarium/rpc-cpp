#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "examples/structures/stream_protocol.hpp"
#include "observable/stream_receiver.hpp"

namespace waos::observable {

  // Demuxes stream_* messages by stream_id on a Channel. Start RPCs go
  // through a Starter (any type with call<void>(function_id, args...)).
  // Cancel is a stream_cancel message on the Channel.
  //
  // Platform-agnostic: no kernel-shield dependency. An rpc_client can
  // fill both roles (one-connection). Distinct instances = split start
  // vs stream sockets.
  //
  // Does not subscribe to socket_opened (socket-bind ack, not per-stream).
  //
  // The caller registers stream_* message types on the Channel (type
  // indexes must match the peer). This class only installs handlers.
  template <typename Starter, typename Channel = Starter>
  class StreamMultiplexer {
    Starter& starter_;
    Channel& channel_;
    std::uint64_t returnPid_ = 0;
    std::uint32_t returnSocket_ = 0;
    std::unordered_map<std::uint64_t, StreamReceiver*> receivers_;
    std::unordered_set<std::uint64_t> opened_;
    std::unordered_set<std::uint64_t> cancelBeforeOpen_;
    std::uint64_t nextId_ = 1;

  public:
    StreamMultiplexer(
        Starter& starter,
        Channel& channel,
        std::uint64_t returnPid,
        std::uint32_t returnSocket)
      : starter_(starter)
      , channel_(channel)
      , returnPid_(returnPid)
      , returnSocket_(returnSocket) {
      install();
    }

    StreamMultiplexer(
        Starter& client,
        std::uint64_t returnPid,
        std::uint32_t returnSocket)
      requires std::is_same_v<Starter, Channel>
      : StreamMultiplexer(client, client, returnPid, returnSocket) {}

    template <typename... Args>
    std::uint64_t createStream(
        std::uint64_t function_id,
        std::uint64_t since_ts,
        StreamReceiver* r,
        Args&&... args) {
      return createStream(
        function_id, since_ts, waos::stream_flags::none, r, std::forward<Args>(args)...);
    }

    template <typename... Args>
    std::uint64_t createStream(
        std::uint64_t function_id,
        std::uint64_t since_ts,
        waos::stream_flags flags,
        StreamReceiver* r,
        Args&&... args) {
      std::uint64_t id = nextId_++;
      waos::stream_handle h;
      h.pid = returnPid_;
      h.socket = returnSocket_;
      h.stream_id = id;
      h.since_timestamp = since_ts;
      h.flags = static_cast<std::uint8_t>(flags);
      if(r) receivers_[id] = r;
      starter_.template call<void>(function_id, h, std::forward<Args>(args)...);
      return id;
    }

    void cancelStream(std::uint64_t id) {
      receivers_.erase(id);
      channel_.template send_message<waos::stream_cancel>(waos::stream_cancel{id});
      if(!opened_.erase(id)) {
        cancelBeforeOpen_.insert(id);
      }
    }

    // Attach a receiver for an id allocated outside createStream (gateway ref_id).
    void attachReceiver(std::uint64_t id, StreamReceiver* receiver) {
      if(receiver) {
        receivers_[id] = receiver;
      } else {
        receivers_.erase(id);
      }
    }

    std::size_t size() const noexcept { return receivers_.size(); }
    std::uint64_t returnPid() const noexcept { return returnPid_; }
    std::uint32_t returnSocket() const noexcept { return returnSocket_; }

  private:
    void install() {
      channel_.template on_message<waos::stream_opened>(
        std::function<void(waos::stream_opened&)>(
          [this](waos::stream_opened& m) { onOpened(m); }));
      channel_.template on_message<waos::stream_item>(
        std::function<void(waos::stream_item&)>(
          [this](waos::stream_item& m) {
            dispatch(m.stream_id, [&](StreamReceiver* r) { r->onStreamItem(m); });
          }));
      channel_.template on_message<waos::stream_key>(
        std::function<void(waos::stream_key&)>(
          [this](waos::stream_key& m) {
            dispatch(m.stream_id, [&](StreamReceiver* r) { r->onStreamKey(m); });
          }));
      channel_.template on_message<waos::stream_done>(
        std::function<void(waos::stream_done&)>(
          [this](waos::stream_done& m) {
            dispatch(m.stream_id, [&](StreamReceiver* r) { r->onStreamDone(m); });
          }));
      channel_.template on_message<waos::stream_reset>(
        std::function<void(waos::stream_reset&)>(
          [this](waos::stream_reset& m) {
            dispatch(m.stream_id, [&](StreamReceiver* r) { r->onStreamReset(m); });
          }));
      channel_.template on_message<waos::stream_error>(
        std::function<void(waos::stream_error&)>(
          [this](waos::stream_error& m) {
            dispatch(m.stream_id, [&](StreamReceiver* r) { r->onStreamError(m); });
          }));
    }

    void onOpened(waos::stream_opened& m) {
      if(cancelBeforeOpen_.erase(m.stream_id)) {
        channel_.template send_message<waos::stream_cancel>(
          waos::stream_cancel{m.stream_id});
        return;
      }
      opened_.insert(m.stream_id);
      dispatch(m.stream_id, [&](StreamReceiver* r) { r->onStreamOpened(m); });
    }

    template <typename F>
    void dispatch(std::uint64_t id, F&& f) {
      auto it = receivers_.find(id);
      if(it == receivers_.end() || !it->second) return;
      f(it->second);
    }
  };

  template <typename Starter, typename Channel>
  StreamMultiplexer(Starter&, Channel&, std::uint64_t, std::uint32_t)
    -> StreamMultiplexer<Starter, Channel>;

  template <typename Client>
  StreamMultiplexer(Client&, std::uint64_t, std::uint32_t)
    -> StreamMultiplexer<Client, Client>;

}
