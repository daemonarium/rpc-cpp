#include "../structures/dbg_shop.hpp"
#include <utils/module.hpp>

DbgShop dbg_shop;

void register_all() {
  rpc_server_ptr->register_basic_types();
  register_rpc(dbg_shop);
}
