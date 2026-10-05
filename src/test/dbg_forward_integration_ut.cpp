#include <algorithm>
#include <boost/ut.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "examples/structures/dbg_forward.hpp"
#include "examples/structures/db.hpp"
#include "observable/observable.hpp"
#include "observable/remote_observable.hpp"
#include "observable/remote_registry.hpp"
#include "observable/stream_multiplexer.hpp"
#include "serialization/annotations.hpp"
#include "serialization/flat_materialized_codec.hpp"
#include "serialization/promise.hpp"
#include "serialization/socket_rpc_client.hpp"
#include "serialization/socket_rpc_server.hpp"

extern "C" {
#include <socket/socket.h>
#include <io/circular_buffer.h>
}

using namespace boost::ut;
using namespace waos::observable;

namespace {

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

  struct four_sockets {
    uint32_t fwd = 0;
    uint32_t client = 0;
    uint32_t db = 0;
    uint32_t dbc = 0;

    four_sockets(size_t buf_size = 8192) {
      sockets_init(4);
      struct socket_options opts = {};
      opts.input_buffer_size = buf_size;
      opts.output_buffer_size = buf_size;
      opts.flush_threshold = buf_size;
      opts.data_event_threshold = buf_size;
      opts.drain_event_threshold = buf_size;
      fwd = socket_open(0, &opts);
      client = socket_open(1, &opts);
      db = socket_open(2, &opts);
      dbc = socket_open(3, &opts);
    }

    ~four_sockets() {
      DbgForward::unbind_upstream();
      socket_close(fwd);
      socket_close(client);
      socket_close(db);
      socket_close(dbc);
      sockets_done();
    }

    static void transfer(uint32_t from, uint32_t to) {
      struct socket_info* src = get_socket_info(from);
      struct socket_info* dst = get_socket_info(to);
      uint8_t tmp[4096];
      for(;;) {
        size_t n = circular_buffer_read(&src->output_buffer, tmp, sizeof(tmp));
        if(n == 0) break;
        size_t off = 0;
        while(off < n) {
          size_t m = circular_buffer_write(&dst->input_buffer, tmp + off, n - off);
          off += m;
        }
      }
    }
  };

  struct MockDbState {
    flat_socket_rpc_server* server = nullptr;
    std::vector<BinaryEntry> store;
    std::vector<std::uint64_t> live;
    int table_calls = 0;
    int cancels = 0;
    std::uint64_t last_since = 0;
    std::uint8_t last_flags = 0;
  };
  MockDbState g_db;

  class MockDb {
    public:
      [[=serialization::observable()]]
      static std::shared_ptr<serialization::promise<void>> table(
          waos::stream_handle stream,
          std::uint32_t /*table_id*/,
          waos::binary /*lower_id*/,
          waos::db_bound_mode /*lower_mode*/,
          waos::binary /*upper_id*/,
          waos::db_bound_mode /*upper_mode*/,
          std::uint64_t /*limit*/,
          bool /*reverse*/,
          bool /*keys_only*/) {
        ++g_db.table_calls;
        g_db.last_since = stream.since_timestamp;
        g_db.last_flags = stream.flags;
        if(!g_db.server) {
          throw std::runtime_error("MockDb server is not set");
        }
        g_db.server->send_message(waos::stream_opened{stream.stream_id});
        for(auto const& e : g_db.store) {
          if(e.ts > stream.since_timestamp) {
            g_db.server->send_message(waos::stream_item{
              stream.stream_id, e.key, e.value, e.ts});
          }
        }
        g_db.server->send_message(waos::stream_done{stream.stream_id});
        if(stream.flags & static_cast<std::uint8_t>(waos::stream_flags::observe)) {
          g_db.live.push_back(stream.stream_id);
        }
        return serialization::promise<void>::resolved();
      }
  };

  void db_put(binary key, binary value, std::uint64_t ts) {
    g_db.store.push_back(BinaryEntry{key, value, ts});
    if(!g_db.server) return;
    for(std::uint64_t id : g_db.live) {
      g_db.server->send_message(waos::stream_item{id, key, value, ts});
    }
  }

  void db_on_cancel(std::uint64_t stream_id) {
    ++g_db.cancels;
    g_db.live.erase(std::remove(g_db.live.begin(), g_db.live.end(), stream_id), g_db.live.end());
  }

  void pump(four_sockets& s,
            flat_socket_rpc_server& fwd,
            flat_socket_rpc_client& client,
            flat_socket_rpc_server& db,
            flat_socket_rpc_client& dbc) {
    four_sockets::transfer(s.client, s.fwd);
    fwd.handle_data();
    four_sockets::transfer(s.fwd, s.client);
    client.handle_data();
    four_sockets::transfer(s.dbc, s.db);
    db.handle_data();
    four_sockets::transfer(s.db, s.dbc);
    dbc.handle_data();
  }

  void pump_n(four_sockets& s,
              flat_socket_rpc_server& fwd,
              flat_socket_rpc_client& client,
              flat_socket_rpc_server& db,
              flat_socket_rpc_client& dbc,
              int n = 6) {
    for(int i = 0; i < n; ++i) {
      pump(s, fwd, client, db, dbc);
    }
  }

}

int main() {
  "db put notifies client through dbg_forward"_test = [] {
    g_db = MockDbState{};
    four_sockets s;
    flat_socket_rpc_client client(s.client);
    flat_socket_rpc_client dbc(s.dbc);
    StreamMultiplexer db_mux(dbc, 1, s.dbc);
    StreamMultiplexer client_mux(client, 1, s.client);
    DbgForward::bind_upstream(db_mux, 0);

    flat_socket_rpc_server fwd(s.fwd);
    flat_socket_rpc_server db(s.db);

    fwd.register_basic_types();
    client.register_basic_types();
    db.register_basic_types();
    dbc.register_basic_types();
    fwd.register_stream_message_types();
    register_stream_types(client);
    db.register_stream_message_types();
    register_stream_types(dbc);

    g_db.server = &db;
    MockDb mock;
    db.register_recursive(mock);
    db.on_message<waos::stream_cancel>(std::function<void(waos::stream_cancel&)>(
      [](waos::stream_cancel& m) { db_on_cancel(m.stream_id); }));

    DbgForward api;
    fwd.register_recursive(api);

    RemoteRegistry registry(client_mux);
    auto ref = registry.getOrCreate(
      0, std::uint32_t{1},
      waos::binary{}, waos::db_bound_mode::none,
      waos::binary{}, waos::db_bound_mode::none,
      std::uint64_t{0}, false, false);

    auto& live = ref->observable();
    auto c = std::make_shared<RecordingConsumer>();
    auto out = live.subscribe(c);
    pump_n(s, fwd, client, db, dbc);

    expect(eq(g_db.table_calls, 1));
    expect(eq(g_db.last_flags, static_cast<std::uint8_t>(waos::stream_flags::observe)));
    expect(eq(c->readys, 1));
    expect(live.state() == ObservableState::ready);

    db_put(bin("k"), bin("v"), 7);
    pump_n(s, fwd, client, db, dbc);
    expect(eq(c->changes.size(), std::size_t{1}));
    expect(c->changes[0].key == bin("k"));
    expect(c->changes[0].value == bin("v"));
    expect(eq(c->changes[0].ts, std::uint64_t{7}));
    expect(eq(live.maxTs(), std::uint64_t{7}));

    out->unsubscribe();
    pump_n(s, fwd, client, db, dbc);
    expect(eq(g_db.cancels, 1));
    expect(live.state() == ObservableState::idle);
    expect(g_db.live.empty());

    auto c2 = std::make_shared<RecordingConsumer>();
    c2->since = live.maxTs();
    live.subscribe(c2);
    pump_n(s, fwd, client, db, dbc);
    expect(eq(g_db.table_calls, 2));
    expect(eq(g_db.last_since, std::uint64_t{7}));
    expect(c2->changes.empty());
    expect(eq(c2->readys, 1));

    db_put(bin("k2"), bin("v2"), 8);
    pump_n(s, fwd, client, db, dbc);
    expect(eq(c2->changes.size(), std::size_t{1}));
    expect(c2->changes[0].key == bin("k2"));
    expect(eq(c2->changes[0].ts, std::uint64_t{8}));

    DbgForward::unbind_upstream();
    g_db.server = nullptr;
  };
}
