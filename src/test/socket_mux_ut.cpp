#include <boost/ut.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "observable/socket_mux.hpp"

  using namespace boost::ut;
using namespace waos::observable;
using waos::binary;

namespace {

  using WireMsg = std::variant<
    waos::mux_socket_open,
    waos::mux_socket_data,
    waos::mux_socket_close,
    waos::mux_socket_window>;

  struct MockChannel {
    bool writable = true;
    std::vector<WireMsg> sent;
    std::unordered_map<std::type_index, std::function<void(void*)>> handlers;
    std::vector<std::function<void()>> writable_handlers;

    template <typename T>
    bool try_queue_message(const T& m) {
      if(!writable) return false;
      sent.push_back(m);
      return true;
    }

    template <typename T>
    void send_message(const T& m) {
      sent.push_back(m);
    }

    template <typename T>
    void on_message(std::function<void(T&)> handler) {
      handlers[std::type_index(typeid(T))] = [h = std::move(handler)](void* p) {
        h(*static_cast<T*>(p));
      };
    }

    template <typename T>
    void emit(T msg) {
      auto it = handlers.find(std::type_index(typeid(T)));
      if(it == handlers.end()) return;
      it->second(&msg);
    }

    template <typename T>
    std::size_t count() const {
      std::size_t n = 0;
      for(const auto& msg : sent) {
        if(std::holds_alternative<T>(msg)) ++n;
      }
      return n;
    }

    template <typename T>
    const T* last() const {
      for(auto it = sent.rbegin(); it != sent.rend(); ++it) {
        if(const T* p = std::get_if<T>(&*it)) return p;
      }
      return nullptr;
    }
  };

  binary bytes(std::size_t n, std::uint8_t fill = 1) {
    return binary(n, fill);
  }

}

int main() {
  "open data close round-trip"_test = [] {
    MockChannel ch;
    std::vector<binary> received;
    SocketMux<MockChannel>::Options opt;
    opt.on_data = [&](std::uint32_t, const binary& p) { received.push_back(p); };
    SocketMux<MockChannel> mux(ch, opt);
    mux.open_socket(7);
    expect(ch.count<waos::mux_socket_open>() == 1_ul);
    mux.grant_window(7, 100);
    expect(ch.count<waos::mux_socket_window>() == 1_ul);
    ch.emit(waos::mux_socket_window{7, 100});
    auto p = mux.send_data(7, bytes(4, 9));
    expect(p->state == serialization::promise_state::resolved);
    const auto* data = ch.last<waos::mux_socket_data>();
    expect(data != nullptr);
    expect(data->socket_id == 7_ul);
    expect(data->payload.size() == 4_ul);
    ch.emit(waos::mux_socket_data{7, bytes(2, 3)});
    expect(received.size() == 1_ul);
    mux.close_socket(7);
    expect(ch.count<waos::mux_socket_close>() == 1_ul);
    expect(!mux.has_socket(7));
  };

  "window flow control blocks then resumes"_test = [] {
    MockChannel ch;
    SocketMux<MockChannel> mux(ch);
    mux.open_socket(1);
    auto first = mux.send_data(1, bytes(8));
    expect(first->state == serialization::promise_state::pending);
    expect(ch.count<waos::mux_socket_data>() == 0_ul);
    ch.emit(waos::mux_socket_window{1, 8});
    expect(first->state == serialization::promise_state::resolved);
    expect(ch.count<waos::mux_socket_data>() == 1_ul);
    auto second = mux.send_data(1, bytes(8));
    expect(second->state == serialization::promise_state::pending);
    ch.emit(waos::mux_socket_window{1, 8});
    expect(second->state == serialization::promise_state::resolved);
  };

  "send-one-past oversized frame then block"_test = [] {
    MockChannel ch;
    SocketMux<MockChannel> mux(ch);
    mux.open_socket(1);
    ch.emit(waos::mux_socket_window{1, 10});
    auto first = mux.send_data(1, bytes(40));
    expect(first->state == serialization::promise_state::resolved);
    expect(mux.remaining(1) == 0_u);
    auto second = mux.send_data(1, bytes(4));
    expect(second->state == serialization::promise_state::pending);
    expect(ch.count<waos::mux_socket_data>() == 1_ul);
  };

  "stalled A does not block B"_test = [] {
    MockChannel ch;
    SocketMux<MockChannel> mux(ch);
    mux.open_socket(1);
    mux.open_socket(2);
    ch.emit(waos::mux_socket_window{2, 100});
    auto blocked = mux.send_data(1, bytes(8));
    auto ok = mux.send_data(2, bytes(8));
    expect(blocked->state == serialization::promise_state::pending);
    expect(ok->state == serialization::promise_state::resolved);
    expect(ch.count<waos::mux_socket_data>() == 1_ul);
    expect(ch.last<waos::mux_socket_data>()->socket_id == 2_ul);
  };

  "DRR fairness two ready sockets"_test = [] {
    MockChannel ch;
    SocketMux<MockChannel>::Options opt;
    opt.quantum = 1;
    SocketMux<MockChannel> mux(ch, opt);
    mux.open_socket(1);
    mux.open_socket(2);
    ch.emit(waos::mux_socket_window{1, 100});
    ch.emit(waos::mux_socket_window{2, 100});
    ch.writable = false;
    mux.send_data(1, bytes(4, 1));
    mux.send_data(1, bytes(4, 1));
    mux.send_data(2, bytes(4, 2));
    mux.send_data(2, bytes(4, 2));
    ch.writable = true;
    mux.on_writable();
    std::size_t a = 0, b = 0;
    std::vector<std::uint32_t> ids;
    for(const auto& msg : ch.sent) {
      if(const auto* d = std::get_if<waos::mux_socket_data>(&msg)) {
        ids.push_back(d->socket_id);
        if(d->socket_id == 1) ++a;
        if(d->socket_id == 2) ++b;
      }
    }
    expect(a == 2_ul);
    expect(b == 2_ul);
    expect(ids.size() >= 2_ul);
    expect(ids[0] != ids[1]);
  };

  "close zeroes credit and resolves blocked send"_test = [] {
    MockChannel ch;
    SocketMux<MockChannel> mux(ch);
    mux.open_socket(3);
    auto blocked = mux.send_data(3, bytes(8));
    expect(blocked->state == serialization::promise_state::pending);
    mux.close_socket(3);
    expect(blocked->state == serialization::promise_state::resolved);
    expect(mux.remaining(3) == 0_u);
    mux.open_socket(3);
    expect(mux.remaining(3) == 0_u);
  };

  "unknown socket_id is a no-op"_test = [] {
    MockChannel ch;
    SocketMux<MockChannel> mux(ch);
    ch.emit(waos::mux_socket_window{99, 50});
    ch.emit(waos::mux_socket_data{99, bytes(4)});
    ch.emit(waos::mux_socket_close{99});
    auto p = mux.send_data(99, bytes(4));
    expect(p->state == serialization::promise_state::resolved);
    expect(ch.count<waos::mux_socket_data>() == 0_ul);
  };

  "control frames pass when remaining is 0"_test = [] {
    MockChannel ch;
    SocketMux<MockChannel> mux(ch);
    mux.open_socket(1);
    auto blocked = mux.send_data(1, bytes(8));
    expect(blocked->state == serialization::promise_state::pending);
    mux.close_socket(1);
    expect(ch.count<waos::mux_socket_close>() == 1_ul);
    expect(blocked->state == serialization::promise_state::resolved);
  };

  "paused grants flush on resume"_test = [] {
    MockChannel ch;
    SocketMux<MockChannel> mux(ch);
    mux.open_socket(1);
    mux.set_paused(1, true);
    mux.grant_window(1, 50);
    expect(ch.count<waos::mux_socket_window>() == 0_ul);
    mux.set_paused(1, false);
    expect(ch.count<waos::mux_socket_window>() == 1_ul);
  };

  "open carries kind ref_id cookie"_test = [] {
    MockChannel ch;
    SocketMux<MockChannel> mux(ch);
    mux.open_socket(4, waos::mux_kind_pipe, 0, 99);
    const auto* opened = ch.last<waos::mux_socket_open>();
    expect(opened != nullptr);
    expect(opened->socket_id == 4_u);
    expect(opened->kind == waos::mux_kind_pipe);
    expect(opened->cookie == 99_u);
  };

  "duplex both sides grant and send"_test = [] {
    MockChannel ch;
    std::vector<binary> received;
    SocketMux<MockChannel>::Options opt;
    opt.on_data = [&](std::uint32_t, const binary& p) { received.push_back(p); };
    SocketMux<MockChannel> mux(ch, opt);
    mux.open_socket(1, waos::mux_kind_pipe, 0, 1);
    mux.grant_window(1, 64);
    ch.emit(waos::mux_socket_window{1, 64});
    auto p = mux.send_data(1, bytes(4, 8));
    expect(p->state == serialization::promise_state::resolved);
    ch.emit(waos::mux_socket_data{1, bytes(3, 9)});
    expect(received.size() == 1_ul);
  };
}
