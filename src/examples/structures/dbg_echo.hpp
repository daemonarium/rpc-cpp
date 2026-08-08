#pragma once

#include <vector>
#include <cstdint>

using binary = std::vector<std::uint8_t>;

class DbgEcho {
  public:
    static binary echo(binary data) {
      return data;
    }
};
