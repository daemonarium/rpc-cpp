#pragma once

#include <map>
#include <string>
#include "serialization/annotations.hpp"
#include "serialization/promise.hpp"

namespace waos {


  using binary = std::vector<std::uint8_t>;

  class kernel {
    public:
      kernel() = default;      

      using process_id = size_t;
      using socket_id = size_t;
      //using module_id = size_t;

      enum class direction {
        NONE = 0,
        IN = 1,
        OUT = 2,
        BOTH = 3,
      };

      static std::shared_ptr<serialization::promise<process_id>> spawn(binary code, size_t memory_limit, size_t mana_limit) {
        return serialization::promise<process_id>::resolved(0);
      }
      static std::shared_ptr<serialization::promise<process_id>> resurect(binary code, binary memory,
                                                                          size_t memory_limit, size_t mana_limit) {
        return serialization::promise<process_id>::resolved(0);
      }
      static std::shared_ptr<serialization::promise<binary>> save(process_id id) {
        return serialization::promise<binary>::resolved(binary());
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
                                                            process_id monitor, socket_id monitor_socket) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<void>> demonitor(process_id target,
                                                              process_id monitor, socket_id monitor_socket) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<size_t>> measure_mana(process_id process, size_t new_mana) {
        return serialization::promise<size_t>::resolved(0);
      }

      static std::shared_ptr<serialization::promise<void>> set_mana_limit(size_t mana_limit) {
        return serialization::promise<void>::resolved();
      }
  };

  struct process_event {    
    kernel::process_id process;
    virtual ~process_event() {}
  };

  struct [[=serialization::dynamic()]] process_killed_event : process_event {  
    virtual ~process_killed_event() {}    
  };

  struct [[=serialization::dynamic()]] process_error_event : process_event {
    virtual ~process_error_event() {}
    std::string error;
  };

  struct process_event_pointer {
    std::shared_ptr<process_event> event_pointer;
  };

}