#include "../structures/dbg_stats.hpp"
#include <utils/module.hpp>

DbgStats dbg_stats;

void register_all() {
  rpc_server_ptr->register_basic_types();
  register_rpc(dbg_stats);
}
