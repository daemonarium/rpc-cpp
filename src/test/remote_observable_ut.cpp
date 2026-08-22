#include <boost/ut.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
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
#include "serialization/flat_materialized_codec.hpp"
#include "serialization/promise.hpp"
#include "serialization/socket_rpc_client.hpp"
#include "serialization/socket_rpc_server.hpp"
#include "test_socket_utils.hpp"

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

    template <typename T>
    void inject(T msg) {
      auto it = handlers.find(std::type_index(typeid(T)));
      if(it == handlers.end()) return;
      it->second(&msg);
    }
  };

  struct ScanService {
    inline static waos::stream_handle last_handle{};
    inline static int starts = 0;
    inline static int cancels = 0;
    inline static std::uint64_t last_cancel = 0;

    static void get() {}
    static void set() {}
    static void batch() {}
    static void open_socket() {}
    static void close_socket() {}
    static void start_scan(waos::stream_handle stream, waos::db_scan_query) {
      last_handle = stream;
      ++starts;
    }
  };

  using flat_socket_rpc_server = serialization::socket_rpc_server<serialization::flat_materialized_codec>;
  using flat_socket_rpc_client = serialization::socket_rpc_client<serialization::flat_materialized_codec>;

  template <typename Side>
  void register_stream_types(Side& s) {
    s.template register_message_type<waos::stream_opened>();
    s.template register_message_type<waos::stream_item>();
    s.template register_message_type<waos::stream_key>();
    s.template register_message_type<waos::stream_done>();
    s.template register_message_type<waos::stream_error>();
    s.template register_message_type<waos::stream_reset>();
    s.template register_message_type<waos::stream_cancel>();
  }

  void rpc_roundtrip(socket_loopback_pair& sockets,
                     flat_socket_rpc_client& client,
                     flat_socket_rpc_server& server) {
    sockets.transfer(sockets.s1, sockets.s0);
    server.handle_data();
    sockets.transfer(sockets.s0, sockets.s1);
    client.handle_data();
  }

}

int main() {
  "1 subscribe triggers onStart createStream once"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 2);
    RemoteObservable obs(mux, kDbStartScan, waos::db_scan_query{});
    auto c = std::make_shared<RecordingConsumer>();
    expect(obs.state() == ObservableState::idle);
    obs.subscribe(c);
    expect(eq(client.starts.size(), std::size_t{1}));
    expect(eq(client.starts[0].function_id, kDbStartScan));
    expect(eq(client.starts[0].handle.pid, std::uint64_t{1}));
    expect(eq(client.starts[0].handle.socket, std::uint32_t{2}));
    expect(eq(client.starts[0].handle.since_timestamp, std::uint64_t{0}));
    expect(eq(obs.streamId(), std::uint64_t{1}));
    expect(obs.state() == ObservableState::loading);
  };

  "2 onStreamItem drives onItem then onStreamDone to ready"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 2);
    RemoteObservable obs(mux, kDbStartScan, waos::db_scan_query{});
    auto c = std::make_shared<RecordingConsumer>();
    obs.subscribe(c);
    waos::stream_item item{obs.streamId(), bin("a"), bin("1"), 10};
    obs.onStreamItem(item);
    expect(eq(c->changes.size(), std::size_t{1}));
    expect(c->changes[0].key == bin("a"));
    expect(eq(c->changes[0].ts, std::uint64_t{10}));
    expect(obs.state() == ObservableState::loading);
    waos::stream_done done{obs.streamId()};
    obs.onStreamDone(done);
    expect(obs.state() == ObservableState::ready);
    expect(eq(c->readys, 1));
    expect(eq(obs.getEntries().size(), std::size_t{1}));
  };

  "3 onStreamReset clear-and-rebuild sets-only replay"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 2);
    RemoteObservable obs(mux, kDbStartScan, waos::db_scan_query{});
    auto c = std::make_shared<RecordingConsumer>();
    obs.subscribe(c);
    waos::stream_item a{obs.streamId(), bin("a"), bin("1"), 1};
    obs.onStreamItem(a);
    waos::stream_done d1{obs.streamId()};
    obs.onStreamDone(d1);
    waos::stream_reset rst{obs.streamId()};
    obs.onStreamReset(rst);
    expect(obs.state() == ObservableState::reloading);
    expect(eq(c->resets, 1));
    expect(eq(c->changes.size(), std::size_t{0}));
    expect(eq(obs.getEntries().size(), std::size_t{1}));
    expect(obs.getEntries()[0].key == bin("a"));
    waos::stream_item nb{obs.streamId(), bin("b"), bin("2"), 2};
    obs.onStreamItem(nb);
    waos::stream_done d2{obs.streamId()};
    obs.onStreamDone(d2);
    expect(obs.state() == ObservableState::ready);
    expect(eq(obs.getEntries().size(), std::size_t{1}));
    expect(obs.getEntries()[0].key == bin("b"));
    expect(eq(c->changes.size(), std::size_t{1}));
    expect(c->changes[0].key == bin("b"));
  };

  "4 onStreamError to error lastError retained"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 2);
    RemoteObservable obs(mux, kDbStartScan, waos::db_scan_query{});
    auto c = std::make_shared<RecordingConsumer>();
    obs.subscribe(c);
    waos::stream_item a{obs.streamId(), bin("a"), bin("1"), 1};
    obs.onStreamItem(a);
    waos::stream_done d{obs.streamId()};
    obs.onStreamDone(d);
    waos::stream_error err{obs.streamId(), "wire boom"};
    obs.onStreamError(err);
    expect(obs.state() == ObservableState::error);
    expect(eq(c->errors, 1));
    expect(obs.lastError() != nullptr);
    expect(eq(obs.getEntries().size(), std::size_t{1}));
    try {
      std::rethrow_exception(obs.lastError());
    } catch(const waos::db_io_error& e) {
      expect(std::string(e.what()) == std::string("wire boom"));
    } catch(...) {
      expect(false);
    }
  };

  "5 last unsubscribe onStop cancelStream idle"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 2);
    RemoteObservable obs(mux, kDbStartScan, waos::db_scan_query{});
    auto c = std::make_shared<RecordingConsumer>();
    auto out = obs.subscribe(c);
    expect(eq(client.starts.size(), std::size_t{1}));
    auto id = obs.streamId();
    out->unsubscribe();
    expect(obs.state() == ObservableState::idle);
    expect(eq(client.cancels.size(), std::size_t{1}));
    expect(eq(client.cancels[0].stream_id, id));
    expect(eq(obs.streamId(), std::uint64_t{0}));
  };

  "6 resubscribe after idle since_timestamp is maxTs"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 2);
    RemoteObservable obs(mux, kDbStartScan, waos::db_scan_query{});
    auto c = std::make_shared<RecordingConsumer>();
    auto out = obs.subscribe(c);
    waos::stream_item a{obs.streamId(), bin("a"), bin("1"), 10};
    obs.onStreamItem(a);
    waos::stream_done d{obs.streamId()};
    obs.onStreamDone(d);
    expect(eq(obs.maxTs(), std::uint64_t{10}));
    out->unsubscribe();
    auto c2 = std::make_shared<RecordingConsumer>();
    obs.subscribe(c2);
    expect(eq(client.starts.size(), std::size_t{2}));
    expect(eq(client.starts[1].handle.since_timestamp, std::uint64_t{10}));
    expect(obs.state() == ObservableState::loading);
  };

  "7 onStreamReset after error is reloading"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 2);
    RemoteObservable obs(mux, kDbStartScan, waos::db_scan_query{});
    auto c = std::make_shared<RecordingConsumer>();
    obs.subscribe(c);
    waos::stream_error err{obs.streamId(), "fail"};
    obs.onStreamError(err);
    expect(obs.state() == ObservableState::error);
    waos::stream_reset rst{obs.streamId()};
    obs.onStreamReset(rst);
    expect(obs.state() == ObservableState::reloading);
    expect(eq(c->resets, 1));
  };

  "loopback start_scan then stream_item stream_done stream_cancel"_test = [] {
    ScanService::starts = 0;
    ScanService::cancels = 0;
    ScanService::last_cancel = 0;
    ScanService::last_handle = {};

    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);

    server.register_basic_types();
    client.register_basic_types();
    register_stream_types(server);
    register_stream_types(client);

    ScanService svc;
    server.register_recursive(svc);
    server.on_message<waos::stream_cancel>(std::function<void(waos::stream_cancel&)>(
      [](waos::stream_cancel& m) {
        ScanService::last_cancel = m.stream_id;
        ++ScanService::cancels;
      }));

    StreamMultiplexer mux(client, 1, 1);
    RemoteObservable obs(mux, kDbStartScan, waos::db_scan_query{});
    auto c = std::make_shared<RecordingConsumer>();
    auto out = obs.subscribe(c);
    expect(obs.state() == ObservableState::loading);

    rpc_roundtrip(sockets, client, server);
    expect(eq(ScanService::starts, 1));
    expect(eq(ScanService::last_handle.stream_id, obs.streamId()));

    server.send_message(waos::stream_item{
      ScanService::last_handle.stream_id, bin("a"), bin("1"), 5});
    server.send_message(waos::stream_item{
      ScanService::last_handle.stream_id, bin("b"), bin("2"), 6});
    server.send_message(waos::stream_done{ScanService::last_handle.stream_id});
    sockets.transfer(sockets.s0, sockets.s1);
    client.handle_data();

    expect(eq(c->changes.size(), std::size_t{2}));
    expect(c->changes[0].key == bin("a"));
    expect(c->changes[1].key == bin("b"));
    expect(eq(c->readys, 1));
    expect(obs.state() == ObservableState::ready);
    expect(eq(obs.getEntries().size(), std::size_t{2}));

    auto id = obs.streamId();
    out->unsubscribe();
    rpc_roundtrip(sockets, client, server);
    expect(eq(ScanService::cancels, 1));
    expect(eq(ScanService::last_cancel, id));
    expect(obs.state() == ObservableState::idle);
  };

  "registry same key reuses observable across idle with delta resume"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RemoteRegistry registry(mux);
    waos::db_scan_query q;
    auto a = registry.getOrCreate(kDbStartScan, q);
    auto b = registry.getOrCreate(kDbStartScan, q);
    expect(a.get() == b.get());

    waos::db_scan_query other;
    other.limit = 10;
    auto d = registry.getOrCreate(kDbStartScan, other);
    expect(a.get() != d.get());
    expect(eq(registry.size(), std::size_t{2}));

    auto c = std::make_shared<RecordingConsumer>();
    auto out = a->subscribe(c);
    waos::stream_item item{a->streamId(), bin("a"), bin("1"), 10};
    a->onStreamItem(item);
    waos::stream_done done{a->streamId()};
    a->onStreamDone(done);
    expect(eq(a->maxTs(), std::uint64_t{10}));
    out->unsubscribe();
    expect(a->state() == ObservableState::idle);
    expect(eq(registry.size(), std::size_t{2}));

    auto c2 = std::make_shared<RecordingConsumer>();
    a->subscribe(c2);
    expect(eq(client.starts.size(), std::size_t{2}));
    expect(eq(client.starts[1].handle.since_timestamp, std::uint64_t{10}));
    expect(a.get() == registry.getOrCreate(kDbStartScan, q).get());
  };
}
