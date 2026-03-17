#pragma once

#include <vector>
#include <cstdint>
#include "annotations.hpp"
#include <exception>

namespace serialization {

  using binary = std::vector<std::uint8_t>;

  struct [[=serialization::message()]] rpc_request {
    std::uint64_t request_id;
    std::uint64_t function_id;
  };

  struct [[=serialization::message()]] rpc_response {
    std::uint64_t request_id;
  };

  struct [[=serialization::message()]] rpc_error {
    std::uint64_t request_id;
  };

  class unknown_exception : public std::exception {
    public:
      unknown_exception(const std::string& message) : message_(message) {}
      ~unknown_exception() {}
      const char* what() const noexcept override { return message_.c_str(); }
    private:
      std::string message_;
  };


}