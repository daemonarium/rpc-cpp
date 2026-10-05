#include <boost/ut.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "observable/observable.hpp"
#include "observable/remote_observable.hpp"
#include "observable/remote_registry.hpp"
#include "observable/stream_multiplexer.hpp"
#include "serialization/annotations.hpp"
#include "serialization/flat_materialized_codec.hpp"
#include "serialization/promise.hpp"
#include "serialization/socket_rpc_client.hpp"
#include "serialization/socket_rpc_server.hpp"
#include "test_socket_utils.hpp"

using namespace boost::ut;
using namespace waos::observable;

namespace {

  binary bin(std::string_view s) {
    return binary(s.begin(), s.end());
  }

  struct ManualSource : Observable<BinaryEntry> {
    int starts = 0;
    int stops = 0;

    void onStart() override { ++starts; }
    void onStop() override { ++stops; }

    void push(binary k, binary v, std::uint64_t ts) {
      onItem(BinaryEntry{std::move(k), std::move(v), ts});
    }
    void done() { onDone(); }
  };

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

  class HighLevelService {
    public:
      inline static std::shared_ptr<ManualSource> last;
      inline static int calls = 0;

      [[=serialization::observable()]]
      static std::shared_ptr<Observable<BinaryEntry>> watch() {
        ++calls;
        last = std::make_shared<ManualSource>();
        return last;
      }
  };

  using flat_socket_rpc_server = serialization::socket_rpc_server<serialization::flat_materialized_codec>;
  using flat_socket_rpc_client = serialization::socket_rpc_client<serialization::flat_materialized_codec>;

  template <typename Side>
  void register_stream_types(Side& s) {
    s.template register_message_type<waos::socket_opened>();
    s.template register_message_type<waos::stream_item>();
    s.template register_message_type<waos::stream_key>();
    s.template register_message_type<waos::stream_done>();
    s.template register_message_type<waos::stream_error>();
    s.template register_message_type<waos::stream_reset>();
    s.template register_message_type<waos::stream_cancel>();
    s.template register_message_type<waos::stream_opened>();
  }

  void rpc_roundtrip(socket_loopback_pair& sockets,
                     flat_socket_rpc_client& client,
                     flat_socket_rpc_server& server) {
    sockets.transfer(sockets.s1, sockets.s0);
    server.handle_data();
    sockets.transfer(sockets.s0, sockets.s1);
    client.handle_data();
  }

  void server_to_client(socket_loopback_pair& sockets,
                        flat_socket_rpc_client& client) {
    sockets.transfer(sockets.s0, sockets.s1);
    client.handle_data();
  }

}

int main() {
  "flags=observe attaches live relay and items flow"_test = [] {
    HighLevelService::calls = 0;
    HighLevelService::last.reset();

    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);
    server.register_basic_types();
    client.register_basic_types();
    server.register_stream_message_types();
    register_stream_types(client);
    HighLevelService svc;
    server.register_recursive(svc);

    StreamMultiplexer mux(client, 1, 1);
    RemoteObservable obs(mux, 0);
    obs.withFlags(waos::stream_flags::observe);
    auto c = std::make_shared<RecordingConsumer>();
    auto out = obs.subscribe(c);
    rpc_roundtrip(sockets, client, server);

    expect(eq(HighLevelService::calls, 1));
    expect(HighLevelService::last != nullptr);
    expect(eq(HighLevelService::last->starts, 1));
    expect(server.stream_publisher().isAttached(obs.streamId()));

    HighLevelService::last->push(bin("a"), bin("1"), 10);
    HighLevelService::last->done();
    server_to_client(sockets, client);

    expect(eq(c->changes.size(), std::size_t{1}));
    expect(c->changes[0].key == bin("a"));
    expect(eq(c->changes[0].ts, std::uint64_t{10}));
    expect(eq(c->readys, 1));
    expect(obs.state() == ObservableState::ready);

    HighLevelService::last->push(bin("b"), bin("2"), 11);
    server_to_client(sockets, client);
    expect(eq(c->changes.size(), std::size_t{2}));
    expect(c->changes[1].key == bin("b"));
    expect(server.stream_publisher().isAttached(obs.streamId()));

    out->unsubscribe();
    rpc_roundtrip(sockets, client, server);
    expect(eq(HighLevelService::last->stops, 1));
    expect(HighLevelService::last->state() == ObservableState::idle);
    expect(server.stream_publisher().isAttached(obs.streamId()) == false);
  };

  "flags=none attach_oneshot then stream_done unsubscribes"_test = [] {
    HighLevelService::calls = 0;
    HighLevelService::last.reset();

    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);
    server.register_basic_types();
    client.register_basic_types();
    server.register_stream_message_types();
    register_stream_types(client);
    HighLevelService svc;
    server.register_recursive(svc);

    StreamMultiplexer mux(client, 1, 1);
    RemoteObservable obs(mux, 0);
    auto c = std::make_shared<RecordingConsumer>();
    obs.subscribe(c);
    rpc_roundtrip(sockets, client, server);

    expect(eq(HighLevelService::calls, 1));
    HighLevelService::last->push(bin("k"), bin("v"), 3);
    HighLevelService::last->done();
    server_to_client(sockets, client);

    expect(eq(c->changes.size(), std::size_t{1}));
    expect(eq(c->readys, 1));
    expect(obs.state() == ObservableState::ready);
    expect(server.stream_publisher().isAttached(obs.streamId()) == false);
    expect(HighLevelService::last->state() == ObservableState::idle);
    expect(eq(HighLevelService::last->stops, 1));

    HighLevelService::last->push(bin("z"), bin("9"), 4);
    server_to_client(sockets, client);
    expect(eq(c->changes.size(), std::size_t{1}));
  };

  "stream_cancel detaches publisher and idles source"_test = [] {
    HighLevelService::calls = 0;
    HighLevelService::last.reset();

    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);
    server.register_basic_types();
    client.register_basic_types();
    server.register_stream_message_types();
    register_stream_types(client);
    HighLevelService svc;
    server.register_recursive(svc);

    StreamMultiplexer mux(client, 1, 1);
    RemoteObservable obs(mux, 0);
    obs.withFlags(waos::stream_flags::observe);
    auto c = std::make_shared<RecordingConsumer>();
    auto out = obs.subscribe(c);
    rpc_roundtrip(sockets, client, server);
    expect(server.stream_publisher().isAttached(obs.streamId()));

    out->unsubscribe();
    rpc_roundtrip(sockets, client, server);
    expect(HighLevelService::last->state() == ObservableState::idle);
    expect(eq(HighLevelService::last->stops, 1));
    expect(server.stream_publisher().size() == std::size_t{0});
  };

  "RemoteReference.observable dedups two subscribers to one upstream RPC"_test = [] {
    HighLevelService::calls = 0;
    HighLevelService::last.reset();

    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);
    server.register_basic_types();
    client.register_basic_types();
    server.register_stream_message_types();
    register_stream_types(client);
    HighLevelService svc;
    server.register_recursive(svc);

    StreamMultiplexer mux(client, 1, 1);
    RemoteRegistry registry(mux);
    auto ref = registry.getOrCreate(0);
    auto& a = ref->observable();
    auto& b = ref->observable();
    expect(&a == &b);

    auto c1 = std::make_shared<RecordingConsumer>();
    auto c2 = std::make_shared<RecordingConsumer>();
    a.subscribe(c1);
    rpc_roundtrip(sockets, client, server);
    expect(eq(HighLevelService::calls, 1));
    b.subscribe(c2);
    rpc_roundtrip(sockets, client, server);
    expect(eq(HighLevelService::calls, 1));
    expect(eq(a.activeCount(), std::size_t{2}));
    expect(eq(HighLevelService::last->starts, 1));
  };
}
