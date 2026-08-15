#include "../structures/db_bench_client.hpp"
#include "../structures/db.hpp"
#include <utils/module.hpp>
#include <serialization/socket_rpc_client.hpp>
#include <utils/socket_handlers.hpp>
#include <socket/socket.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <string>

DbBenchClient db_bench_client;

namespace {

  constexpr std::uint64_t kDbGet = 0;
  constexpr std::uint64_t kDbOpenSocket = 3;
  constexpr std::uint64_t kDbStartScan = 5;

  // Must match native-host/bench/db-wasm.mts WASM_ID / private socket.
  constexpr std::uint64_t kClientPid = 2;
  constexpr std::uint32_t kDbControlSock = 1;
  constexpr std::uint32_t kDbPrivateSock = 2;
  constexpr std::size_t kBuf = 65536;

  std::unique_ptr<serialization::flat_socket_rpc_client> db_control;
  std::unique_ptr<serialization::flat_socket_rpc_client> db_private;
  std::uint64_t scan_items = 0;
  std::uint64_t scan_dones = 0;
  std::shared_ptr<serialization::promise<void>> scan_done_promise;

  waos::binary to_bin(const char* s, std::size_t n) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(s);
    return waos::binary(p, p + n);
  }

  waos::binary make_key(std::uint32_t i) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "k%08u", i);
    return to_bin(buf, 9);
  }

  void wire_client(serialization::flat_socket_rpc_client& c) {
    c.register_basic_types();
    c.channel().template register_exception_type<waos::db_error>();
    c.channel().template register_exception_type<waos::db_invalid_argument>();
    c.channel().template register_exception_type<waos::db_closed>();
    c.channel().template register_exception_type<waos::db_io_error>();
    c.register_message_type<waos::db_socket_opened>();
    c.register_message_type<waos::db_scan_item>();
    c.register_message_type<waos::db_scan_key>();
    c.register_message_type<waos::db_scan_done>();
    c.register_message_type<waos::db_scan_error>();
    c.register_message_type<waos::db_visible_event>();
    c.register_message_type<waos::db_durable_event>();
  }

  void ensure_db_client() {
    if(db_control) {
      return;
    }
    struct socket_options opts = {
      .input_buffer_size = kBuf,
      .output_buffer_size = kBuf,
      .flush_threshold = kBuf,
      .data_event_threshold = 0,
      .drain_event_threshold = 4,
      .flags = 0,
    };
    socket_open(kDbControlSock, &opts);
    socket_open(kDbPrivateSock, &opts);

    db_control = std::make_unique<serialization::flat_socket_rpc_client>(kDbControlSock);
    db_private = std::make_unique<serialization::flat_socket_rpc_client>(kDbPrivateSock);
    wire_client(*db_control);
    wire_client(*db_private);

    db_private->on_message<waos::db_scan_item>(
        std::function<void(waos::db_scan_item&)>([](waos::db_scan_item&) { ++scan_items; }));
    db_private->on_message<waos::db_scan_done>(
        std::function<void(waos::db_scan_done&)>([](waos::db_scan_done&) {
          ++scan_dones;
          if(scan_done_promise) {
            scan_done_promise->resolve();
          }
        }));
    db_private->on_message<waos::db_scan_error>(
        std::function<void(waos::db_scan_error&)>([](waos::db_scan_error& e) {
          if(scan_done_promise) {
            try {
              throw waos::db_io_error(e.message);
            } catch(...) {
              scan_done_promise->reject(std::current_exception());
            }
          }
        }));

    auto* control_ptr = db_control.get();
    auto* private_ptr = db_private.get();
    socket_handlers::register_socket_handler(kDbControlSock, [control_ptr]() {
      control_ptr->handle_data();
    });
    socket_handlers::register_socket_handler(kDbPrivateSock, [private_ptr]() {
      private_ptr->handle_data();
    });
  }

  bench_result finish_result(std::uint64_t keys, std::chrono::steady_clock::duration d) {
    bench_result r;
    r.keys = keys;
    const auto ms = std::chrono::duration<double, std::milli>(d).count();
    r.ms = ms > 0 ? static_cast<std::uint64_t>(ms + 0.5) : 0;
    if(r.ms > 0) {
      r.keys_per_s = keys * 1000 / r.ms;
    }
    return r;
  }

}  // namespace

std::shared_ptr<serialization::promise<bench_result>> DbBenchClient::bench_get(std::uint32_t n) {
  ensure_db_client();
  const auto t0 = std::chrono::steady_clock::now();
  std::uint64_t got = 0;
  for(std::uint32_t i = 0; i < n; ++i) {
    auto result = co_await db_control->call<waos::db_get_result>(kDbGet, make_key(i));
    if(result.present) {
      ++got;
    }
  }
  co_return finish_result(got, std::chrono::steady_clock::now() - t0);
}

std::shared_ptr<serialization::promise<bench_result>> DbBenchClient::bench_scan() {
  ensure_db_client();
  scan_items = 0;
  scan_dones = 0;
  scan_done_promise = std::make_shared<serialization::promise<void>>();
  co_await db_control->call<void>(kDbOpenSocket, kClientPid, kDbPrivateSock);
  waos::db_scan_query q;
  const auto t0 = std::chrono::steady_clock::now();
  co_await db_control->call<void>(kDbStartScan, kClientPid, kDbPrivateSock, q);
  if(scan_dones == 0) {
    co_await scan_done_promise;
  }
  co_return finish_result(scan_items, std::chrono::steady_clock::now() - t0);
}

void register_all() {
  rpc_server_ptr->register_basic_types();
  ensure_db_client();
  register_rpc(db_bench_client);
  bench_result result;
  register_rpc(result);
}
