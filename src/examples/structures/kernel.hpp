#pragma once

#include <map>
#include <string>
#include "serialization/annotations.hpp"
#include "serialization/promise.hpp"

namespace waos {


  using binary = std::vector<std::uint8_t>;

  struct range {
    binary gt_key;
    bool gt_inclusive;
    binary lt_key;
    bool lt_inclusive;
    size_t limit;
    range() {}
  };

  class Kernel {
    public:
      Kernel() = default;      

      using process_id = size_t;
      using channel_id = size_t;
      using module_id = size_t;

      using module_flags = std::uint32_t;
      enum class module_flag {
        MEASURE_GAS = 1 << 0,
        SOFT_FLOAT = 1 << 1        
      };

      enum class direction {
        NONE = 0,
        IN = 1,
        OUT = 2,
        BOTH = 3,
      };

      static std::shared_ptr<serialization::promise<process_id>> create_process() {
        return serialization::promise<process_id>::resolved(0);
      }
      static std::shared_ptr<serialization::promise<void>> destroy_process(process_id id) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<void>> load_module(process_id process_id, module_id module_id,
                       std::string db_name, std::string table_name, binary key,
                       module_flags flags) {
        return serialization::promise<void>::resolved();
      }
      
      static std::shared_ptr<serialization::promise<void>> load_memory(process_id process_id, std::string db_name, 
                                                                std::string table_name, binary key) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<void>> store_memory(process_id process_id, std::string db_name,
                                                                 std::string table_name, binary key) {
        return serialization::promise<void>::resolved();
      }
     
      static std::shared_ptr<serialization::promise<void>> initialize_process(process_id process_id) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<void>> finalize_process(process_id process_id) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<void>> send_message(process_id process_id, channel_id channel_id,
                                                                 binary message) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<std::vector<std::uint8_t>>> request(process_id process_id, channel_id channel_id,
                                                                                 binary message) {
        return serialization::promise<std::vector<std::uint8_t>>::rejected(
          std::make_exception_ptr(std::runtime_error("Not implemented"))
        );
      }
      
      static std::shared_ptr<serialization::promise<void>> link(process_id process_id,
                module_id from_module, module_id to_module,
                std::string from_name, std::string to_name) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<void>> unlink(process_id process_id, 
                  module_id from_module, module_id to_module, std::string from_name,
                  std::string to_name) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<void>> connect(process_id process_from, channel_id channel_from, 
                   process_id process_to, channel_id channel_to,
                   direction direction) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<void>> disconnect(process_id process_from, channel_id channel_from,
                      process_id process_to, channel_id channel_to,
                      direction direction) {
        return serialization::promise<void>::resolved();
      }
      
      static std::shared_ptr<serialization::promise<void>> monitor(process_id target,
                                                            process_id monitor, channel_id monitor_channel) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<void>> unmonitor(process_id target,
                                                              process_id monitor, channel_id monitor_channel) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<void>> set(std::string db_name, std::string table_name, 
                                                        binary key, binary value) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<binary>> get(std::string db_name, std::string table_name, 
                                                          binary key) {
        return serialization::promise<binary>::resolved(binary());
      }
      static std::shared_ptr<serialization::promise<void>> remove(std::string db_name, std::string table_name, 
                                                          binary key) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<std::vector<std::pair<binary, binary>>>> range_get(
        std::string db_name, std::string table_name, range range) {
        return serialization::promise<std::vector<std::pair<binary, binary>>>::resolved(
          std::vector<std::pair<binary, binary>>()
        );
      }

      static std::shared_ptr<serialization::promise<void>> range_remove(
        std::string db_name, std::string table_name, range range) {
        return serialization::promise<void>::resolved();
      }

      struct batch_operation {                        
        range range;
        binary value;
        enum class type {
          SET = 0,
          REMOVE = 1,
        } type;
      };

      using batch_operations = std::vector<batch_operation>;

      static std::shared_ptr<serialization::promise<void>> batch_update(
        std::string db_name, std::string table_name, batch_operations operations) {
        return serialization::promise<void>::resolved();
      }

      //// TODO: add data observation support with channels



    private:      
  };



}