#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "serialization/annotations.hpp"
#include "serialization/promise.hpp"

namespace waos {

  using binary = std::vector<std::uint8_t>;

  // ---------------------------------------------------------------------------
  // Observation / stream contract
  //
  // A client starts a stream (DB start_scan today; observe and other streams
  // later) with a stream_handle. The server emits items on the private channel
  // until the stream ends:
  //
  //   start -> stream_opened -> stream_item | stream_key
  //         -> stream_done | stream_error | stream_reset
  //
  // since_timestamp (on stream_handle) is the observation watermark:
  //   * 0  = full stream: current state in range (every key once).
  //   * >0 = delta: entries with timestamp > since_timestamp. Tombstones are
  //          empty values and mean delete. Applies to any observation-based
  //          stream, not only DB scan.
  //
  // Delta safety is observer-specific. The DB gate-checks
  //   since_timestamp >= max_elided_tombstone_ts
  //   AND since_timestamp >= min_retention_ts
  // and falls back to a full stream when either check fails. Other observers
  // may use their own rules; 0 always means full.
  //
  // stream_reset: the server invalidated a delta mid-flight (DB: compaction
  // elided a tombstone with ts > since_timestamp). The server then restarts as
  // a full stream on the same stream_id. The client MUST:
  //   1. discard delta items received before stream_reset
  //   2. apply the following full-stream items (last-ts-wins)
  //   3. reconcile: keys in client state that are in the stream range but
  //      absent from the full stream are deleted
  //
  // Empty value = tombstone = delete. Client applies last-ts-wins.
  // ---------------------------------------------------------------------------

  enum class stream_mode : std::uint8_t {
    scan = 0,
    observe = 1,
  };

  struct channel_handle {
    std::uint64_t pid = 0;
    std::uint32_t socket = 0;
  };

  struct stream_handle {
    std::uint64_t pid = 0;
    std::uint32_t socket = 0;
    std::uint64_t stream_id = 0;
    // 0 = full stream; >0 = delta (changes with timestamp > since_timestamp).
    std::uint64_t since_timestamp = 0;
  };

  struct [[=serialization::message()]] stream_opened {
  };

  struct [[=serialization::message()]] stream_item {
    std::uint64_t stream_id = 0;
    binary key;
    binary value;
    std::uint64_t timestamp = 0;
  };

  struct [[=serialization::message()]] stream_key {
    std::uint64_t stream_id = 0;
    binary key;
  };

  struct [[=serialization::message()]] stream_done {
    std::uint64_t stream_id = 0;
  };

  struct [[=serialization::message()]] stream_error {
    std::uint64_t stream_id = 0;
    std::string message;
  };

  // Delta invalidated mid-flight; server restarts as a full stream on this id.
  struct [[=serialization::message()]] stream_reset {
    std::uint64_t stream_id = 0;
  };

  // Observation phase messages — defined for the standard, not yet registered by DB.
  struct [[=serialization::message()]] stream_observe_begin {
    std::uint64_t stream_id = 0;
  };

  struct [[=serialization::message()]] stream_retransmit {
    std::uint64_t stream_id = 0;
  };

}
