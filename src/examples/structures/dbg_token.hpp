#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <serialization/promise.hpp>

/** ERC20-like sample whose balances live in a peer dbg_kv process (socket 2). */
class DbgToken {
  public:
    static std::string name();
    static std::string symbol();

    static std::shared_ptr<serialization::promise<void>> mint(std::string addr, std::uint64_t amount);
    static std::shared_ptr<serialization::promise<std::uint64_t>> balanceOf(std::string addr);
};
