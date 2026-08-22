#include <boost/ut.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "observable/observable.hpp"
#include "observable/ring_buffer.hpp"

using namespace boost::ut;
using namespace waos::observable;

namespace {

  binary b(std::string_view s) {
    return binary(s.begin(), s.end());
  }

  struct RecordingConsumer {
    std::vector<Change> changes;
    std::uint64_t since = 0;
    int readys = 0;
    int resets = 0;
    int errors = 0;
    int closeds = 0;

    std::shared_ptr<serialization::promise<void>> onChange(Change c) {
      changes.push_back(std::move(c));
      return serialization::promise<void>::resolved();
    }
    void onReady() { ++readys; }
    void onReset() {
      ++resets;
      changes.clear();
    }
    void onError(std::exception_ptr) { ++errors; }
    void onClosed() { ++closeds; }
    std::uint64_t sinceTimestamp() const { return since; }
  };

  struct BlockingConsumer {
    std::vector<Change> changes;
    std::uint64_t since = 0;
    int readys = 0;
    int resets = 0;
    int errors = 0;
    int closeds = 0;
    std::shared_ptr<serialization::promise<void>> gate
      = std::make_shared<serialization::promise<void>>();

    std::shared_ptr<serialization::promise<void>> onChange(Change c) {
      changes.push_back(std::move(c));
      return gate;
    }
    void onReady() { ++readys; }
    void onReset() {
      ++resets;
      changes.clear();
    }
    void onError(std::exception_ptr) { ++errors; }
    void onClosed() { ++closeds; }
    std::uint64_t sinceTimestamp() const { return since; }

    void release() {
      if(gate && gate->state == serialization::promise_state::pending) {
        gate->resolve();
      }
    }
  };

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
    void recover() { onReset(); }
  };

}

int main() {
  "1 idle to loading on subscribe"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    expect(src.state() == ObservableState::idle);
    src.subscribe(c);
    expect(src.starts == 1);
    expect(src.state() == ObservableState::loading);
  };

  "2 loading to ready on done"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    bool resolved = false;
    src.readyPromise()->on_resolved([&] { resolved = true; });
    expect(src.readyPromise()->state == serialization::promise_state::pending);
    src.done();
    expect(src.state() == ObservableState::ready);
    expect(resolved);
    expect(c->readys == 1);
  };

  "3 ready to reloading on reset oldData readable"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.push(b("a"), b("1"), 1);
    src.done();
    src.reset();
    expect(src.state() == ObservableState::reloading);
    auto entries = src.getEntries();
    expect(eq(entries.size(), std::size_t{1}));
    expect(entries[0].key == b("a"));
    expect(src.get(b("a")).has_value());
  };

  "4 reloading to ready atomic swap"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.push(b("a"), b("1"), 1);
    src.done();
    src.reset();
    src.push(b("b"), b("2"), 2);
    expect(src.getEntries()[0].key == b("a"));
    src.done();
    expect(src.state() == ObservableState::ready);
    auto entries = src.getEntries();
    expect(eq(entries.size(), std::size_t{1}));
    expect(entries[0].key == b("b"));
  };

  "5 error retains view"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.push(b("a"), b("1"), 1);
    src.done();
    src.fail("boom");
    expect(src.state() == ObservableState::error);
    expect(eq(src.getEntries().size(), std::size_t{1}));
    expect(c->errors == 1);
    expect(src.lastError() != nullptr);
  };

  "6 error to loading on onItem"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.done();
    src.fail("boom");
    src.push(b("a"), b("1"), 1);
    expect(src.state() == ObservableState::loading);
  };

  "7 last unsubscribe to idle retains view"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    auto out = src.subscribe(c);
    src.push(b("a"), b("1"), 1);
    src.done();
    out->unsubscribe();
    expect(src.state() == ObservableState::idle);
    expect(src.stops == 1);
    expect(eq(src.getEntries().size(), std::size_t{1}));
  };

  "8 idle to loading on resubscribe"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    auto out = src.subscribe(c);
    expect(src.starts == 1);
    out->unsubscribe();
    auto c2 = std::make_shared<RecordingConsumer>();
    src.subscribe(c2);
    expect(src.starts == 2);
    expect(src.state() == ObservableState::loading);
  };

  "9 kill is terminal closed"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.kill();
    expect(src.state() == ObservableState::closed);
    expect(c->closeds == 1);
    auto c2 = std::make_shared<RecordingConsumer>();
    expect(src.subscribe(c2) == nullptr);
  };

  "10 second subscribe does not onStart"_test = [] {
    ManualSource src;
    auto c1 = std::make_shared<RecordingConsumer>();
    auto c2 = std::make_shared<RecordingConsumer>();
    src.subscribe(c1);
    src.subscribe(c2);
    expect(src.starts == 1);
  };

  "11 both consumers get live changes"_test = [] {
    ManualSource src;
    auto c1 = std::make_shared<RecordingConsumer>();
    auto c2 = std::make_shared<RecordingConsumer>();
    src.subscribe(c1);
    src.subscribe(c2);
    src.push(b("k"), b("v"), 1);
    expect(eq(c1->changes.size(), std::size_t{1}));
    expect(eq(c2->changes.size(), std::size_t{1}));
    expect(c1->changes[0].key == b("k"));
    expect(c2->changes[0].key == b("k"));
  };

  "12 unsubscribe one does not stop upstream"_test = [] {
    ManualSource src;
    auto c1 = std::make_shared<RecordingConsumer>();
    auto c2 = std::make_shared<RecordingConsumer>();
    auto o1 = src.subscribe(c1);
    src.subscribe(c2);
    o1->unsubscribe();
    expect(src.stops == 0);
    expect(src.state() == ObservableState::loading);
  };

  "13 unsubscribe both stops upstream"_test = [] {
    ManualSource src;
    auto c1 = std::make_shared<RecordingConsumer>();
    auto c2 = std::make_shared<RecordingConsumer>();
    auto o1 = src.subscribe(c1);
    auto o2 = src.subscribe(c2);
    o1->unsubscribe();
    o2->unsubscribe();
    expect(src.stops == 1);
    expect(src.state() == ObservableState::idle);
  };

  "14 new consumer after ready gets full replay"_test = [] {
    ManualSource src;
    auto c1 = std::make_shared<RecordingConsumer>();
    src.subscribe(c1);
    src.push(b("a"), b("1"), 1);
    src.push(b("b"), b("2"), 2);
    src.done();
    auto c2 = std::make_shared<RecordingConsumer>();
    src.subscribe(c2);
    expect(eq(c2->changes.size(), std::size_t{2}));
    expect(c2->changes[0].key == b("a"));
    expect(c2->changes[1].key == b("b"));
    expect(c2->readys == 1);
  };

  "15 returning consumer sinceTimestamp filters replay"_test = [] {
    ManualSource src;
    auto c1 = std::make_shared<RecordingConsumer>();
    src.subscribe(c1);
    src.push(b("a"), b("1"), 1);
    src.push(b("b"), b("2"), 5);
    src.push(b("c"), b("3"), 10);
    src.done();
    auto c2 = std::make_shared<RecordingConsumer>();
    c2->since = 5;
    src.subscribe(c2);
    expect(eq(c2->changes.size(), std::size_t{1}));
    expect(c2->changes[0].key == b("c"));
  };

  "16 replay is local no onStart"_test = [] {
    ManualSource src;
    auto c1 = std::make_shared<RecordingConsumer>();
    src.subscribe(c1);
    src.done();
    expect(src.starts == 1);
    auto c2 = std::make_shared<RecordingConsumer>();
    src.subscribe(c2);
    expect(src.starts == 1);
  };

  "17 higher ts replaces"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.push(b("a"), b("1"), 1);
    src.push(b("a"), b("2"), 2);
    auto e = src.get(b("a"));
    expect(e.has_value());
    expect(e->value == b("2"));
    expect(eq(e->ts, std::uint64_t{2}));
  };

  "18 lower ts ignored"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.push(b("a"), b("2"), 2);
    src.push(b("a"), b("1"), 1);
    auto e = src.get(b("a"));
    expect(e.has_value());
    expect(e->value == b("2"));
    expect(eq(c->changes.size(), std::size_t{1}));
  };

  "19 empty value tombstone erases"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.push(b("a"), b("1"), 1);
    src.push(b("a"), binary{}, 2);
    expect(!src.get(b("a")).has_value());
    expect(eq(src.getEntries().size(), std::size_t{0}));
    expect(c->changes.back().value.empty());
  };

  "20 maxTs monotonic"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.push(b("a"), b("1"), 3);
    src.push(b("b"), b("2"), 1);
    src.push(b("a"), binary{}, 5);
    expect(eq(src.maxTs(), std::uint64_t{5}));
  };

  "21 drop consumer detaches and stops"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    expect(src.starts == 1);
    c.reset();
    src.push(b("x"), b("y"), 1);
    expect(src.stops == 1);
    expect(src.state() == ObservableState::idle);
  };

  "22 ring_buffer push false when full"_test = [] {
    ring_buffer<int> rb(2);
    expect(rb.push(1));
    expect(rb.push(2));
    expect(!rb.push(3));
    expect(eq(rb.size(), std::size_t{2}));
    expect(eq(*rb.pop(), 1));
    expect(eq(*rb.pop(), 2));
  };

  "23 ring_buffer wrap around fifo"_test = [] {
    ring_buffer<int> rb(3);
    expect(rb.push(1));
    expect(rb.push(2));
    expect(rb.push(3));
    expect(eq(*rb.pop(), 1));
    expect(eq(*rb.pop(), 2));
    expect(rb.push(4));
    expect(rb.push(5));
    expect(eq(*rb.pop(), 3));
    expect(eq(*rb.pop(), 4));
    expect(eq(*rb.pop(), 5));
    expect(rb.empty());
  };

  "24 blocking consumer pause on full buffer"_test = [] {
    ManualSource src;
    auto c = std::make_shared<BlockingConsumer>();
    src.subscribe(c, 1);
    src.push(b("a"), b("1"), 1);
    expect(eq(c->changes.size(), std::size_t{1}));
    expect(src.pauses == 0);
    src.push(b("b"), b("2"), 2);
    expect(src.pauses == 1);
    expect(eq(c->changes.size(), std::size_t{1}));
  };

  "25 resolve gate drains and resumes"_test = [] {
    ManualSource src;
    auto c = std::make_shared<BlockingConsumer>();
    src.subscribe(c, 1);
    src.push(b("a"), b("1"), 1);
    src.push(b("b"), b("2"), 2);
    expect(src.pauses == 1);
    c->release();
    expect(eq(c->changes.size(), std::size_t{2}));
    expect(c->changes[1].key == b("b"));
    expect(src.resumes == 1);
  };

  "26 one blocking one sync does not pause source"_test = [] {
    ManualSource src;
    auto slow = std::make_shared<BlockingConsumer>();
    auto fast = std::make_shared<RecordingConsumer>();
    src.subscribe(slow, 1);
    src.subscribe(fast, 1);
    src.push(b("a"), b("1"), 1);
    src.push(b("b"), b("2"), 2);
    expect(src.pauses == 0);
    expect(eq(fast->changes.size(), std::size_t{2}));
    expect(eq(slow->changes.size(), std::size_t{1}));
  };

  "27 no data loss after drain"_test = [] {
    ManualSource src;
    auto c = std::make_shared<BlockingConsumer>();
    src.subscribe(c, 8);
    for(int i = 0; i < 9; ++i) {
      src.push(b(std::to_string(i)), b("v"), static_cast<std::uint64_t>(i + 1));
    }
    expect(eq(c->changes.size(), std::size_t{1}));
    c->release();
    expect(eq(c->changes.size(), std::size_t{9}));
    expect(c->changes.front().key == b("0"));
    expect(c->changes.back().key == b("8"));
  };

  "28 reset fans out and consumer clears"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.push(b("a"), b("1"), 1);
    src.push(b("gone"), b("x"), 2);
    src.done();
    expect(eq(c->changes.size(), std::size_t{2}));
    src.reset();
    expect(c->resets == 1);
    expect(eq(c->changes.size(), std::size_t{0}));
    src.push(b("a"), b("1"), 1);
    src.done();
    expect(eq(c->changes.size(), std::size_t{1}));
    expect(c->changes[0].key == b("a"));
    expect(c->readys == 2);
  };

  "29 error to reloading via reset"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.push(b("a"), b("1"), 1);
    src.done();
    src.fail("boom");
    expect(src.state() == ObservableState::error);
    src.recover();
    expect(src.state() == ObservableState::reloading);
    expect(c->resets == 1);
    src.push(b("b"), b("2"), 3);
    src.done();
    expect(src.state() == ObservableState::ready);
    expect(eq(src.getEntries().size(), std::size_t{1}));
    expect(src.getEntries()[0].key == b("b"));
  };

  "30 readyPromise reresolves after reload"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    auto first = src.readyPromise();
    src.done();
    expect(first->state == serialization::promise_state::resolved);
    src.reset();
    auto second = src.readyPromise();
    expect(second.get() != first.get());
    expect(second->state == serialization::promise_state::pending);
    bool again = false;
    second->on_resolved([&] { again = true; });
    src.push(b("n"), b("v"), 1);
    src.done();
    expect(again);
    expect(second->state == serialization::promise_state::resolved);
  };

  "31 error to idle retains lastError"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    auto out = src.subscribe(c);
    src.done();
    src.fail("boom");
    out->unsubscribe();
    expect(src.state() == ObservableState::idle);
    expect(src.lastError() != nullptr);
    auto c2 = std::make_shared<RecordingConsumer>();
    src.subscribe(c2);
    expect(src.starts == 2);
    expect(src.state() == ObservableState::loading);
  };

  "32 recover via retransmit"_test = [] {
    ManualSource src;
    auto c = std::make_shared<RecordingConsumer>();
    src.subscribe(c);
    src.push(b("old"), b("1"), 1);
    src.done();
    src.fail("boom");
    src.recover();
    src.push(b("new"), b("2"), 2);
    src.done();
    expect(src.state() == ObservableState::ready);
    expect(eq(c->changes.size(), std::size_t{1}));
    expect(c->changes[0].key == b("new"));
    expect(eq(src.getEntries().size(), std::size_t{1}));
    expect(src.getEntries()[0].key == b("new"));
  };
}
