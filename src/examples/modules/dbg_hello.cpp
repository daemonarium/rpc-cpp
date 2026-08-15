#include "../structures/dbg_hello.hpp"
#include <utils/module.hpp>

DbgHello dbg_hello;

void register_all() {
  rpc_server_ptr->register_basic_types();
  register_rpc(dbg_hello);
}
