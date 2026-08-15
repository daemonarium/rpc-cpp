#include "../structures/dbg_echo.hpp"
#include <utils/module.hpp>

DbgEcho dbg_echo;

void register_all() {
  rpc_server_ptr->register_basic_types();
  register_rpc(dbg_echo);
}
