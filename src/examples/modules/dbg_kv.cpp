#include "../structures/dbg_kv.hpp"
#include <utils/module.hpp>

DbgKv dbg_kv;

void register_all() {
  rpc_server_ptr->register_basic_types();
  register_rpc(dbg_kv);
}
