#include <utils/module.hpp>
#include "../structures/kernel.hpp"

waos::kernel kernel;

void register_all() {
  register_rpc(kernel);
  waos::process_event_pointer eventPointer;
  register_rpc(eventPointer);
  waos::process_killed_event killedEvent;
  register_rpc(killedEvent);
  waos::process_error_event errorEvent;
  register_rpc(errorEvent);
}
