#pragma once

#include <cstdint>
#include <map>
#include <string>

class DbgShop {
  public:
    static std::string name() {
      return "DbgShop";
    }

    static std::string symbol() {
      return "DBG";
    }

    static void mint(std::string addr, std::uint64_t amount) {
      balances()[std::move(addr)] = amount;
    }

    static std::uint64_t balanceOf(std::string addr) {
      const auto it = balances().find(addr);
      if (it == balances().end()) {
        return 0;
      }
      return it->second;
    }

    static bool transfer(std::string from, std::string to, std::uint64_t amount) {
      if (amount == 0) {
        return true;
      }
      if (balanceOf(from) < amount) {
        return false;
      }
      balances()[from] -= amount;
      balances()[to] += amount;
      return true;
    }

  private:
    static std::map<std::string, std::uint64_t>& balances() {
      static std::map<std::string, std::uint64_t> m;
      return m;
    }
};
