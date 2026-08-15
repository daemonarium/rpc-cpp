#pragma once

#include <cstdint>
#include <memory>
#include <serialization/promise.hpp>

struct bench_result {
  std::uint64_t keys = 0;
  std::uint64_t ms = 0;
  std::uint64_t keys_per_s = 0;
};

/** USER RPC: time native DB get/scan from inside this WASM module. */
class DbBenchClient {
  public:
    static std::shared_ptr<serialization::promise<bench_result>> bench_get(std::uint32_t n);
    static std::shared_ptr<serialization::promise<bench_result>> bench_scan();
};
