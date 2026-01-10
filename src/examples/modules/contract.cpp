
#include "../structures/contract.hpp"
#include <utils/module.hpp>

void register_all() {
  Contract contract = Contract::get_rpc_instance();
  register_rpc(contract);
}



