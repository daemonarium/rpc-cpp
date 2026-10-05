#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "examples/structures/stream_protocol.hpp"
#include "serialization/annotations.hpp"
#include "serialization/promise.hpp"

namespace waos {

  struct [[=serialization::message()]] mux_socket_open {
    std::uint64_t socket_id = 0;
  };

  struct [[=serialization::message()]] mux_socket_data {
    std::uint64_t socket_id = 0;
    binary payload;
  };

  struct [[=serialization::message()]] mux_socket_close {
    std::uint64_t socket_id = 0;
  };

  struct [[=serialization::message()]] mux_socket_window {
    std::uint64_t socket_id = 0;
    std::uint64_t add_bytes = 0;
  };

}

namespace waos::observable {

  constexpr std::uint64_t mux_socket_rpc_reserved = 0;
  constexpr std::size_t default_mux_window = 16384;
  constexpr std::size_t default_mux_quantum = 4096;

  template <typename Channel>
  void register_mux_socket_messages(Channel& channel) {
    channel.template register_message_type<waos::mux_socket_open>();
    channel.template register_message_type<waos::mux_socket_data>();
    channel.template register_message_type<waos::mux_socket_close>();
    channel.template register_message_type<waos::mux_socket_window>();
  }

  // Virtual-socket multiplexer on a Channel. mux_socket_data is
  // credit-windowed per socket_id (bytes). Control frames always pass.
  // DRR + aging shares the underlying channel across sockets.
  //
  // Channel must provide try_queue_message<T>, send_message<T>, on_message<T>.
  template <typename Channel>
  class SocketMux {
  public:
    struct Options {
      std::size_t quantum = default_mux_quantum;
      std::size_t default_window = default_mux_window;
      std::function<void(std::uint64_t, const binary&)> on_data;
      std::function<void(std::uint64_t)> on_open;
      std::function<void(std::uint64_t)> on_close;
    };

    explicit SocketMux(Channel& channel, Options options = {})
      : channel_(channel)
      , quantum_(options.quantum ? options.quantum : default_mux_quantum)
      , default_window_(options.default_window ? options.default_window : default_mux_window)
      , on_data_(std::move(options.on_data))
      , on_open_(std::move(options.on_open))
      , on_close_(std::move(options.on_close)) {
      channel_.template on_message<waos::mux_socket_open>(
        std::function<void(waos::mux_socket_open&)>([this](waos::mux_socket_open& msg) {
          on_peer_open(msg.socket_id);
        })
      );
      channel_.template on_message<waos::mux_socket_data>(
        std::function<void(waos::mux_socket_data&)>([this](waos::mux_socket_data& msg) {
          on_peer_data(msg.socket_id, msg.payload);
        })
      );
      channel_.template on_message<waos::mux_socket_close>(
        std::function<void(waos::mux_socket_close&)>([this](waos::mux_socket_close& msg) {
          on_peer_close(msg.socket_id);
        })
      );
      channel_.template on_message<waos::mux_socket_window>(
        std::function<void(waos::mux_socket_window&)>([this](waos::mux_socket_window& msg) {
          on_peer_window(msg.socket_id, msg.add_bytes);
        })
      );
    }

    std::size_t default_window_bytes() const { return default_window_; }

    void open_socket(std::uint64_t socket_id) {
      if(socket_id == mux_socket_rpc_reserved) return;
      Slot& slot = ensure(socket_id);
      if(slot.opened_local) return;
      slot.opened_local = true;
      channel_.template send_message<waos::mux_socket_open>(waos::mux_socket_open{socket_id});
    }

    void close_socket(std::uint64_t socket_id) {
      auto it = slots_.find(socket_id);
      if(it == slots_.end()) return;
      finish_slot(it->second);
      slots_.erase(it);
      channel_.template send_message<waos::mux_socket_close>(waos::mux_socket_close{socket_id});
    }

    std::shared_ptr<serialization::promise<void>> send_data(
      std::uint64_t socket_id,
      binary payload
    ) {
      auto it = slots_.find(socket_id);
      if(it == slots_.end()) {
        return serialization::promise<void>::resolved();
      }
      auto p = std::make_shared<serialization::promise<void>>();
      it->second.outbound.push_back(Outbound{std::move(payload), p});
      pump();
      return p;
    }

    void grant_window(std::uint64_t socket_id, std::uint64_t add_bytes) {
      auto it = slots_.find(socket_id);
      if(it == slots_.end() || add_bytes == 0) return;
      if(it->second.grant_paused) {
        it->second.unacked = sat_add(it->second.unacked, add_bytes);
        return;
      }
      channel_.template send_message<waos::mux_socket_window>(
        waos::mux_socket_window{socket_id, add_bytes});
    }

    void set_paused(std::uint64_t socket_id, bool paused) {
      auto it = slots_.find(socket_id);
      if(it == slots_.end()) return;
      it->second.grant_paused = paused;
      if(!paused && it->second.unacked > 0) {
        std::uint64_t n = it->second.unacked;
        it->second.unacked = 0;
        grant_window(socket_id, n);
      }
    }

    std::uint64_t remaining(std::uint64_t socket_id) const {
      auto it = slots_.find(socket_id);
      return it == slots_.end() ? 0 : it->second.remaining;
    }

    std::uint64_t sent(std::uint64_t socket_id) const {
      auto it = slots_.find(socket_id);
      return it == slots_.end() ? 0 : it->second.sent;
    }

    bool has_socket(std::uint64_t socket_id) const {
      return slots_.find(socket_id) != slots_.end();
    }

    std::size_t outbound_count(std::uint64_t socket_id) const {
      auto it = slots_.find(socket_id);
      return it == slots_.end() ? 0 : it->second.outbound.size();
    }

    void on_writable() { pump(); }

  private:
    struct Outbound {
      binary payload;
      std::shared_ptr<serialization::promise<void>> promise;
    };

    struct Slot {
      std::uint64_t id = 0;
      std::uint64_t remaining = 0;
      std::uint64_t sent = 0;
      std::deque<Outbound> outbound;
      std::size_t deficit = 0;
      std::size_t starved = 0;
      bool opened_local = false;
      bool grant_paused = false;
      std::uint64_t unacked = 0;
    };

    Channel& channel_;
    std::size_t quantum_ = default_mux_quantum;
    std::size_t default_window_ = default_mux_window;
    std::function<void(std::uint64_t, const binary&)> on_data_;
    std::function<void(std::uint64_t)> on_open_;
    std::function<void(std::uint64_t)> on_close_;
    std::map<std::uint64_t, Slot> slots_;
    bool pumping_ = false;

    static std::uint64_t sat_add(std::uint64_t a, std::uint64_t b) {
      if(b > std::numeric_limits<std::uint64_t>::max() - a) {
        return std::numeric_limits<std::uint64_t>::max();
      }
      return a + b;
    }

    Slot& ensure(std::uint64_t id) {
      auto it = slots_.find(id);
      if(it != slots_.end()) return it->second;
      Slot slot;
      slot.id = id;
      auto inserted = slots_.emplace(id, std::move(slot));
      return inserted.first->second;
    }

    void on_peer_open(std::uint64_t id) {
      if(id == mux_socket_rpc_reserved) return;
      ensure(id);
      if(on_open_) on_open_(id);
    }

    void on_peer_data(std::uint64_t id, const binary& payload) {
      if(slots_.find(id) == slots_.end()) return;
      if(on_data_) on_data_(id, payload);
    }

    void on_peer_close(std::uint64_t id) {
      auto it = slots_.find(id);
      if(it == slots_.end()) return;
      finish_slot(it->second);
      slots_.erase(it);
      if(on_close_) on_close_(id);
    }

    void on_peer_window(std::uint64_t id, std::uint64_t add_bytes) {
      auto it = slots_.find(id);
      if(it == slots_.end()) return;
      it->second.remaining = sat_add(it->second.remaining, add_bytes);
      pump();
    }

    void finish_slot(Slot& slot) {
      auto queued = std::move(slot.outbound);
      slot.outbound.clear();
      slot.remaining = 0;
      slot.sent = 0;
      slot.unacked = 0;
      for(auto& item : queued) {
        if(item.promise) item.promise->resolve();
      }
    }

    void consume(Slot& slot, std::size_t size) {
      const std::uint64_t n = static_cast<std::uint64_t>(size);
      slot.sent = sat_add(slot.sent, n);
      if(slot.remaining > n) slot.remaining -= n;
      else slot.remaining = 0;
    }

    Slot* pick_ready() {
      Slot* chosen = nullptr;
      std::uint64_t chosen_id = 0;
      std::size_t best_starved = 0;
      std::vector<Slot*> ready;
      for(auto& [id, slot] : slots_) {
        if(slot.outbound.empty() || slot.remaining == 0) continue;
        ready.push_back(&slot);
        if(!chosen
            || slot.starved > best_starved
            || (slot.starved == best_starved && id < chosen_id)) {
          chosen = &slot;
          chosen_id = id;
          best_starved = slot.starved;
        }
      }
      if(!chosen) return nullptr;
      for(Slot* slot : ready) {
        if(slot != chosen) slot->starved++;
      }
      return chosen;
    }

    void pump() {
      if(pumping_) return;
      pumping_ = true;
      while(true) {
        Slot* slot = pick_ready();
        if(!slot) break;
        if(slot->deficit == 0) slot->deficit += quantum_;
        Outbound& item = slot->outbound.front();
        waos::mux_socket_data msg;
        msg.socket_id = slot->id;
        msg.payload = item.payload;
        if(!channel_.template try_queue_message<waos::mux_socket_data>(msg)) {
          break;
        }
        const std::size_t size = item.payload.size();
        auto promise = std::move(item.promise);
        slot->outbound.pop_front();
        consume(*slot, size);
        slot->deficit = slot->deficit > size ? slot->deficit - size : 0;
        slot->starved = 0;
        if(promise) promise->resolve();
      }
      pumping_ = false;
      if constexpr (requires { channel_.flush(); }) {
        channel_.flush();
      }
    }
  };

}
