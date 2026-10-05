#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "examples/structures/stream_protocol.hpp"
#include "observable/observable.hpp"
#include "observable/stream_multiplexer.hpp"
#include "observable/stream_receiver.hpp"
#include "observable/types.hpp"

namespace waos::observable {

  class Disposable {
  public:
    virtual ~Disposable() = default;
    virtual void dispose() = 0;
  };

  class ReferenceOps {
  public:
    virtual ~ReferenceOps() = default;
    virtual void setReference() = 0;
    virtual void observe(StreamReceiver* receiver, std::uint64_t since) = 0;
    virtual void fetch(StreamReceiver* receiver, waos::stream_flags flags, std::uint64_t since) = 0;
    virtual void unobserve() = 0;
    virtual void dispose() = 0;
    virtual bool persistent() const { return false; }
  };

  template <typename Starter, typename Channel = Starter>
  class DirectReferenceOps : public ReferenceOps {
    StreamMultiplexer<Starter, Channel>& mux_;
    std::uint64_t functionId_ = 0;
    std::function<std::uint64_t(std::uint64_t, StreamReceiver*, waos::stream_flags)> start_;
    std::uint64_t streamId_ = 0;

  public:
    DirectReferenceOps(
        StreamMultiplexer<Starter, Channel>& mux,
        std::uint64_t function_id,
        std::function<std::uint64_t(std::uint64_t, StreamReceiver*, waos::stream_flags)> start)
      : mux_(mux)
      , functionId_(function_id)
      , start_(std::move(start)) {}

    bool persistent() const override { return true; }
    void setReference() override {}

    void observe(StreamReceiver* receiver, std::uint64_t since) override {
      if(start_) streamId_ = start_(since, receiver, waos::stream_flags::observe);
    }

    void fetch(StreamReceiver* receiver, waos::stream_flags flags, std::uint64_t since) override {
      if(start_) streamId_ = start_(since, receiver, flags);
    }

    void unobserve() override {
      if(streamId_ == 0) return;
      mux_.cancelStream(streamId_);
      streamId_ = 0;
    }

    void dispose() override {}
  };

  template <typename EntryT = BinaryEntry>
  class RemoteReference {
    std::shared_ptr<ReferenceOps> ops_;
    std::function<std::shared_ptr<Observable<EntryT>>()> liveFactory_;
    std::function<std::shared_ptr<Observable<EntryT>>(std::uint64_t)> scanFactory_;
    std::shared_ptr<Observable<EntryT>> live_;
    std::vector<std::shared_ptr<Observable<EntryT>>> scans_;
    std::size_t holdCount_ = 0;
    bool setSent_ = false;
    bool dropping_ = false;

    class Hold : public Disposable {
      RemoteReference* owner_ = nullptr;
      bool done_ = false;
    public:
      explicit Hold(RemoteReference* owner) : owner_(owner) {}
      ~Hold() override { dispose(); }
      void dispose() override {
        if(done_) return;
        done_ = true;
        if(owner_) {
          owner_->releaseHold();
          owner_ = nullptr;
        }
      }
    };

  public:
    RemoteReference(
        std::shared_ptr<ReferenceOps> ops,
        std::function<std::shared_ptr<Observable<EntryT>>()> liveFactory,
        std::function<std::shared_ptr<Observable<EntryT>>(std::uint64_t)> scanFactory)
      : ops_(std::move(ops))
      , liveFactory_(std::move(liveFactory))
      , scanFactory_(std::move(scanFactory)) {}

    Observable<EntryT>& observable() {
      ensureSet();
      if(!live_) live_ = adopt(liveFactory_());
      return *live_;
    }

    std::shared_ptr<Observable<EntryT>> stream(std::uint64_t since = 0) {
      ensureSet();
      if(live_ && live_->activeCount() > 0) return live_;
      auto scan = adopt(scanFactory_(since));
      scans_.push_back(scan);
      return scan;
    }

    std::shared_ptr<Observable<EntryT>> fetch(std::uint64_t since = 0) {
      ensureSet();
      auto scan = adopt(scanFactory_(since));
      scans_.push_back(scan);
      return scan;
    }

    std::unique_ptr<Disposable> hold() {
      ++holdCount_;
      return std::make_unique<Hold>(this);
    }

    std::shared_ptr<Observable<EntryT>> peekLive() const { return live_; }

    void bind() { ensureSet(); }

  private:
    void ensureSet() {
      if(setSent_) return;
      setSent_ = true;
      if(ops_) ops_->setReference();
    }

    std::shared_ptr<Observable<EntryT>> adopt(std::shared_ptr<Observable<EntryT>> obs) {
      obs->addIdleListener([this, raw = obs.get()]() {
        scans_.erase(
          std::remove_if(
            scans_.begin(), scans_.end(),
            [raw](auto const& p) { return p.get() == raw; }),
          scans_.end());
        maybeDrop();
      });
      return obs;
    }

    void releaseHold() {
      if(holdCount_ > 0) --holdCount_;
      maybeDrop();
    }

    void maybeDrop() {
      if(dropping_) return;
      if(ops_ && ops_->persistent()) return;
      if(holdCount_ > 0) return;
      if(live_ && live_->activeCount() > 0) return;
      for(auto const& s : scans_) {
        if(s && s->activeCount() > 0) return;
      }
      if(!setSent_) return;
      dropping_ = true;
      setSent_ = false;
      auto live = live_;
      live_.reset();
      scans_.clear();
      if(ops_) ops_->dispose();
      if(live && live->state() != ObservableState::closed) {
        live->kill();
      }
      dropping_ = false;
    }
  };

}
