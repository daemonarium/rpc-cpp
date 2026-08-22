#pragma once

#include <cstdint>
#include <exception>
#include <memory>

#include "observable/ring_buffer.hpp"
#include "observable/types.hpp"

namespace waos::observable {

  class ConsumerOutput;

  class ObservableBase {
  public:
    virtual ~ObservableBase() = default;

    virtual void removeOutput(ConsumerOutput* out) = 0;
    virtual void onConsumerPaused(ConsumerOutput* out) = 0;
    virtual void onConsumerResumed(ConsumerOutput* out) = 0;

  protected:
    virtual void onStart() {}
    virtual void onStop() {}
    virtual void onPause() {}
    virtual void onResume() {}
  };

  class ConsumerOutput : public std::enable_shared_from_this<ConsumerOutput> {
    ObservableBase* owner_;
    std::weak_ptr<void> consumerRef_;
    DispatchCallbacks dispatch_;
    ring_buffer<Change> buffer_;
    std::uint64_t replayWatermark_ = 0;
    std::shared_ptr<serialization::promise<void>> inFlight_;
    bool paused_ = false;
    bool detached_ = false;

  public:
    ConsumerOutput(
        ObservableBase* owner,
        std::shared_ptr<void> consumerRef,
        DispatchCallbacks dispatch,
        std::uint64_t replayWatermark,
        std::size_t bufferLimit)
      : owner_(owner)
      , consumerRef_(std::move(consumerRef))
      , dispatch_(std::move(dispatch))
      , buffer_(bufferLimit)
      , replayWatermark_(replayWatermark) {}

    std::uint64_t replayWatermark() const noexcept { return replayWatermark_; }
    bool isPaused() const noexcept { return paused_; }
    bool isConsumerDead() const { return consumerRef_.expired(); }
    bool detached() const noexcept { return detached_; }

    void emit(Change c) {
      if(detached_) return;
      if(consumerRef_.expired()) {
        detach();
        return;
      }
      if(inFlight_) {
        if(!buffer_.push(std::move(c))) {
          setPaused(true);
          return;
        }
        if(buffer_.full()) setPaused(true);
        return;
      }
      deliver(std::move(c));
    }

    void replayDirect(Change c) {
      if(detached_ || consumerRef_.expired()) return;
      if(c.ts > replayWatermark_) replayWatermark_ = c.ts;
      if(!dispatch_.onChange) return;
      auto p = dispatch_.onChange(std::move(c));
      if(p && p->state == serialization::promise_state::pending) {
        inFlight_ = std::move(p);
        auto self = shared_from_this();
        inFlight_->on_resolved([self]() { self->drain(); });
      }
    }

    void dispatchReset() {
      buffer_.clear();
      replayWatermark_ = 0;
      setPaused(false);
      if(dispatch_.onReset) dispatch_.onReset();
    }

    void dispatchReady() {
      if(dispatch_.onReady) dispatch_.onReady();
    }

    void dispatchError(std::exception_ptr ex) {
      if(dispatch_.onError) dispatch_.onError(ex);
    }

    void dispatchClosed() {
      if(dispatch_.onClosed) dispatch_.onClosed();
    }

    void unsubscribe() {
      detach();
    }

  private:
    void deliver(Change c) {
      if(c.ts > replayWatermark_) replayWatermark_ = c.ts;
      if(!dispatch_.onChange) return;
      auto p = dispatch_.onChange(std::move(c));
      if(p && p->state == serialization::promise_state::pending) {
        inFlight_ = std::move(p);
        auto self = shared_from_this();
        inFlight_->on_resolved([self]() { self->drain(); });
      }
    }

    void drain() {
      inFlight_.reset();
      while(!buffer_.empty()) {
        auto next = buffer_.pop();
        if(!next) break;
        if(!dispatch_.onChange) continue;
        if(next->ts > replayWatermark_) replayWatermark_ = next->ts;
        auto p = dispatch_.onChange(std::move(*next));
        if(p && p->state == serialization::promise_state::pending) {
          inFlight_ = std::move(p);
          auto self = shared_from_this();
          inFlight_->on_resolved([self]() { self->drain(); });
          if(buffer_.full()) setPaused(true);
          else setPaused(false);
          return;
        }
      }
      setPaused(false);
    }

    void setPaused(bool p) {
      if(paused_ == p) return;
      paused_ = p;
      if(!owner_ || detached_) return;
      if(p) owner_->onConsumerPaused(this);
      else owner_->onConsumerResumed(this);
    }

    void detach() {
      if(detached_) return;
      detached_ = true;
      if(paused_) {
        paused_ = false;
        if(owner_) owner_->onConsumerResumed(this);
      }
      auto* owner = owner_;
      owner_ = nullptr;
      if(owner) owner->removeOutput(this);
    }
  };

}
