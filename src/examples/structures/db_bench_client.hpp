#pragma once

#include <cstdint>
#include <memory>
#include <serialization/promise.hpp>
#include "db.hpp"

struct bench_result {
  std::uint64_t keys = 0;
  std::uint64_t ms = 0;
  std::uint64_t keys_per_s = 0;
};

/** USER RPC: drive a DB (native or kv-storage-mem) from inside this WASM module. */
class DbBenchClient {
  public:
    static std::shared_ptr<serialization::promise<bench_result>> bench_get(std::uint32_t n);
    static std::shared_ptr<serialization::promise<bench_result>> bench_scan();
    static std::shared_ptr<serialization::promise<void>> connect();
    static std::shared_ptr<serialization::promise<void>> set(
        waos::binary key, waos::binary value, std::uint64_t timestamp);
    static std::shared_ptr<serialization::promise<waos::db_get_result>> get(waos::binary key);
    static std::shared_ptr<serialization::promise<std::uint64_t>> scan_collect();
};
