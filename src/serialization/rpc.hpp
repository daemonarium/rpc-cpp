#pragma once

#include "type_registry.hpp"

namespace serialization {

  using binary = std::vector<std::uint8_t>;

  struct [[=serialization::message()]] rpc_request {
    std::uint64_t request_id;
    std::uint16_t function_id;
    binary arguments;
  };

  struct [[=serialization::message()]] rpc_response {
    std::uint64_t request_id;
    bool success = true;
    binary result;    
  };

  struct [[=serialization::message()]] rpc_error {
    std::uint64_t request_id;
    bool success = false;
    std::string error;
  };

}