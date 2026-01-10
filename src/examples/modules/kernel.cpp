#include <utils/module.hpp>
#include "../structures/kernel.hpp"

waos::Kernel kernel;

void register_all() {
  register_rpc(kernel);
}
