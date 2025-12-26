#pragma once

#include <map>
#include <string>
#include "serialization/annotations.hpp"

namespace waos {
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
  { T::balanceOf(waos::address()) } -> std::same_as<amount>;
  { T::allowance(waos::address(), waos::address()) } -> std::same_as<amount>;
  { T::transfer(waos::address(), amount()) } -> std::same_as<bool>;
  { T::approve(waos::address(), amount()) } -> std::same_as<bool>;
  { T::transferFrom(waos::address(), waos::address(), amount()) } -> std::same_as<bool>;  
};

class Contract {
  public:
    Contract() = default;
    Contract(std::string name, std::string symbol, std::uint8_t decimals, amount totalSupply) 
      : name_(std::move(name)), symbol_(std::move(symbol)), decimals_(decimals), totalSupply_(totalSupply) {}

    [[=serialization::read()]]
    std::string name() const { return name_; }

    [[=serialization::read()]]
    std::string symbol() const { return symbol_; } 

    [[=serialization::read()]]
    std::uint8_t decimals() const { return decimals_; }

    [[=serialization::read()]]
    amount totalSupply() const { return totalSupply_; }

    [[=serialization::read()]]
    amount balanceOf(waos::address addr) const { 
      const auto it = balances.find(addr);
      if(it == balances.end()) return 0;    
      return it->second;
    }

    [[=serialization::read()]]
    amount allowance(waos::address owner, waos::address spender) const {
      const auto it = allowances.find(std::make_pair(owner, spender));
      if(it == allowances.end()) return 0;
      return it->second;
    }

    bool approve(waos::address spender, amount value) {
      allowances[std::make_pair(waos::sender(), spender)] = value;
      return true;
    }

    bool transfer(waos::address to, amount value) {
      if(value == 0) return true;
      if(to == waos::sender()) return true;
      if(value > balanceOf(waos::sender())) return false;
      balances[waos::sender()] -= value;
      balances[to] += value;
      return true;
    }

    bool transferFrom(waos::address from, waos::address to, amount value) {
      if(value == 0) return true;
      if(to == waos::sender()) return true;
      if(value > allowance(from, waos::sender())) throw std::runtime_error("allowance too low");
      if(value > balanceOf(from)) throw std::runtime_error("balance too low");
      balances[from] -= value;
      balances[to] += value;
      allowances[std::make_pair(from, to)] -= value;
      return true;
    } 

    [[=serialization::policy::ignore]]
    static Contract& get_rpc_instance();

  private:
    std::string name_;
    std::string symbol_;
    std::uint8_t decimals_;
    amount totalSupply_;

    using Balances = waos::index<waos::address, amount>;
    using AllowancePair = std::pair<waos::address, waos::address>;
    using Allowances = waos::index<AllowancePair, amount>;
    Balances balances;
    Allowances allowances;

    static std::shared_ptr<Contract> instance_;
};

std::shared_ptr<Contract> Contract::instance_ = nullptr;

Contract& Contract::get_rpc_instance() {
  if(instance_ == nullptr) {
    instance_ = std::make_shared<Contract>("Waos Token", "WAOS", 6, 21000000000000);
  }
  return *instance_;
}
