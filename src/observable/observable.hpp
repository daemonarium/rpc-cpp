#pragma once

#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "observable/consumer_output.hpp"
#include "observable/types.hpp"
#include "serialization/promise.hpp"

namespace waos::observable {

  template <typename EntryT = BinaryEntry>
  class Observable : public ObservableBase {
    std::map<binary, EntryT> data_;
    std::map<binary, EntryT> oldData_;
    std::uint64_t maxTs_ = 0;
    std::vector<std::shared_ptr<ConsumerOutput>> outputs_;
    std::size_t activeCount_ = 0;
    std::size_t pausedCount_ = 0;
    ObservableState state_ = ObservableState::idle;
    std::shared_ptr<serialization::promise<void>> readyPromise_
      = std::make_shared<serialization::promise<void>>();
    std::exception_ptr lastError_;
    std::vector<std::function<void()>> idleListeners_;

  public:
    Observable() = default;
    ~Observable() override = default;

    void onItem(EntryT e) {
      if(state_ == ObservableState::closed) return;
      sweepDeadOutputs();
      if(state_ == ObservableState::error) {
        state_ = ObservableState::loading;
      }
      if(e.ts > maxTs_) maxTs_ = e.ts;

      auto it = data_.find(e.key);
      if(it != data_.end() && it->second.ts >= e.ts) {
        return;
      }

      Change ch{e.key, e.value, e.ts};
      if(e.value.empty()) {
        if(it != data_.end()) data_.erase(it);
      } else {
        data_[e.key] = std::move(e);
      }
      fanOut(ch);
    }

    void onDone() {
      if(state_ == ObservableState::closed) return;
      sweepDeadOutputs();
      if(state_ != ObservableState::loading && state_ != ObservableState::reloading) {
        return;
      }
      oldData_.clear();
      state_ = ObservableState::ready;
      readyPromise_->resolve();
      auto snapshot = outputs_;
      for(auto& out : snapshot) {
        if(out && !out->detached()) out->dispatchReady();
      }
    }

    void onReset() {
      if(state_ == ObservableState::closed) return;
      sweepDeadOutputs();
      if(state_ != ObservableState::ready
          && state_ != ObservableState::error
          && state_ != ObservableState::loading) {
        return;
      }
      oldData_ = std::move(data_);
      data_.clear();
      state_ = ObservableState::reloading;
      readyPromise_ = std::make_shared<serialization::promise<void>>();
      auto snapshot = outputs_;
      for(auto& out : snapshot) {
        if(out && !out->detached()) out->dispatchReset();
      }
    }

    void onError(std::exception_ptr ex) {
      if(state_ == ObservableState::closed) return;
      sweepDeadOutputs();
      state_ = ObservableState::error;
      lastError_ = ex;
      auto snapshot = outputs_;
      for(auto& out : snapshot) {
        if(out && !out->detached()) out->dispatchError(ex);
      }
    }

    std::shared_ptr<ConsumerOutput> subscribe(
        std::shared_ptr<void> consumerRef,
        DispatchCallbacks callbacks,
        std::uint64_t sinceTimestamp = 0,
        std::size_t bufferLimit = 1024) {
      if(state_ == ObservableState::closed) return nullptr;
      sweepDeadOutputs();
      auto out = std::make_shared<ConsumerOutput>(
        this, std::move(consumerRef), std::move(callbacks), sinceTimestamp, bufferLimit);
      outputs_.push_back(out);
      ++activeCount_;
      if(state_ == ObservableState::idle) {
        state_ = ObservableState::loading;
        onStart();
      }
      replayTo(*out);
      if(state_ == ObservableState::ready) out->dispatchReady();
      else if(state_ == ObservableState::error) out->dispatchError(lastError_);
      return out;
    }

    template <typename C>
      requires Consumer<C>
    std::shared_ptr<ConsumerOutput> subscribe(
        std::shared_ptr<C> consumer,
        std::size_t bufferLimit = 1024) {
      std::weak_ptr<C> weak = consumer;
      DispatchCallbacks cb;
      cb.onChange = [weak](Change ch) {
        auto c = weak.lock();
        if(!c) return serialization::promise<void>::resolved();
        return c->onChange(std::move(ch));
      };
      cb.onReady = [weak]() {
        if(auto c = weak.lock()) c->onReady();
      };
      cb.onReset = [weak]() {
        if(auto c = weak.lock()) c->onReset();
      };
      cb.onError = [weak](std::exception_ptr ex) {
        if(auto c = weak.lock()) c->onError(ex);
      };
      cb.onClosed = [weak]() {
        if(auto c = weak.lock()) c->onClosed();
      };
      auto since = consumer->sinceTimestamp();
      return subscribe(
        std::static_pointer_cast<void>(consumer), std::move(cb), since, bufferLimit);
    }

    void unsubscribe(ConsumerOutput* out) {
      removeOutput(out);
    }

    std::shared_ptr<serialization::promise<void>> readyPromise() {
      return readyPromise_;
    }

    std::vector<EntryT> getEntries() const {
      const auto& src = view();
      std::vector<EntryT> out;
      out.reserve(src.size());
      for(auto const& [k, e] : src) out.push_back(e);
      return out;
    }

    std::optional<EntryT> get(binary const& key) const {
      auto const& src = view();
      auto it = src.find(key);
      if(it == src.end()) return std::nullopt;
      return it->second;
    }

    std::uint64_t maxTs() const noexcept { return maxTs_; }
    void bumpMaxTs(std::uint64_t ts) {
      if(ts > maxTs_) maxTs_ = ts;
    }
    ObservableState state() const noexcept { return state_; }
    std::exception_ptr lastError() const { return lastError_; }
    std::size_t activeCount() const noexcept { return activeCount_; }
    std::size_t pausedCount() const noexcept { return pausedCount_; }

    void addIdleListener(std::function<void()> cb) {
      idleListeners_.push_back(std::move(cb));
    }

    void kill() {
      if(state_ == ObservableState::closed) return;
      auto snapshot = outputs_;
      for(auto& out : snapshot) {
        if(out && !out->detached()) out->dispatchClosed();
      }
      outputs_.clear();
      pausedCount_ = 0;
      if(activeCount_ > 0) {
        activeCount_ = 0;
        onStop();
      }
      state_ = ObservableState::closed;
    }

    void removeOutput(ConsumerOutput* out) override {
      if(!out) return;
      auto it = std::find_if(outputs_.begin(), outputs_.end(),
        [out](auto const& p) { return p.get() == out; });
      if(it == outputs_.end()) return;
      bool wasPaused = (*it)->isPaused();
      outputs_.erase(it);
      if(activeCount_ > 0) --activeCount_;
      if(wasPaused && pausedCount_ > 0) --pausedCount_;
      if(activeCount_ == 0 && state_ != ObservableState::closed) {
        pausedCount_ = 0;
        state_ = ObservableState::idle;
        onStop();
        notifyIdle();
      }
    }

    void notifyIdle() {
      for(auto const& cb : idleListeners_) {
        if(cb) cb();
      }
    }

    void onConsumerPaused(ConsumerOutput*) override {
      std::size_t before = pausedCount_;
      ++pausedCount_;
      if(activeCount_ > 0 && before < activeCount_ && pausedCount_ >= activeCount_) {
        onPause();
      }
    }

    void onConsumerResumed(ConsumerOutput*) override {
      bool wasAll = activeCount_ > 0 && pausedCount_ >= activeCount_;
      if(pausedCount_ > 0) --pausedCount_;
      if(wasAll && pausedCount_ < activeCount_) {
        onResume();
      }
    }

  protected:
    using ObservableBase::onStart;
    using ObservableBase::onStop;
    using ObservableBase::onPause;
    using ObservableBase::onResume;

  private:
    std::map<binary, EntryT> const& view() const {
      if(state_ == ObservableState::reloading) return oldData_;
      return data_;
    }

    void fanOut(Change const& c) {
      auto snapshot = outputs_;
      for(auto& out : snapshot) {
        if(out && !out->detached()) out->emit(c);
      }
    }

    void replayTo(ConsumerOutput& out) {
      for(auto const& [k, e] : data_) {
        if(e.ts > out.replayWatermark()) {
          out.replayDirect(Change{e.key, e.value, e.ts});
        }
      }
    }

    void sweepDeadOutputs() {
      auto snapshot = outputs_;
      for(auto& out : snapshot) {
        if(out && !out->detached() && out->isConsumerDead()) {
          out->unsubscribe();
        }
      }
    }
  };

}
