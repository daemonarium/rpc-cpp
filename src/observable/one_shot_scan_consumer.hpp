#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "examples/structures/stream_protocol.hpp"
#include "observable/types.hpp"
#include "serialization/promise.hpp"

namespace waos::observable {

  // Finite-scan sibling of StreamRelayConsumer. Subscribe already replays
  // entries via onChange; onReady therefore only queues stream_done (it
  // must not drain getEntries again). After stream_done or stream_error
  // is successfully queued, finished() is true so StreamPublisher can
  // unsubscribe. Channel must provide try_queue_message<T>(const T&) -> bool.
  template <typename Channel>
  class OneShotScanConsumer {
    Channel& channel_;
    std::uint64_t streamId_ = 0;
    std::uint64_t since_ = 0;

    enum class Control : std::uint8_t { None, Done, Reset, Error };
    std::optional<waos::stream_item> blockedItem_;
    std::shared_ptr<serialization::promise<void>> blockedPromise_;
    Control pendingControl_ = Control::None;
    std::string pendingError_;
    bool finished_ = false;
    std::function<void()> onFinished_;

  public:
    OneShotScanConsumer(Channel& channel, std::uint64_t stream_id, std::uint64_t since)
      : channel_(channel)
      , streamId_(stream_id)
      , since_(since) {}

    std::uint64_t streamId() const noexcept { return streamId_; }
    std::uint64_t sinceTimestamp() const noexcept { return since_; }
    bool finished() const noexcept { return finished_; }

    void setOnFinished(std::function<void()> cb) {
      onFinished_ = std::move(cb);
    }

    std::shared_ptr<serialization::promise<void>> onChange(Change ch) {
      if(pendingControl_ == Control::Reset) {
        if(!flushControl()) {
          return blockItem(toItem(std::move(ch)));
        }
      }
      waos::stream_item item = toItem(std::move(ch));
      if(channel_.template try_queue_message<waos::stream_item>(item)) {
        return serialization::promise<void>::resolved();
      }
      return blockItem(std::move(item));
    }

    void onReady() {
      if(blockedItem_) {
        pendingControl_ = Control::Done;
        return;
      }
      queueControl(Control::Done);
    }

    void onReset() {
      if(blockedItem_) {
        blockedItem_.reset();
        resolveBlocked();
      }
      queueControl(Control::Reset);
    }

    void onError(std::exception_ptr ex) {
      pendingError_ = exceptionMessage(ex);
      if(blockedItem_) {
        pendingControl_ = Control::Error;
        return;
      }
      queueControl(Control::Error);
    }

    void onClosed() {
      pendingError_ = "closed";
      if(blockedItem_) {
        pendingControl_ = Control::Error;
        return;
      }
      queueControl(Control::Error);
    }

    void onWritable() {
      if(pendingControl_ == Control::Reset) {
        if(!flushControl()) return;
      }
      if(blockedItem_) {
        if(!channel_.template try_queue_message<waos::stream_item>(*blockedItem_)) {
          return;
        }
        blockedItem_.reset();
        resolveBlocked();
        if(blockedItem_) return;
      }
      flushControl();
    }

  private:
    waos::stream_item toItem(Change ch) const {
      waos::stream_item m;
      m.stream_id = streamId_;
      m.key = std::move(ch.key);
      m.value = std::move(ch.value);
      m.timestamp = ch.ts;
      return m;
    }

    std::shared_ptr<serialization::promise<void>> blockItem(waos::stream_item item) {
      blockedItem_ = std::move(item);
      blockedPromise_ = std::make_shared<serialization::promise<void>>();
      return blockedPromise_;
    }

    void resolveBlocked() {
      auto p = std::move(blockedPromise_);
      blockedPromise_.reset();
      if(p) p->resolve();
    }

    void queueControl(Control c) {
      pendingControl_ = c;
      flushControl();
    }

    bool flushControl() {
      if(pendingControl_ == Control::None) return true;
      bool ok = false;
      Control flushed = pendingControl_;
      if(pendingControl_ == Control::Done) {
        ok = channel_.template try_queue_message<waos::stream_done>(
          waos::stream_done{streamId_});
      } else if(pendingControl_ == Control::Reset) {
        ok = channel_.template try_queue_message<waos::stream_reset>(
          waos::stream_reset{streamId_});
      } else if(pendingControl_ == Control::Error) {
        ok = channel_.template try_queue_message<waos::stream_error>(
          waos::stream_error{streamId_, pendingError_});
      }
      if(ok) {
        pendingControl_ = Control::None;
        pendingError_.clear();
        if(flushed == Control::Done || flushed == Control::Error) {
          markFinished();
        }
      }
      return ok;
    }

    void markFinished() {
      if(finished_) return;
      finished_ = true;
      if(onFinished_) onFinished_();
    }

    static std::string exceptionMessage(std::exception_ptr ex) {
      if(!ex) return "error";
      try {
        std::rethrow_exception(ex);
      } catch(const std::exception& e) {
        return e.what();
      } catch(...) {
        return "error";
      }
    }
  };

}
