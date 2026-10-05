#include <boost/ut.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include "examples/structures/db.hpp"
#include "observable/remote_observable.hpp"
#include "observable/remote_registry.hpp"
#include "observable/stream_multiplexer.hpp"
#include "serialization/promise.hpp"

using namespace boost::ut;
using namespace waos::observable;

namespace {

  constexpr std::uint64_t kDbStartScan = 5;

  binary bin(std::string_view s) {
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

  struct MockClient {
    std::unordered_map<std::type_index, std::function<void(void*)>> handlers;
    struct StartCall {
      std::uint64_t function_id = 0;
      waos::stream_handle handle;
      waos::db_scan_query query;
    };
    std::vector<StartCall> starts;
    std::vector<waos::stream_cancel> cancels;

    template <typename T>
    void on_message(std::function<void(T&)> handler) {
      handlers[std::type_index(typeid(T))] = [h = std::move(handler)](void* p) {
        h(*static_cast<T*>(p));
      };
    }

    template <typename T>
    void send_message(const T& msg) {
      if constexpr (std::is_same_v<T, waos::stream_cancel>) {
        cancels.push_back(msg);
      }
    }

    template <typename Rt, typename... Args>
    std::shared_ptr<serialization::promise<Rt>> call(std::uint64_t function_id, Args... args) {
      if constexpr (sizeof...(Args) >= 2) {
        auto tup = std::make_tuple(std::move(args)...);
        starts.push_back(StartCall{
          function_id,
          std::get<0>(tup),
          std::get<1>(tup),
        });
      }
      if constexpr (std::is_void_v<Rt>) {
        return serialization::promise<void>::resolved();
      } else {
        return serialization::promise<Rt>::resolved(Rt{});
      }
    }
  };

}

int main() {
  "key equality same args bytes same key different args different"_test = [] {
    waos::db_scan_query q;
    auto a = serializeObservationArgs(std::make_tuple(q));
    auto b = serializeObservationArgs(std::make_tuple(q));
    expect(a == b);

    waos::db_scan_query other;
    other.limit = 1;
    auto c = serializeObservationArgs(std::make_tuple(other));
    expect(a != c);

    ObservationKey k1{kDbStartScan, a};
    ObservationKey k2{kDbStartScan, b};
    ObservationKey k3{kDbStartScan, c};
    ObservationKey k4{kDbStartScan + 1, a};
    expect(k1 == k2);
    expect(!(k1 == k3));
    expect(!(k1 == k4));
  };

  "dedup two getOrCreate same key one observable one createStream"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RemoteRegistry registry(mux);
    waos::db_scan_query q;
    auto a = registry.getOrCreate(kDbStartScan, q);
    auto b = registry.getOrCreate(kDbStartScan, q);
    expect(a.get() == b.get());
    expect(eq(registry.size(), std::size_t{1}));
    expect(eq(client.starts.size(), std::size_t{0}));

    auto again = registry.getOrCreate(kDbStartScan, q);
    expect(a.get() == again.get());
    expect(eq(registry.size(), std::size_t{1}));

    auto c1 = std::make_shared<RecordingConsumer>();
    auto c2 = std::make_shared<RecordingConsumer>();
    a->observable().subscribe(c1);
    b->observable().subscribe(c2);
    expect(eq(client.starts.size(), std::size_t{1}));
    expect(eq(client.starts[0].function_id, kDbStartScan));
  };

  "alive across idle unsubscribe all still in registry"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RemoteRegistry registry(mux);
    waos::db_scan_query q;
    auto obs = registry.getOrCreate(kDbStartScan, q);
    auto c = std::make_shared<RecordingConsumer>();
    auto out = obs->observable().subscribe(c);
    expect(eq(client.starts.size(), std::size_t{1}));
    out->unsubscribe();
    expect(obs->observable().state() == ObservableState::idle);
    expect(eq(registry.size(), std::size_t{1}));
    expect(registry.getOrCreate(kDbStartScan, q).get() == obs.get());
  };

  "delta resume maxTs retained across idle since not 0"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RemoteRegistry registry(mux);
    waos::db_scan_query q;
    auto ref = registry.getOrCreate(kDbStartScan, q);
    auto& obs = ref->observable();
    auto c = std::make_shared<RecordingConsumer>();
    auto out = obs.subscribe(c);
    auto* remote = dynamic_cast<RemoteObservable<MockClient, MockClient, waos::db_scan_query>*>(&obs);
    expect(remote != nullptr);
    waos::stream_item item{remote->streamId(), bin("a"), bin("1"), 42};
    remote->onStreamItem(item);
    waos::stream_done done{remote->streamId()};
    remote->onStreamDone(done);
    expect(eq(obs.maxTs(), std::uint64_t{42}));
    out->unsubscribe();
    expect(eq(obs.maxTs(), std::uint64_t{42}));

    auto c2 = std::make_shared<RecordingConsumer>();
    obs.subscribe(c2);
    expect(eq(client.starts.size(), std::size_t{2}));
    expect(eq(client.starts[0].handle.since_timestamp, std::uint64_t{0}));
    expect(eq(client.starts[0].handle.flags, static_cast<std::uint8_t>(waos::stream_flags::observe)));
    expect(eq(client.starts[1].handle.since_timestamp, std::uint64_t{42}));
    expect(client.starts[1].handle.since_timestamp != std::uint64_t{0});
  };
}
