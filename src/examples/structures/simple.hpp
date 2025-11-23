#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include "serialization/promise.hpp"

class Simple {
  public:
    static std::string hello() { return "hello"; }
    static std::uint64_t fib(std::uint64_t n) { 
      uint64_t a = 0, b = 1;
      for(std::uint64_t i = 0; i < n; i++) {
        uint64_t c = a + b;
        a = b;
        b = c;
      }
      return a;
    }
    static std::shared_ptr<serialization::promise<std::string>> hello_promise() {
      return serialization::promise<std::string>::resolved("hello");
    }
};
