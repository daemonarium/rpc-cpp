#pragma once

#include "serialization/socket_rpc_server.hpp"
#include "serialization/type_registry.hpp"
#include "serialization/type_description.hpp"
#include <memory>

using rpc_server_type = serialization::socket_rpc_server;
extern std::unique_ptr<rpc_server_type> rpc_server_ptr;
extern serialization::type_registry registry;

template <typename T>
void register_rpc(T& t) {        
#ifdef RPC_SERVER
  if(rpc_server_ptr) {
    rpc_server_ptr->register_recursive(t);
  }
#endif

#ifdef RPC_TYPE_DESCRIPTION
  registry.register_type(serialization::type_description::create(t, registry));  
#endif
}

template <typename T>
void register_type(T& t) {
#ifdef RPC_SERVER
  if(rpc_server_ptr) {
    rpc_server_ptr->register_recursive(t);
  }
#endif

#ifdef RPC_TYPE_DESCRIPTION
  registry.register_type(serialization::type_description::create(t, registry));  
#endif
}