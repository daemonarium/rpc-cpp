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
#include "observable/one_shot_scan_consumer.hpp"
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
      std::shared_ptr<StreamRelayConsumer<Channel>> live;
      std::shared_ptr<OneShotScanConsumer<Channel>> oneshot;
      std::shared_ptr<ConsumerOutput> subscription;
      std::shared_ptr<Observable<BinaryEntry>> owned;
      Observable<BinaryEntry>* obs = nullptr;
      std::uint64_t since = 0;
      std::size_t bufferLimit = 1024;
      bool opened = false;
      bool finishPending = false;
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
      attachImpl(stream_id, &obs, nullptr, since, bufferLimit, false);
    }

    void attach(
        std::uint64_t stream_id,
        std::shared_ptr<Observable<BinaryEntry>> obs,
        std::uint64_t since = 0,
        std::size_t bufferLimit = 1024) {
      if(!obs) return;
      Observable<BinaryEntry>* raw = obs.get();
      attachImpl(stream_id, raw, std::move(obs), since, bufferLimit, false);
    }

    void attach_oneshot(
        std::uint64_t stream_id,
        Observable<BinaryEntry>& obs,
        std::uint64_t since = 0,
        std::size_t bufferLimit = 1024) {
      attachImpl(stream_id, &obs, nullptr, since, bufferLimit, true);
    }

    void attach_oneshot(
        std::uint64_t stream_id,
        std::shared_ptr<Observable<BinaryEntry>> obs,
        std::uint64_t since = 0,
        std::size_t bufferLimit = 1024) {
      if(!obs) return;
      Observable<BinaryEntry>* raw = obs.get();
      attachImpl(stream_id, raw, std::move(obs), since, bufferLimit, true);
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
        if(it->second.live) it->second.live->onWritable();
        else if(it->second.oneshot) it->second.oneshot->onWritable();
        it = streams_.find(id);
        if(it == streams_.end()) continue;
        if(oneshotFinished(it->second)) {
          detach(id);
        }
      }
    }

    std::size_t size() const noexcept { return streams_.size(); }
    bool isAttached(std::uint64_t stream_id) const {
      return streams_.contains(stream_id);
    }

  private:
    static bool oneshotFinished(const Entry& e) {
      return e.oneshot && (e.finishPending || e.oneshot->finished());
    }

    void attachImpl(
        std::uint64_t stream_id,
        Observable<BinaryEntry>* obs,
        std::shared_ptr<Observable<BinaryEntry>> owned,
        std::uint64_t since,
        std::size_t bufferLimit,
        bool oneshot) {
      if(pendingCancels_.erase(stream_id)) return;
      detach(stream_id);
      Entry e;
      e.obs = obs;
      e.owned = std::move(owned);
      e.since = since;
      e.bufferLimit = bufferLimit;
      if(oneshot) {
        e.oneshot = std::make_shared<OneShotScanConsumer<Channel>>(
          channel_, stream_id, since);
        e.oneshot->setOnFinished([this, stream_id]() {
          auto it = streams_.find(stream_id);
          if(it == streams_.end()) return;
          if(!it->second.subscription) {
            it->second.finishPending = true;
            return;
          }
          detach(stream_id);
        });
      } else {
        e.live = std::make_shared<StreamRelayConsumer<Channel>>(
          channel_, stream_id, since);
      }
      streams_[stream_id] = std::move(e);
      if(channel_.template try_queue_message<waos::stream_opened>(
            waos::stream_opened{stream_id})) {
        completeOpen(stream_id);
      }
    }

    void completeOpen(std::uint64_t stream_id) {
      auto it = streams_.find(stream_id);
      if(it == streams_.end() || it->second.opened) return;
      it->second.opened = true;
      Observable<BinaryEntry>* obs = it->second.obs;
      std::size_t bufferLimit = it->second.bufferLimit;
      std::shared_ptr<ConsumerOutput> sub;
      if(it->second.live) {
        sub = obs->subscribe(it->second.live, bufferLimit);
      } else {
        sub = obs->subscribe(it->second.oneshot, bufferLimit);
      }
      it = streams_.find(stream_id);
      if(it == streams_.end()) {
        if(sub) sub->unsubscribe();
        return;
      }
      it->second.subscription = std::move(sub);
      if(oneshotFinished(it->second)) {
        detach(stream_id);
      }
    }
  };

}
