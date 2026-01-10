#include "module.hpp"

#include <serialization/stream_writer.hpp>
#include <io/socket.h>

rpc_server_type rpc_server;
serialization::type_registry registry;

void register_all();

extern "C" size_t rpc_description(char** buffer) {  
  serialization::type_description::create(serialization::read_annotation(), registry);
  register_all();
  std::ostringstream output_stream;
  serialization::stream_writer writer(output_stream);  

  writer.register_serialization_function<serialization::type_description>();
  writer.register_serialization_function<serialization::read_annotation>();

  serialize(writer, registry);
  writer.write_rest();

  *buffer = (char*)malloc(output_stream.str().size());
  memcpy(*buffer, output_stream.str().data(), output_stream.str().size());
  return output_stream.str().size();
}


extern "C" __attribute__((import_module("waos"), import_name("waos_socket_flush")))
void waos_socket_flush(std::uint32_t socket);



extern "C" void waos_socket_data(std::uint32_t socket) {

  if(socket != 0) { // ignore for now
    return;
  }

  struct socket_info* socket0 = get_socket_info(0);

  // --- read one length-prefixed request from socket0 input ---
  uint32_t req_len = 0;
  if (circular_buffer_readable_bytes(&socket0->input_buffer) < sizeof(req_len)) {
    return; // not enough data yet
  }
  circular_buffer_peek(&socket0->input_buffer, 0, &req_len, sizeof(req_len));
  size_t available = circular_buffer_readable_bytes(&socket0->input_buffer);
  if (available < sizeof(req_len) + req_len) {
    return; // wait for full message
  }

  // consume length prefix
  circular_buffer_skip_read(&socket0->input_buffer, sizeof(req_len));
  // create reader at the end of the length prefix
  serialization::circular_buffer_reader reader(&socket0->input_buffer, 0);

  uint32_t placeholder = 0;    
  circular_buffer_poke(&socket0->output_buffer, 0, &placeholder, sizeof(placeholder));
  serialization::circular_buffer_writer writer(&socket0->output_buffer, sizeof(placeholder));  

  auto promise = rpc_server.handle_call(reader, writer);
  promise->on_resolved([&](void) {
    // skip readed bytes
    circular_buffer_skip_read(&socket0->input_buffer, req_len);
    writer.write_rest();

    uint32_t payload_len = writer.write_offset_ - sizeof(placeholder);
    circular_buffer_poke(&socket0->output_buffer, 0, &payload_len, sizeof(payload_len));
    circular_buffer_commit_write(&socket0->output_buffer, writer.write_offset_);    

    waos_socket_flush(0); // always flush after full response
  });

}

extern "C" void waos_socket_drain(int socket) {
  if(socket != 0) { // ignore for now
    return;
  }

  /// not handled
}

extern "C" void start() {
  register_all();
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
}