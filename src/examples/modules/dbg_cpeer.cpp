#include "../structures/dbg_kv.hpp"
#include <utils/module.hpp>
#include <socket/socket.h>

/** Compressed-format peer KV store (same API as dbg_kv). */
DbgKv dbg_cpeer;

void register_all() {
  // KERNEL_API socket for typical shield bind (process.1 ↔ shield KERNEL_API).
  struct socket_options opts = {
    .input_buffer_size = 1024,
    .output_buffer_size = 1024,
    .flush_threshold = 1024,
    .data_event_threshold = 1024,
    .drain_event_threshold = 1024,
    .flags = 0,
  };
  socket_open(1, &opts);

  rpc_server_ptr->register_basic_types();
  register_rpc(dbg_cpeer);
}
