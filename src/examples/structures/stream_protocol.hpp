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
  // A client starts a stream (observe and other streams; DB start_scan is a
  // separate fast path that does not emit per-scan stream_opened) with a
  // stream_handle. The producer emits items on the private channel until
  // the stream ends:
  //
  //   start -> stream_opened{id} -> stream_item | stream_key
  //         -> stream_done | stream_error | stream_reset
  //
  // Items must not be queued on that stream before stream_opened{id}.
  // stream_cancel is valid after opened; the producer also accepts cancel
  // before attach via a pending set.
  //
  // socket_opened is not part of this lifecycle. It is the ack that a
  // private channel is bound (open_socket). It has no stream_id.
  //
  // since_timestamp (on stream_handle) is the catch-up window, not lifecycle:
  //   * 0  = full snapshot: current state in range (every key once).
  //   * >0 = delta: entries with timestamp > since_timestamp. Tombstones are
  //          empty values and mean delete. Applies to any observation-based
  //          stream, not only DB scan.
  //
  // Lifecycle is stream_handle.flags (see stream_flags). Catch-up and
  // observe-vs-scan are orthogonal: an observe stream can still request a
  // delta window, and a scan can request a full snapshot.
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

  // Lifecycle bits on stream_handle.flags. Orthogonal to since_timestamp.
  // none    = finite scan: producer ends after catch-up (stream_done closes).
  // observe = live stream: stream_done is end of catch-up; the stream stays
  //           open until stream_cancel / error.
  enum class stream_flags : std::uint8_t {
    none = 0,
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
    // Catch-up window. 0 = full snapshot; >0 = delta (ts > since_timestamp).
    std::uint64_t since_timestamp = 0;
    // Lifecycle (stream_flags). Not a catch-up window.
    std::uint8_t flags = 0;
  };

  // Ack that the private channel is bound (open_socket). Not per-stream.
  // Occupies the historical empty stream_opened type-index slot.
  struct [[=serialization::message()]] socket_opened {
  };

  struct [[=serialization::message()]] stream_item {
    std::uint64_t stream_id = 0;
    binary key;
    binary value;
    std::uint64_t timestamp = 0;
  };

  // keys_only live key. timestamp is last-ts-wins.
  // Tombstone in keys_only observe is stream_item with empty value, not stream_key.
  struct [[=serialization::message()]] stream_key {
    std::uint64_t stream_id = 0;
    binary key;
    std::uint64_t timestamp = 0;
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

  // Consumer -> producer: tear down the stream with this id. Fire-and-forget.
  // Producer no-ops if the id is unknown or already ended.
  struct [[=serialization::message()]] stream_cancel {
    std::uint64_t stream_id = 0;
  };

  // Producer has this id in its map; client may cancel / use the stream.
  // Registered last so existing type indexes do not shift.
  struct [[=serialization::message()]] stream_opened {
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
