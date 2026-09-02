#pragma once

#include <concepts>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <vector>

#include "serialization/promise.hpp"

namespace waos::observable {

  using binary = std::vector<std::uint8_t>;

  // Empty value = tombstone = delete. Matches stream_item on the wire.
  struct BinaryEntry {
    binary key;
    binary value;
    std::uint64_t ts = 0;
  };

  struct Change {
    binary key;
    binary value;
    std::uint64_t ts = 0;
  };

  enum class ObservableState : std::uint8_t {
    idle,
    loading,
    ready,
    reloading,
    error,
    closed
  };

  // Consumer mirrors the wire signal set so relay consumers can forward
  // verbatim. onChange is async: a resolved promise is the sync path;
  // a pending promise is backpressure (await before the next change).
  // sinceTimestamp() seeds the library-owned replayWatermark at subscribe.
  template <typename C>
  concept Consumer = requires(C c, Change ch, std::exception_ptr ex) {
    { c.onChange(ch) } -> std::convertible_to<std::shared_ptr<serialization::promise<void>>>;
    { c.onReady() } -> std::same_as<void>;
    { c.onReset() } -> std::same_as<void>;
    { c.onError(ex) } -> std::same_as<void>;
    { c.onClosed() } -> std::same_as<void>;
    { c.sinceTimestamp() } -> std::convertible_to<std::uint64_t>;
  };

  struct DispatchCallbacks {
    std::function<std::shared_ptr<serialization::promise<void>>(Change)> onChange;
    std::function<void()> onReady;
    std::function<void()> onReset;
    std::function<void(std::exception_ptr)> onError;
    std::function<void()> onClosed;
  };

}
