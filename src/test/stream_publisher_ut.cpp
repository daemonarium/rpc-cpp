#include <boost/ut.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "observable/observable.hpp"
#include "observable/stream_publisher.hpp"
#include "observable/stream_relay_consumer.hpp"

using namespace boost::ut;
using namespace waos::observable;

namespace {

  binary b(std::string_view s) {
    return binary(s.begin(), s.end());
  }

  struct ManualSource : Observable<BinaryEntry> {
    int starts = 0;
    int stops = 0;
    int pauses = 0;
    int resumes = 0;

    void onStart() override { ++starts; }
    void onStop() override { ++stops; }
    void onPause() override { ++pauses; }
    void onResume() override { ++resumes; }

    void push(binary k, binary v, std::uint64_t ts) {
      onItem(BinaryEntry{std::move(k), std::move(v), ts});
    }
    void done() { onDone(); }
    void reset() { onReset(); }
    void fail(std::string m) {
      onError(std::make_exception_ptr(std::runtime_error(std::move(m))));
    }
  };

  using WireMsg = std::variant<
    waos::socket_opened,
    waos::stream_opened,
    waos::stream_item,
    waos::stream_done,
    waos::stream_reset,
    waos::stream_error,
    waos::stream_cancel>;

  struct MockChannel {
    bool writable = true;
    std::vector<WireMsg> sent;
    std::unordered_map<std::type_index, std::function<void(void*)>> handlers;

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
    void inject(T msg) {
      auto it = handlers.find(std::type_index(typeid(T)));
      if(it == handlers.end()) return;
      it->second(&msg);
    }
  };

  template <typename T>
  std::vector<T> ofType(const std::vector<WireMsg>& v) {
    std::vector<T> out;
    for(auto const& m : v) {
      if(auto* p = std::get_if<T>(&m)) out.push_back(*p);
    }
    return out;
  }

}

int main() {
  "1 relay maps onChange onReady onReset onError with stream_id"_test = [] {
    MockChannel ch;
    auto c = std::make_shared<StreamRelayConsumer<MockChannel>>(ch, 7, 0);
    auto p = c->onChange(Change{b("k"), b("v"), 3});
    expect(p->state == serialization::promise_state::resolved);
    c->onReady();
    auto items = ofType<waos::stream_item>(ch.sent);
    auto dones = ofType<waos::stream_done>(ch.sent);
    expect(eq(items.size(), std::size_t{1}));
    expect(eq(items[0].stream_id, std::uint64_t{7}));
    expect(items[0].key == b("k"));
    expect(items[0].value == b("v"));
    expect(eq(items[0].timestamp, std::uint64_t{3}));
    expect(eq(dones.size(), std::size_t{1}));
    expect(eq(dones[0].stream_id, std::uint64_t{7}));

    MockChannel ch2;
    auto c2 = std::make_shared<StreamRelayConsumer<MockChannel>>(ch2, 8, 0);
    c2->onChange(Change{b("a"), b("b"), 1});
    c2->onReset();
    auto resets = ofType<waos::stream_reset>(ch2.sent);
    expect(eq(resets.size(), std::size_t{1}));
    expect(eq(resets[0].stream_id, std::uint64_t{8}));

    MockChannel ch3;
    auto c3 = std::make_shared<StreamRelayConsumer<MockChannel>>(ch3, 9, 0);
    c3->onChange(Change{b("x"), b("y"), 2});
    c3->onError(std::make_exception_ptr(std::runtime_error("boom")));
    auto errors = ofType<waos::stream_error>(ch3.sent);
    expect(eq(errors.size(), std::size_t{1}));
    expect(eq(errors[0].stream_id, std::uint64_t{9}));
    expect(errors[0].message == std::string("boom"));
  };

  "2 attach queues stream_opened before any stream_item"_test = [] {
    ManualSource src;
    MockChannel ch;
    StreamPublisher pub(ch);
    pub.attach(1, src);
    src.push(b("k"), b("v"), 1);
    expect(eq(ch.sent.size(), std::size_t{2}));
    expect(std::holds_alternative<waos::stream_opened>(ch.sent[0]));
    expect(eq(std::get<waos::stream_opened>(ch.sent[0]).stream_id, std::uint64_t{1}));
    expect(std::holds_alternative<waos::stream_item>(ch.sent[1]));
    expect(eq(std::get<waos::stream_item>(ch.sent[1]).stream_id, std::uint64_t{1}));
  };

  "3 replay respects since"_test = [] {
    ManualSource src;
    src.push(b("a"), b("1"), 1);
    src.push(b("b"), b("2"), 2);
    src.push(b("c"), b("3"), 3);
    MockChannel ch;
    StreamPublisher pub(ch);
    pub.attach(4, src, 2);
    auto items = ofType<waos::stream_item>(ch.sent);
    expect(eq(items.size(), std::size_t{1}));
    expect(items[0].key == b("c"));
    expect(eq(items[0].timestamp, std::uint64_t{3}));
    expect(eq(items[0].stream_id, std::uint64_t{4}));
  };

  "4 socket full pending onChange pause then drain resume"_test = [] {
    ManualSource src;
    MockChannel ch;
    StreamPublisher pub(ch);
    pub.attach(1, src, 0, 2);
    ch.writable = false;
    src.push(b("a"), b("1"), 1);
    src.push(b("b"), b("2"), 2);
    src.push(b("c"), b("3"), 3);
    expect(eq(ofType<waos::stream_item>(ch.sent).size(), std::size_t{0}));
    expect(eq(src.pauses, 1));
    ch.writable = true;
    pub.onWritable();
    auto items = ofType<waos::stream_item>(ch.sent);
    expect(eq(items.size(), std::size_t{3}));
    expect(items[0].key == b("a"));
    expect(items[1].key == b("b"));
    expect(items[2].key == b("c"));
    expect(eq(src.resumes, 1));
  };

  "5 stream_done waits for blocked item"_test = [] {
    ManualSource src;
    MockChannel ch;
    StreamPublisher pub(ch);
    pub.attach(1, src);
    ch.writable = false;
    src.push(b("k"), b("v"), 1);
    src.done();
    expect(eq(ofType<waos::stream_item>(ch.sent).size(), std::size_t{0}));
    expect(eq(ofType<waos::stream_done>(ch.sent).size(), std::size_t{0}));
    ch.writable = true;
    pub.onWritable();
    expect(eq(ch.sent.size(), std::size_t{3}));
    expect(std::holds_alternative<waos::stream_opened>(ch.sent[0]));
    expect(std::holds_alternative<waos::stream_item>(ch.sent[1]));
    expect(std::holds_alternative<waos::stream_done>(ch.sent[2]));
  };

  "6 onReset while blocked drops item then sends reset"_test = [] {
    ManualSource src;
    MockChannel ch;
    StreamPublisher pub(ch);
    pub.attach(1, src);
    ch.writable = false;
    src.push(b("old"), b("v"), 1);
    src.reset();
    ch.writable = true;
    pub.onWritable();
    expect(eq(ofType<waos::stream_item>(ch.sent).size(), std::size_t{0}));
    auto resets = ofType<waos::stream_reset>(ch.sent);
    expect(eq(resets.size(), std::size_t{1}));
    expect(eq(resets[0].stream_id, std::uint64_t{1}));
    src.push(b("new"), b("w"), 2);
    auto items = ofType<waos::stream_item>(ch.sent);
    expect(eq(items.size(), std::size_t{1}));
    expect(items[0].key == b("new"));
  };

  "7 two ids one Observable cancel one keeps the other"_test = [] {
    ManualSource src;
    MockChannel ch;
    StreamPublisher pub(ch);
    pub.attach(1, src);
    pub.attach(2, src);
    expect(eq(src.starts, 1));
    src.push(b("k"), b("v"), 1);
    expect(eq(ofType<waos::stream_item>(ch.sent).size(), std::size_t{2}));
    ch.inject(waos::stream_cancel{1});
    expect(pub.isAttached(1) == false);
    expect(pub.isAttached(2));
    expect(src.state() == ObservableState::loading);
    src.push(b("k2"), b("v2"), 2);
    auto items = ofType<waos::stream_item>(ch.sent);
    expect(eq(items.size(), std::size_t{3}));
    expect(eq(items[2].stream_id, std::uint64_t{2}));
    ch.inject(waos::stream_cancel{2});
    expect(eq(src.stops, 1));
    expect(src.state() == ObservableState::idle);
  };

  "8 stream_cancel after attach unsubscribes last consumer idle"_test = [] {
    ManualSource src;
    MockChannel ch;
    StreamPublisher pub(ch);
    pub.attach(5, src);
    expect(eq(src.starts, 1));
    ch.inject(waos::stream_cancel{5});
    expect(eq(src.stops, 1));
    expect(src.state() == ObservableState::idle);
    expect(pub.size() == std::size_t{0});
  };

  "9 cancel before attach is no-op"_test = [] {
    ManualSource src;
    MockChannel ch;
    StreamPublisher pub(ch);
    ch.inject(waos::stream_cancel{9});
    pub.attach(9, src);
    expect(eq(src.starts, 0));
    expect(eq(ofType<waos::stream_opened>(ch.sent).size(), std::size_t{0}));
    expect(eq(ofType<waos::stream_item>(ch.sent).size(), std::size_t{0}));
    expect(pub.size() == std::size_t{0});
  };

  "10 duplicate attach same id replaces"_test = [] {
    ManualSource src;
    MockChannel ch;
    StreamPublisher pub(ch);
    pub.attach(1, src);
    src.push(b("a"), b("1"), 1);
    pub.attach(1, src);
    expect(eq(src.starts, 2));
    expect(eq(src.stops, 1));
    expect(eq(ofType<waos::stream_opened>(ch.sent).size(), std::size_t{2}));
    expect(pub.size() == std::size_t{1});
  };

  "11 detach unknown and second cancel are no-ops"_test = [] {
    ManualSource src;
    MockChannel ch;
    StreamPublisher pub(ch);
    pub.detach(99);
    ch.inject(waos::stream_cancel{99});
    ch.inject(waos::stream_cancel{99});
    expect(eq(src.starts, 0));
    expect(pub.size() == std::size_t{0});
  };

  "12 publisher dtor unsubscribes remaining"_test = [] {
    ManualSource src;
    {
      MockChannel ch;
      StreamPublisher pub(ch);
      pub.attach(3, src);
      expect(eq(src.starts, 1));
    }
    expect(eq(src.stops, 1));
    expect(src.state() == ObservableState::idle);
  };

  "opened pending until writable then subscribe"_test = [] {
    ManualSource src;
    MockChannel ch;
    ch.writable = false;
    StreamPublisher pub(ch);
    pub.attach(1, src);
    expect(eq(src.starts, 0));
    expect(eq(ofType<waos::stream_opened>(ch.sent).size(), std::size_t{0}));
    src.push(b("k"), b("v"), 1);
    ch.writable = true;
    pub.onWritable();
    expect(eq(src.starts, 1));
    expect(std::holds_alternative<waos::stream_opened>(ch.sent[0]));
    expect(std::holds_alternative<waos::stream_item>(ch.sent[1]));
  };
}
