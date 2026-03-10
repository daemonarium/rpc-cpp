#pragma once

#include "type_registry.hpp"

namespace serialization {

  using binary = std::vector<std::uint8_t>;

  struct [[=serialization::message()]] rpc_request {
    std::uint64_t request_id;
    std::uint64_t function_id;
  };

  struct [[=serialization::message()]] rpc_response {
    std::uint64_t request_id;
    bool success = true;
  };

  struct [[=serialization::message()]] rpc_error {
    std::uint64_t request_id;
    bool success = false;
    std::string error;
  };

}