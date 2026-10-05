#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

#include "examples/structures/db.hpp"
#include "observable/observable.hpp"
#include "observable/remote_observable.hpp"
#include "observable/stream_multiplexer.hpp"
#include "serialization/annotations.hpp"

class DbgForward {
  public:
    using ObservablePtr =
      std::shared_ptr<waos::observable::Observable<waos::observable::BinaryEntry>>;

    using MakeForward = std::function<ObservablePtr(
      std::uint32_t table_id,
      waos::binary lower_id, waos::db_bound_mode lower_mode,
      waos::binary upper_id, waos::db_bound_mode upper_mode,
      std::uint64_t limit, bool reverse, bool keys_only)>;

    // High-level observable: returns Observable, no stream_handle.
    // The framework attaches a live relay or one-shot scan from stream.flags.
    [[=serialization::observable()]]
    static ObservablePtr forward(
        std::uint32_t table_id,
        waos::binary lower_id, waos::db_bound_mode lower_mode,
        waos::binary upper_id, waos::db_bound_mode upper_mode,
        std::uint64_t limit, bool reverse, bool keys_only) {
      if(!make_) {
        throw std::runtime_error("DbgForward upstream is not bound");
      }
      return make_(
        table_id, std::move(lower_id), lower_mode, std::move(upper_id), upper_mode,
        limit, reverse, keys_only);
    }

    template <typename Starter, typename Channel>
    static void bind_upstream(
        waos::observable::StreamMultiplexer<Starter, Channel>& mux,
        std::uint64_t table_fn) {
      make_ = [&mux, table_fn](
          std::uint32_t table_id,
          waos::binary lower_id, waos::db_bound_mode lower_mode,
          waos::binary upper_id, waos::db_bound_mode upper_mode,
          std::uint64_t limit, bool reverse, bool keys_only) -> ObservablePtr {
        using Remote = waos::observable::RemoteObservable<
          Starter, Channel,
          std::uint32_t, waos::binary, waos::db_bound_mode, waos::binary,
          waos::db_bound_mode, std::uint64_t, bool, bool>;
        auto obs = std::make_shared<Remote>(
          mux, table_fn, table_id, std::move(lower_id), lower_mode,
          std::move(upper_id), upper_mode, limit, reverse, keys_only);
        obs->withFlags(waos::stream_flags::observe);
        return std::static_pointer_cast<
          waos::observable::Observable<waos::observable::BinaryEntry>>(obs);
      };
    }

    static void unbind_upstream() {
      make_ = nullptr;
    }

  private:
    inline static MakeForward make_;
};
