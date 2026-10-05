#pragma once

#include <cstdint>
#include <exception>
#include <tuple>
#include <utility>

#include "examples/structures/db.hpp"
#include "observable/observable.hpp"
#include "observable/stream_multiplexer.hpp"
#include "observable/stream_receiver.hpp"

namespace waos::observable {

  // Wire -> Observable bridge. On first subscribe sends start via the
  // muxer (function_id + args, since = maxTs). On last unsubscribe sends
  // stream_cancel. The muxer owns the return channel (pid/socket).
  template <typename Starter, typename Channel, typename... Args>
  class RemoteObservable : public Observable<BinaryEntry>, public StreamReceiver {
    StreamMultiplexer<Starter, Channel>& mux_;
    std::uint64_t functionId_ = 0;
    waos::stream_flags flags_ = waos::stream_flags::none;
    std::tuple<Args...> args_;
    std::uint64_t streamId_ = 0;

  public:
    RemoteObservable(
        StreamMultiplexer<Starter, Channel>& mux,
        std::uint64_t function_id,
        Args... args)
      : mux_(mux)
      , functionId_(function_id)
      , args_(std::move(args)...) {}

    RemoteObservable& withFlags(waos::stream_flags flags) {
      flags_ = flags;
      return *this;
    }

    ~RemoteObservable() override {
      stopStream();
    }

    std::uint64_t streamId() const noexcept { return streamId_; }
    std::uint64_t functionId() const noexcept { return functionId_; }

    void onStreamOpened(waos::stream_opened&) override {}

    void onStreamItem(waos::stream_item& m) override {
      onItem(BinaryEntry{std::move(m.key), std::move(m.value), m.timestamp});
    }

    void onStreamDone(waos::stream_done&) override {
      onDone();
    }

    void onStreamReset(waos::stream_reset&) override {
      onReset();
    }

    void onStreamError(waos::stream_error& m) override {
      onError(std::make_exception_ptr(waos::db_io_error(m.message)));
    }

  protected:
    void onStart() override {
      streamId_ = std::apply(
        [&](auto&... a) {
          return mux_.createStream(functionId_, maxTs(), flags_, this, a...);
        },
        args_);
    }

    void onStop() override {
      stopStream();
    }

    void onPause() override {}
    void onResume() override {}

  private:
    void stopStream() {
      if(streamId_ == 0) return;
      mux_.cancelStream(streamId_);
      streamId_ = 0;
    }
  };

}
