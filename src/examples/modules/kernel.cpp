#include <utils/module.hpp>
#include "../structures/kernel.hpp"

waos::kernel kernel;

void register_all() {
  register_rpc(kernel);
  waos::monitor_flag monitorFlag = waos::monitor_flag::NONE;
  register_rpc(monitorFlag);
  waos::monitor_config monitorConfig;
  register_rpc(monitorConfig);
  waos::save_result saveResult;
  register_rpc(saveResult);
  waos::process_killed_event killedEvent;
  register_rpc(killedEvent);
  waos::process_error_event errorEvent;
  register_rpc(errorEvent);
  waos::process_copy_event copyEvent;
  register_rpc(copyEvent);
  waos::process_data_event dataEvent;
  register_rpc(dataEvent);
  waos::process_drain_event drainEvent;
  register_rpc(drainEvent);
  waos::process_save_event saveEvent;
  register_rpc(saveEvent);
  waos::process_memory_event memoryEvent;
  register_rpc(memoryEvent);
  waos::process_socket_state_event socketStateEvent;
  register_rpc(socketStateEvent);
  waos::process_measure_mana_event measureManaEvent;
  register_rpc(measureManaEvent);
  waos::process_set_mana_limit_event setManaLimitEvent;
  register_rpc(setManaLimitEvent);
}
