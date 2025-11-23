#pragma once

#include <map>
#include <string>
#include "serialization/annotations.hpp"

namespace ipi {
  template <typename K, typename V>
  using index = std::map<K, V>;

  using address = std::string;
  static std::string sender() { return "user"; }
};


using amount = std::uint64_t;

template <typename T>
concept ERC20 = requires(T) {
  { T::name() } -> std::same_as<std::string>;
  { T::symbol() } -> std::same_as<std::string>;
  { T::decimals() } -> std::same_as<std::uint8_t>;
  { T::totalSupply() } -> std::same_as<amount>;
  { T::balanceOf(ipi::address()) } -> std::same_as<amount>;
  { T::allowance(ipi::address(), ipi::address()) } -> std::same_as<amount>;
  { T::transfer(ipi::address(), amount()) } -> std::same_as<bool>;
  { T::approve(ipi::address(), amount()) } -> std::same_as<bool>;
  { T::transferFrom(ipi::address(), ipi::address(), amount()) } -> std::same_as<bool>;  
};

class Contract {
  public:
    Contract() = default;
    Contract(std::string name, std::string symbol, std::uint8_t decimals, amount totalSupply) 
      : name_(std::move(name)), symbol_(std::move(symbol)), decimals_(decimals), totalSupply_(totalSupply) {}

    std::string name() const { return name_; }
    std::string symbol() const { return symbol_; } 
    std::uint8_t decimals() const { return decimals_; }
    amount totalSupply() const { return totalSupply_; }

    amount balanceOf(ipi::address addr) const { 
      const auto it = balances.find(addr);
      if(it == balances.end()) return 0;    
      return it->second;
    }

    amount allowance(ipi::address owner, ipi::address spender) const {
      const auto it = allowances.find(std::make_pair(owner, spender));
      if(it == allowances.end()) return 0;
      return it->second;
    }

    bool approve(ipi::address spender, amount value) {
      allowances[std::make_pair(ipi::sender(), spender)] = value;
      return true;
    }

    bool transfer(ipi::address to, amount value) {
      if(value == 0) return true;
      if(to == ipi::sender()) return true;
      if(value > balanceOf(ipi::sender())) return false;
      balances[ipi::sender()] -= value;
      balances[to] += value;
      return true;
    }

    bool transferFrom(ipi::address from, ipi::address to, amount value) {
      if(value == 0) return true;
      if(to == ipi::sender()) return true;
      if(value > allowance(from, ipi::sender())) throw std::runtime_error("allowance too low");
      if(value > balanceOf(from)) throw std::runtime_error("balance too low");
      balances[from] -= value;
      balances[to] += value;
      allowances[std::make_pair(from, to)] -= value;
      return true;
    } 

  private:
    std::string name_;
    std::string symbol_;
    std::uint8_t decimals_;
    amount totalSupply_;

    [[=serialization::policy::ignore]]
    int ignored_test;

    using Balances = ipi::index<ipi::address, amount>;
    using AllowancePair = std::pair<ipi::address, ipi::address>;
    using Allowances = ipi::index<AllowancePair, amount>;
    Balances balances;
    Allowances allowances;
};
