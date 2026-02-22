#pragma once

#include <exception>
#include <string>

namespace serialization {

  class rpc_exception : public std::exception {
    public:
      rpc_exception(const std::string& message) : message_(message) {}
      ~rpc_exception() {}
      const char* what() const noexcept override { return message_.c_str(); }
    private:
      std::string message_;
  };

}
