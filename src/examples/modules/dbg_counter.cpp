#include "../structures/dbg_counter.hpp"
#include <utils/module.hpp>

DbgCounter dbg_counter;

void register_all() {
  rpc_server_ptr->register_basic_types();
  register_rpc(dbg_counter);
}
