#include "../structures/simple.hpp"
#include <utils/module.hpp>

Simple simple;

void register_all() {
  register_rpc(simple);
}