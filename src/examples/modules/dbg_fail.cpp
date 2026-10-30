#include "../structures/dbg_fail.hpp"
#include <utils/module.hpp>

DbgFail dbg_fail;

void register_all() {
  rpc_server_ptr->register_basic_types();
  register_rpc(dbg_fail);
}
