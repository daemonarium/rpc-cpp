#pragma once

#include <string>
#include <cstdint>

class DbgHello {
  public:
    static std::string hello() {
      return "hello";
    }

    static std::uint64_t fib(std::uint64_t n) {
      std::uint64_t a = 0;
      std::uint64_t b = 1;
      for (std::uint64_t i = 0; i < n; i++) {
        std::uint64_t c = a + b;
        a = b;
        b = c;
      }
      return a;
    }
};
