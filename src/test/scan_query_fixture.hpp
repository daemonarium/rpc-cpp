#pragma once

#include <cstdint>

#include "serialization/stream_protocol.hpp"

namespace waos {

  // Stand-in for the db contract scan argument. RPC tests do not include
  // the product contract.
  enum class db_bound_mode : std::uint8_t {
    none = 0,
    exclusive = 1,
    inclusive = 2,
  };

  struct db_scan_query {
    binary lower;
    db_bound_mode lower_mode = db_bound_mode::none;
    binary upper;
    db_bound_mode upper_mode = db_bound_mode::none;
    std::uint64_t limit = 0;
    bool reverse = false;
    bool keys_only = false;
  };

}
