#pragma once

#include "serialization/rpc_server.hpp"
#include "serialization/type_registry.hpp"
#include "serialization/type_description.hpp"
#include "serialization/circular_buffer_reader.hpp"
#include "serialization/circular_buffer_writer.hpp"

using rpc_server_type = serialization::rpc_server<serialization::circular_buffer_reader, serialization::circular_buffer_writer>;
extern rpc_server_type rpc_server;
extern serialization::type_registry registry;

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