#include "../structures/dbg_forward.hpp"
#include <utils/module.hpp>
#include <utils/socket_handlers.hpp>
#include <serialization/socket_rpc_client.hpp>
#include <socket/socket.h>

#include <cstdint>
#include <memory>

DbgForward dbg_forward;

namespace {

  constexpr std::uint64_t kDbTable = 4;
  constexpr std::uint32_t kDbSock = 1;
  constexpr std::size_t kBuf = 4096;

  std::uint32_t self_pid_ = 0;
  std::unique_ptr<serialization::flat_socket_rpc_client> db_client;
  std::unique_ptr<waos::observable::StreamMultiplexer<
    serialization::flat_socket_rpc_client, serialization::flat_socket_rpc_client>> db_mux;

  void wire_stream_types(auto& c) {
    c.template register_message_type<waos::socket_opened>();
    c.template register_message_type<waos::stream_item>();
    c.template register_message_type<waos::stream_key>();
    c.template register_message_type<waos::stream_done>();
    c.template register_message_type<waos::stream_error>();
    c.template register_message_type<waos::stream_reset>();
    c.template register_message_type<waos::stream_cancel>();
    c.template register_message_type<waos::stream_opened>();
  }

  void ensure_db() {
    if(db_mux) return;
    struct socket_options opts = {
      .input_buffer_size = kBuf,
      .output_buffer_size = kBuf,
      .flush_threshold = kBuf,
      .data_event_threshold = 0,
      .drain_event_threshold = 4,
      .flags = 0,
    };
    socket_open(kDbSock, &opts);
    db_client = std::make_unique<serialization::flat_socket_rpc_client>(kDbSock);
    db_client->register_basic_types();
    wire_stream_types(*db_client);
    auto* client_ptr = db_client.get();
    socket_handlers::register_socket_handler(kDbSock, [client_ptr]() {
      client_ptr->handle_data();
    });
    db_mux = std::make_unique<waos::observable::StreamMultiplexer<
      serialization::flat_socket_rpc_client, serialization::flat_socket_rpc_client>>(
      *db_client, self_pid_, kDbSock);
    DbgForward::bind_upstream(*db_mux, kDbTable);
  }

}

extern "C" void waos_set_process_id(std::uint32_t pid) {
  self_pid_ = pid;
}

void register_all() {
  rpc_server_ptr->register_basic_types();
  rpc_server_ptr->register_stream_message_types();
  ensure_db();
  register_rpc(dbg_forward);
  waos::db_bound_mode boundMode = waos::db_bound_mode::none;
  register_rpc(boundMode);
  waos::stream_handle streamHandle;
  register_rpc(streamHandle);
  waos::socket_opened socketOpened;
  register_rpc(socketOpened);
  waos::stream_item scanItem;
  register_rpc(scanItem);
  waos::stream_key scanKey;
  register_rpc(scanKey);
  waos::stream_done scanDone;
  register_rpc(scanDone);
  waos::stream_error scanError;
  register_rpc(scanError);
  waos::stream_reset scanReset;
  register_rpc(scanReset);
  waos::stream_opened streamOpened;
  register_rpc(streamOpened);
  waos::stream_cancel streamCancel;
  register_rpc(streamCancel);
}
