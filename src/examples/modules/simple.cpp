#include "serialization/type_registry.hpp"
#include "serialization/type_description.hpp"
#include "serialization/rpc_server.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/stream_writer.hpp"

#include "../structures/simple.hpp"

#include <sstream>
#include <memory>

using rpc_server_type = serialization::rpc_server<serialization::stream_reader, serialization::stream_writer>;
rpc_server_type rpc_server;
serialization::type_registry registry;

Simple simple;

template <typename T>
void register_rpc(T& t) {        
#ifdef RPC_SERVER
  rpc_server.register_recursive(t, rpc_server);
#endif

#ifdef RPC_TYPE_DESCRIPTION
  registry.register_type(serialization::type_description::create(t, registry));  
#endif
}

template <typename T>
void register_type(T& t) {
#ifdef RPC_SERVER
  rpc_server_type::register_recursive(t, rpc_server);
#endif

#ifdef RPC_TYPE_DESCRIPTION
  registry.register_type(serialization::type_description::create(t, registry));  
#endif
}

void register_all() {
  register_rpc(simple);
}

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

extern "C" __attribute__((import_module("env"), import_name("send_message")))
void send_message(std::uint32_t channel, char* buffer, size_t size);

extern "C" void handle_message(std::uint32_t channel, char* buffer, size_t size) {
  if(channel != 0) return; // only handle channel 0 for request and response messages
  std::istringstream input_stream(std::string(buffer, size));
  serialization::stream_reader reader(input_stream);
  std::ostringstream output_stream;
  serialization::stream_writer writer(output_stream);
  auto promise = rpc_server.handle_call(reader, writer);
  promise->on_resolved([&](void) {
    send_message(0, output_stream.str().data(), output_stream.str().size());
  });
}

extern "C" void start() {
  register_all();
}
