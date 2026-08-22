#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "examples/structures/stream_protocol.hpp"
#include "observable/observable.hpp"
#include "observable/stream_relay_consumer.hpp"

namespace waos::observable {

  // One Channel / socket. Attaches Observable<BinaryEntry> instances as
  // per-stream relays. Not used by start_scan.
  //
  // Channel must provide on_message, try_queue_message, and (for cancel
  // echo from the peer) the stream_cancel handler installed here.
  template <typename Channel>
  class StreamPublisher {
    struct Entry {
      std::shared_ptr<StreamRelayConsumer<Channel>> consumer;
      std::shared_ptr<ConsumerOutput> subscription;
      Observable<BinaryEntry>* obs = nullptr;
      std::uint64_t since = 0;
      std::size_t bufferLimit = 1024;
      bool opened = false;
    };

    Channel& channel_;
    std::unordered_map<std::uint64_t, Entry> streams_;
    std::unordered_set<std::uint64_t> pendingCancels_;

  public:
    explicit StreamPublisher(Channel& channel)
      : channel_(channel) {
      channel_.template on_message<waos::stream_cancel>(
        std::function<void(waos::stream_cancel&)>(
          [this](waos::stream_cancel& m) { onCancel(m); }));
    }

    StreamPublisher(const StreamPublisher&) = delete;
    StreamPublisher& operator=(const StreamPublisher&) = delete;

    ~StreamPublisher() {
      while(!streams_.empty()) {
        detach(streams_.begin()->first);
      }
    }

    void attach(
        std::uint64_t stream_id,
        Observable<BinaryEntry>& obs,
        std::uint64_t since = 0,
        std::size_t bufferLimit = 1024) {
      if(pendingCancels_.erase(stream_id)) return;
      detach(stream_id);
      Entry e;
      e.consumer = std::make_shared<StreamRelayConsumer<Channel>>(
        channel_, stream_id, since);
      e.obs = &obs;
      e.since = since;
      e.bufferLimit = bufferLimit;
      streams_[stream_id] = std::move(e);
      if(channel_.template try_queue_message<waos::stream_opened>(
            waos::stream_opened{stream_id})) {
        completeOpen(stream_id);
      }
    }

    void detach(std::uint64_t stream_id) {
      auto it = streams_.find(stream_id);
      if(it == streams_.end()) return;
      if(it->second.subscription) {
        it->second.subscription->unsubscribe();
        it->second.subscription.reset();
      }
      streams_.erase(it);
    }

    void onCancel(waos::stream_cancel& m) {
      if(streams_.contains(m.stream_id)) {
        detach(m.stream_id);
      } else {
        pendingCancels_.insert(m.stream_id);
      }
    }

    void onWritable() {
      std::vector<std::uint64_t> ids;
      ids.reserve(streams_.size());
      for(auto const& [id, _] : streams_) ids.push_back(id);
      for(std::uint64_t id : ids) {
        auto it = streams_.find(id);
        if(it == streams_.end()) continue;
        if(!it->second.opened) {
          if(!channel_.template try_queue_message<waos::stream_opened>(
                waos::stream_opened{id})) {
            continue;
          }
          completeOpen(id);
          it = streams_.find(id);
          if(it == streams_.end()) continue;
        }
        it->second.consumer->onWritable();
      }
    }

    std::size_t size() const noexcept { return streams_.size(); }
    bool isAttached(std::uint64_t stream_id) const {
      return streams_.contains(stream_id);
    }

  private:
    void completeOpen(std::uint64_t stream_id) {
      auto it = streams_.find(stream_id);
      if(it == streams_.end() || it->second.opened) return;
      it->second.opened = true;
      it->second.subscription = it->second.obs->subscribe(
        it->second.consumer, it->second.bufferLimit);
    }
  };

}
