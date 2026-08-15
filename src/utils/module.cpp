#include "module.hpp"

#include <serialization/type_registry_blob.hpp>
#include <serialization/type_description.hpp>
#include <socket/socket.h>
#include <memory>
#include <utils/socket_handlers.hpp>

std::unique_ptr<rpc_server_type> rpc_server_ptr;
serialization::type_registry registry(true);

void register_all();

extern "C" size_t rpc_description(std::uint32_t socket, char** buffer) {
  // Types were registered in start() via register_all(). Do not call register_all()
  // again — re-registration throws under wasm exceptions and empties the export.
  // Catch-all: a trap mid-serialize can corrupt heap (including the live RPC
  // channel). Prefer returning 0 over propagating into the host.
  try {
    serialization::type_description::create(serialization::read_annotation(), registry);
    return serialization::serialize_type_registry_blob_malloc(registry, buffer);
  } catch(...) {
    return 0;
  }
}

extern "C" void waos_socket_data(std::uint32_t socket) {
  socket_handlers::handle_socket_data(socket);
}

extern "C" void waos_socket_output_drain(int socket) {
  socket_handlers::handle_socket_drain(socket);
}

extern "C" void start() {
  sockets_init(3);
  struct socket_options socket_options = {
    .input_buffer_size = 1024,
    .output_buffer_size = 1024,
    .flush_threshold = 1024,
    .data_event_threshold = 1024,
    .drain_event_threshold = 1024,
    .flags = 0,
  };
  socket_open(0, &socket_options);

#if defined(WAOS_MODULE_RPC_COMPRESSED)
  registry.set_data_format(serialization::serialization_format::compressed);
#endif

  // Initialize socket_rpc_server after socket is opened
  rpc_server_ptr = std::make_unique<rpc_server_type>(0);
  
  // Register all RPC methods after server is initialized
  register_all();
  
  // Register handler for socket 0
  auto server_ptr = rpc_server_ptr.get();
  socket_handlers::register_socket_handler(0, [server_ptr]() {
    if(server_ptr) {
      server_ptr->handle_data();
    }
  });
}
