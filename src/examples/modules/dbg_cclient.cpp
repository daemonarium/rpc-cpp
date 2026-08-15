#include "../structures/dbg_token.hpp"
#include <utils/module.hpp>
#include <serialization/socket_rpc_client.hpp>
#include <utils/socket_handlers.hpp>
#include <socket/socket.h>
#include <memory>
#include <string>

/** Compressed USER + compressed peer client → dbg_cpeer. */
DbgToken dbg_cclient;

namespace {

  constexpr std::uint64_t kKvPut = 0;
  constexpr std::uint64_t kKvGet = 1;

  std::unique_ptr<serialization::compressed_socket_rpc_client> kv_client;

  std::string balance_key(const std::string& addr) {
    return "bal." + addr;
  }

  void ensure_kv_client() {
    if (kv_client) {
      return;
    }
    struct socket_options opts = {
      .input_buffer_size = 1024,
      .output_buffer_size = 1024,
      .flush_threshold = 1024,
      .data_event_threshold = 1024,
      .drain_event_threshold = 1024,
      .flags = 0,
    };
    socket_open(1, &opts);
    socket_open(2, &opts);
    kv_client = std::make_unique<serialization::compressed_socket_rpc_client>(2);
    kv_client->register_basic_types();
    auto* client_ptr = kv_client.get();
    socket_handlers::register_socket_handler(2, [client_ptr]() {
      client_ptr->handle_data();
    });
  }

}  // namespace

std::string DbgToken::name() {
  return "DbgCClient";
}

std::string DbgToken::symbol() {
  return "DCC";
}

std::shared_ptr<serialization::promise<void>> DbgToken::mint(std::string addr, std::uint64_t amount) {
  ensure_kv_client();
  const std::string key = balance_key(addr);
  const std::string value = std::to_string(amount);
  co_await kv_client->call<void>(kKvPut, key, value);
  co_return;
}

std::shared_ptr<serialization::promise<std::uint64_t>> DbgToken::balanceOf(std::string addr) {
  ensure_kv_client();
  const std::string key = balance_key(addr);
  auto value = co_await kv_client->call<std::string>(kKvGet, key);
  if (value.empty()) {
    co_return 0;
  }
  co_return static_cast<std::uint64_t>(std::stoull(value));
}

void register_all() {
  rpc_server_ptr->register_basic_types();
  ensure_kv_client();
  register_rpc(dbg_cclient);
}
