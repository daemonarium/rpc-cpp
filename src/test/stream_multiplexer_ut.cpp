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
#include "observable/stream_multiplexer.hpp"
#include "observable/stream_receiver.hpp"
#include "serialization/promise.hpp"

using namespace boost::ut;
using namespace waos::observable;

namespace {

  constexpr std::uint64_t kDbStartScan = 5;

  struct MockStarter {
    struct StartCall {
      std::uint64_t function_id = 0;
      waos::stream_handle handle;
      waos::db_scan_query query;
    };
    std::vector<StartCall> starts;

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

  struct MockChannel {
    std::unordered_map<std::type_index, std::function<void(void*)>> handlers;
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

    template <typename T>
    void inject(T msg) {
      auto it = handlers.find(std::type_index(typeid(T)));
      if(it == handlers.end()) return;
      it->second(&msg);
    }
  };

  struct MockClient : MockStarter, MockChannel {};

  struct RecordingReceiver : StreamReceiver {
    int opened = 0;
    std::vector<waos::stream_item> items;
    std::vector<waos::stream_done> dones;
    std::vector<waos::stream_reset> resets;
    std::vector<waos::stream_error> errors;

    std::vector<std::uint64_t> openedIds;

    void onStreamOpened(waos::stream_opened& m) override {
      ++opened;
      openedIds.push_back(m.stream_id);
    }
    void onStreamItem(waos::stream_item& m) override { items.push_back(m); }
    void onStreamDone(waos::stream_done& m) override { dones.push_back(m); }
    void onStreamReset(waos::stream_reset& m) override { resets.push_back(m); }
    void onStreamError(waos::stream_error& m) override { errors.push_back(m); }
  };

  waos::binary bin(std::string_view s) {
    return waos::binary(s.begin(), s.end());
  }

}

int main() {
  "createStream returns sequential ids and registers receivers"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RecordingReceiver r1;
    RecordingReceiver r2;
    RecordingReceiver r3;
    expect(eq(mux.createStream(kDbStartScan, 0, &r1, waos::db_scan_query{}), std::uint64_t{1}));
    expect(eq(mux.createStream(kDbStartScan, 0, &r2, waos::db_scan_query{}), std::uint64_t{2}));
    expect(eq(mux.createStream(kDbStartScan, 0, &r3, waos::db_scan_query{}), std::uint64_t{3}));
    expect(eq(mux.size(), std::size_t{3}));
    expect(eq(client.starts.size(), std::size_t{3}));
    expect(eq(client.starts[0].function_id, kDbStartScan));
    expect(eq(client.starts[0].handle.stream_id, std::uint64_t{1}));
  };

  "dispatch routes item done reset error opened to matching receiver"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RecordingReceiver r1;
    RecordingReceiver r2;
    auto id1 = mux.createStream(kDbStartScan, 0, &r1, waos::db_scan_query{});
    auto id2 = mux.createStream(kDbStartScan, 0, &r2, waos::db_scan_query{});

    client.inject(waos::stream_item{id1, bin("k"), bin("v"), 7});
    client.inject(waos::stream_done{id1});
    client.inject(waos::stream_reset{id2});
    client.inject(waos::stream_error{id2, "boom"});
    client.inject(waos::stream_opened{id1});

    expect(eq(r1.items.size(), std::size_t{1}));
    expect(r1.items[0].key == bin("k"));
    expect(eq(r1.items[0].timestamp, std::uint64_t{7}));
    expect(eq(r1.dones.size(), std::size_t{1}));
    expect(eq(r1.resets.size(), std::size_t{0}));
    expect(eq(r1.errors.size(), std::size_t{0}));
    expect(eq(r2.items.size(), std::size_t{0}));
    expect(eq(r2.resets.size(), std::size_t{1}));
    expect(eq(r2.errors.size(), std::size_t{1}));
    expect(r2.errors[0].message == std::string("boom"));
    expect(eq(r1.opened, 1));
    expect(eq(r1.openedIds[0], id1));
    expect(eq(r2.opened, 0));
  };

  "stream_opened goes only to the matching id"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RecordingReceiver r1;
    RecordingReceiver r2;
    auto id1 = mux.createStream(kDbStartScan, 0, &r1, waos::db_scan_query{});
    auto id2 = mux.createStream(kDbStartScan, 0, &r2, waos::db_scan_query{});
    client.inject(waos::stream_opened{id2});
    expect(eq(r1.opened, 0));
    expect(eq(r2.opened, 1));
    expect(eq(r2.openedIds[0], id2));
    (void)id1;
  };

  "cancel before opened sends stream_cancel again when opened arrives"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RecordingReceiver r1;
    auto id = mux.createStream(kDbStartScan, 0, &r1, waos::db_scan_query{});
    mux.cancelStream(id);
    expect(eq(client.cancels.size(), std::size_t{1}));
    client.inject(waos::stream_opened{id});
    expect(eq(client.cancels.size(), std::size_t{2}));
    expect(eq(client.cancels[1].stream_id, id));
    expect(eq(r1.opened, 0));
  };

  "socket_opened is ignored"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RecordingReceiver r1;
    mux.createStream(kDbStartScan, 0, &r1, waos::db_scan_query{});
    client.inject(waos::socket_opened{});
    expect(eq(r1.opened, 0));
  };

  "unknown id after cancel is silently dropped"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RecordingReceiver r1;
    auto id = mux.createStream(kDbStartScan, 0, &r1, waos::db_scan_query{});
    mux.cancelStream(id);
    expect(eq(mux.size(), std::size_t{0}));
    expect(eq(client.cancels.size(), std::size_t{1}));
    expect(eq(client.cancels[0].stream_id, id));
    expect(eq(client.starts.size(), std::size_t{1}));

    client.inject(waos::stream_item{id, bin("k"), bin("v"), 1});
    client.inject(waos::stream_done{id});
    client.inject(waos::stream_reset{id});
    client.inject(waos::stream_error{id, "late"});
    expect(eq(r1.items.size(), std::size_t{0}));
    expect(eq(r1.dones.size(), std::size_t{0}));
    expect(eq(r1.resets.size(), std::size_t{0}));
    expect(eq(r1.errors.size(), std::size_t{0}));
  };

  "cancelStream removes receiver subsequent dispatch is no-op"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RecordingReceiver r1;
    auto id = mux.createStream(kDbStartScan, 0, &r1, waos::db_scan_query{});
    client.inject(waos::stream_item{id, bin("k"), bin("v"), 1});
    expect(eq(r1.items.size(), std::size_t{1}));
    mux.cancelStream(id);
    client.inject(waos::stream_item{id, bin("x"), bin("y"), 2});
    expect(eq(r1.items.size(), std::size_t{1}));
  };

  "concurrent streams are demuxed correctly"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RecordingReceiver r1;
    RecordingReceiver r2;
    auto id1 = mux.createStream(kDbStartScan, 0, &r1, waos::db_scan_query{});
    auto id2 = mux.createStream(kDbStartScan, 0, &r2, waos::db_scan_query{});

    client.inject(waos::stream_item{id1, bin("a"), bin("1"), 1});
    client.inject(waos::stream_item{id2, bin("b"), bin("2"), 2});
    client.inject(waos::stream_item{id1, bin("c"), bin("3"), 3});
    client.inject(waos::stream_done{id2});
    client.inject(waos::stream_done{id1});

    expect(eq(r1.items.size(), std::size_t{2}));
    expect(r1.items[0].key == bin("a"));
    expect(r1.items[1].key == bin("c"));
    expect(eq(r2.items.size(), std::size_t{1}));
    expect(r2.items[0].key == bin("b"));
    expect(eq(r1.dones.size(), std::size_t{1}));
    expect(eq(r2.dones.size(), std::size_t{1}));
  };

  "createStream copies query and muxer pid socket onto the start call"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 9, 4);
    RecordingReceiver r1;
    waos::db_scan_query q;
    q.limit = 10;
    q.reverse = true;
    mux.createStream(kDbStartScan, 42, &r1, q);
    expect(eq(client.starts.size(), std::size_t{1}));
    expect(eq(client.starts[0].function_id, kDbStartScan));
    expect(eq(client.starts[0].handle.pid, std::uint64_t{9}));
    expect(eq(client.starts[0].handle.socket, std::uint32_t{4}));
    expect(eq(client.starts[0].handle.since_timestamp, std::uint64_t{42}));
    expect(eq(client.starts[0].handle.flags, static_cast<std::uint8_t>(waos::stream_flags::none)));
    expect(eq(client.starts[0].query.limit, std::uint64_t{10}));
    expect(client.starts[0].query.reverse);
  };

  "cancelStream sends stream_cancel not an RPC"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RecordingReceiver r1;
    auto id = mux.createStream(kDbStartScan, 0, &r1, waos::db_scan_query{});
    mux.cancelStream(id);
    expect(eq(client.cancels.size(), std::size_t{1}));
    expect(eq(client.cancels[0].stream_id, id));
    expect(eq(client.starts.size(), std::size_t{1}));
    expect(eq(client.starts[0].function_id, kDbStartScan));
  };

  "two-role starter and channel are distinct"_test = [] {
    MockStarter starter;
    MockChannel channel;
    StreamMultiplexer mux(starter, channel, 7, 3);
    RecordingReceiver r;
    auto id = mux.createStream(11, 5, &r, waos::db_scan_query{});
    expect(eq(starter.starts.size(), std::size_t{1}));
    expect(eq(starter.starts[0].function_id, std::uint64_t{11}));
    expect(eq(starter.starts[0].handle.pid, std::uint64_t{7}));
    expect(eq(starter.starts[0].handle.socket, std::uint32_t{3}));
    expect(eq(starter.starts[0].handle.since_timestamp, std::uint64_t{5}));
    expect(eq(starter.starts[0].handle.flags, static_cast<std::uint8_t>(waos::stream_flags::none)));
    expect(eq(channel.cancels.size(), std::size_t{0}));

    channel.inject(waos::stream_item{id, bin("k"), bin("v"), 1});
    expect(eq(r.items.size(), std::size_t{1}));

    mux.cancelStream(id);
    expect(eq(channel.cancels.size(), std::size_t{1}));
    expect(eq(channel.cancels[0].stream_id, id));
    expect(eq(starter.starts.size(), std::size_t{1}));
  };

  "one-client starter equals channel"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 2, 8);
    RecordingReceiver r;
    auto id = mux.createStream(kDbStartScan, 0, &r, waos::db_scan_query{});
    expect(eq(client.starts.size(), std::size_t{1}));
    expect(eq(client.starts[0].handle.pid, std::uint64_t{2}));
    expect(eq(client.starts[0].handle.socket, std::uint32_t{8}));
    mux.cancelStream(id);
    expect(eq(client.cancels.size(), std::size_t{1}));
    expect(eq(client.cancels[0].stream_id, id));
  };

  "createStream observe flag is copied onto stream_handle"_test = [] {
    MockClient client;
    StreamMultiplexer mux(client, 1, 1);
    RecordingReceiver r;
    mux.createStream(kDbStartScan, 0, waos::stream_flags::observe, &r, waos::db_scan_query{});
    expect(eq(client.starts[0].handle.flags, static_cast<std::uint8_t>(waos::stream_flags::observe)));
    expect(eq(client.starts[0].handle.since_timestamp, std::uint64_t{0}));
  };
}
