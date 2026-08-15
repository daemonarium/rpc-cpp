#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include "serialization/annotations.hpp"
#include "serialization/promise.hpp"

namespace waos {

  using binary = std::vector<std::uint8_t>;

  enum class monitor_flag : std::uint32_t {
    NONE = 0,
    KILLED = 1u << 0,
    ERROR = 1u << 1,
    COPY = 1u << 2,
    COPY_PAYLOAD = 1u << 3,
    DATA = 1u << 4,
    DRAIN = 1u << 5,
    SAVE = 1u << 6,
    MEMORY = 1u << 7,
    INITIAL_BUFFER_STATE = 1u << 8,
    INITIAL_BUFFER_DATA = 1u << 9,
    BUFFER_STATE = 1u << 10,
    BUFFER_DATA = 1u << 11,
    MANA = 1u << 12,
    BIND = 1u << 13,
  };

  struct monitor_config {
    std::uint32_t flags;
  };

  struct save_result {
    std::uint64_t snapshot_uid;
    binary memory;
    size_t mana_used;
    size_t mana_limit;
  };

  class kernel {
    public:
      kernel() = default;

      using process_id = size_t;
      using socket_id = size_t;

      enum class direction {
        NONE = 0,
        IN = 1,
        OUT = 2,
        BOTH = 3,
      };

      struct socket_binding {
        socket_id socket;
        process_id peer_process;
        socket_id peer_socket;
        direction direction;
      };

      static std::shared_ptr<serialization::promise<process_id>> spawn(binary code, size_t memory_limit, size_t mana_limit) {
        return serialization::promise<process_id>::resolved(0);
      }
      static std::shared_ptr<serialization::promise<process_id>> resurect(binary code, binary memory,
                                                                          size_t memory_limit, size_t mana_limit) {
        return serialization::promise<process_id>::resolved(0);
      }
      static std::shared_ptr<serialization::promise<save_result>> save(process_id id) {
        return serialization::promise<save_result>::resolved(save_result{});
      }
      static std::shared_ptr<serialization::promise<void>> kill(process_id id) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<void>> bind(process_id process_from, socket_id socket_from,
                   process_id process_to, socket_id socket_to,
                   direction direction) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<void>> unbind(process_id process_from, socket_id socket_from,
                      process_id process_to, socket_id socket_to,
                      direction direction) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<void>> monitor(process_id target,
                                                            process_id monitor, socket_id monitor_socket,
                                                            monitor_config config) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<void>> demonitor(process_id target,
                                                              process_id monitor, socket_id monitor_socket) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<size_t>> measure_mana(process_id process, size_t new_mana) {
        return serialization::promise<size_t>::resolved(0);
      }

      static std::shared_ptr<serialization::promise<void>> set_mana_limit(process_id process, size_t mana_limit) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<std::vector<socket_binding>>> list_socket_bindings(process_id id) {
        return serialization::promise<std::vector<socket_binding>>::resolved({});
      }

      static std::shared_ptr<serialization::promise<binary>> get_rpc_description(process_id id, socket_id socket) {
        return serialization::promise<binary>::resolved({});
      }
  };

  struct process_event {
    kernel::process_id process;
    virtual ~process_event() {}
  };

  struct [[=serialization::message()]] process_killed_event : process_event {
    virtual ~process_killed_event() {}
  };

  struct [[=serialization::message()]] process_error_event : process_event {
    virtual ~process_error_event() {}
    std::string error;
  };

  struct [[=serialization::message()]] process_copy_event : process_event {
    virtual ~process_copy_event() {}
    kernel::socket_id socket;
    kernel::direction direction;
    size_t amount;
  };

  struct [[=serialization::message()]] process_data_event : process_event {
    virtual ~process_data_event() {}
    kernel::socket_id socket;
    kernel::direction direction;
  };

  struct [[=serialization::message()]] process_drain_event : process_event {
    virtual ~process_drain_event() {}
    kernel::socket_id socket;
    kernel::direction direction;
  };

  struct [[=serialization::message()]] process_save_event : process_event {
    virtual ~process_save_event() {}
    std::uint64_t snapshot_uid;
  };

  struct [[=serialization::message()]] process_memory_event : process_event {
    virtual ~process_memory_event() {}
    size_t size;
  };

  struct [[=serialization::message()]] process_socket_state_event : process_event {
    virtual ~process_socket_state_event() {}
    kernel::socket_id socket;
    kernel::direction direction;
    size_t buffer_size;
    size_t read_offset;
    size_t write_offset;
  };

  struct [[=serialization::message()]] process_measure_mana_event : process_event {
    virtual ~process_measure_mana_event() {}
    size_t new_mana;
  };

  struct [[=serialization::message()]] process_set_mana_limit_event : process_event {
    virtual ~process_set_mana_limit_event() {}
    size_t mana_limit;
  };

  struct [[=serialization::message()]] process_bind_event : process_event {
    virtual ~process_bind_event() {}
    kernel::socket_id socket;
    kernel::process_id peer_process;
    kernel::socket_id peer_socket;
    kernel::direction direction;
  };

  struct [[=serialization::message()]] process_unbind_event : process_event {
    virtual ~process_unbind_event() {}
    kernel::socket_id socket;
    kernel::process_id peer_process;
    kernel::socket_id peer_socket;
    kernel::direction direction;
  };

  struct process_event_pointer {
    std::shared_ptr<process_event> event_pointer;
  };

}
