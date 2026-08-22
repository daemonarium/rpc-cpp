#pragma once

#include <cstdint>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "observable/remote_observable.hpp"
#include "observable/stream_multiplexer.hpp"
#include "observable/types.hpp"
#include "serialization/flat_bytes_writer.hpp"
#include "serialization/serialize.hpp"

namespace waos::observable {

  struct ObservationKey {
    std::uint64_t functionId = 0;
    binary argsBytes;

    bool operator==(const ObservationKey& o) const {
      return functionId == o.functionId && argsBytes == o.argsBytes;
    }
  };

  struct ObservationKeyHash {
    std::size_t operator()(const ObservationKey& k) const {
      std::size_t h = std::hash<std::uint64_t>{}(k.functionId);
      std::size_t h2 = 0;
      for(auto b : k.argsBytes) {
        h2 = h2 * 131u + static_cast<std::size_t>(b);
      }
      return h ^ (h2 << 1);
    }
  };

  template <typename... Args>
  binary serializeObservationArgs(const std::tuple<Args...>& args) {
    std::vector<std::uint8_t> out;
    serialization::flat_bytes_writer w(out);
    serialization::serialize(w, args);
    w.write_rest();
    return out;
  }

  // Owns RemoteObservables keyed by (function_id, serialized args) so they
  // survive idle (0 consumers). Resubscribe reuses the same object and
  // therefore maxTs / the retained view (delta resume). v1: no eviction.
  template <typename Starter, typename Channel = Starter>
  class RemoteRegistry {
    StreamMultiplexer<Starter, Channel>& mux_;
    std::unordered_map<ObservationKey, std::shared_ptr<void>, ObservationKeyHash> entries_;

  public:
    explicit RemoteRegistry(StreamMultiplexer<Starter, Channel>& mux)
      : mux_(mux) {}

    template <typename... Args>
    std::shared_ptr<RemoteObservable<Starter, Channel, Args...>> getOrCreate(
        std::uint64_t function_id, Args... args) {
      auto tup = std::make_tuple(args...);
      ObservationKey key{
        function_id,
        serializeObservationArgs(tup),
      };
      auto it = entries_.find(key);
      if(it != entries_.end()) {
        return std::static_pointer_cast<RemoteObservable<Starter, Channel, Args...>>(
          it->second);
      }
      auto obs = std::make_shared<RemoteObservable<Starter, Channel, Args...>>(
        mux_, function_id, std::move(args)...);
      entries_.emplace(std::move(key), obs);
      return obs;
    }

    std::size_t size() const noexcept { return entries_.size(); }
  };

}
