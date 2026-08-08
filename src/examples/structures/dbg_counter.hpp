#pragma once

#include <cstdint>

class DbgCounter {
  public:
    static std::uint64_t get() {
      return value();
    }

    static void set(std::uint64_t n) {
      value() = n;
    }

    static std::uint64_t inc() {
      return ++value();
    }

  private:
    static std::uint64_t& value() {
      static std::uint64_t v = 0;
      return v;
    }
};
