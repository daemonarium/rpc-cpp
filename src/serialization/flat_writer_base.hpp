#pragma once

#include "writer.hpp"
#include "serialize.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace serialization {

  // CRTP base for flat writers. Factors out the shared_ptr graph machinery
  // duplicated between flat_stream_writer and flat_circular_buffer_writer:
  // the written_pointers_/write_later_ tables, mark_written_pointer,
  // make_shared_write_later<T>, write_pointer<T> (+weak), and the per-frame
  // size prefix loop in write_rest.
  //
  // Derived classes must provide hooks:
  //   - std::size_t current_pos() const        — current write position
  //   - void poke_u32(abs, val)                 — back-patch a u32 LE
  //   - write_u32 / write_i32 (already in concrete writers)
  // and keep their own write_raw / begin_frame / end_frame / typed / exception
  // / message machinery (not shared here).
  template <typename Derived>
  class flat_writer_base : public writer {
    public:
      // Pre-register a shared_ptr as already-written so that self-references
      // (and any later references) to it use negative back-reference deltas
      // instead of creating duplicate sub-frames. Used by the shared-graph
      // root path: the root is deref-serialized (no write_pointer) but must
      // still be discoverable by write_pointer for back-references.
      void mark_written_pointer(std::shared_ptr<void> value) {
        written_pointers_.push_back(std::move(value));
      }

      // Flat graphs are homogeneous: sub-frames carry no per-frame type index.
      // The deferred callback serializes the pointee directly.
      template <typename T>
      WriteLater make_shared_write_later(std::shared_ptr<T> value) {
        return [value, this]() { serialize(this->derived(), *value); };
      }

      // Flush all deferred sub-frames. Each is wrapped with a per-frame size
      // prefix (encoded_size = real_size + 1; 0 reserved as sentinel) written
      // before the body and poked after the body is complete.
      void write_rest() {
        while(!write_later_.empty()) {
          auto pair = write_later_.front();
          std::size_t size_field = derived().current_pos();
          derived().write_u32(0);
          std::size_t frame_start = derived().current_pos();
          pair.second();
          std::uint32_t real_size =
            static_cast<std::uint32_t>(derived().current_pos() - frame_start);
          std::uint32_t encoded_size = real_size + 1;
          derived().poke_u32(size_field, encoded_size);
          written_pointers_.push_back(pair.first);
          write_later_.erase(write_later_.begin());
        }
      }

      template <typename T>
      void write_pointer(std::shared_ptr<T> value, WriteLater write_later) {
        if(!value) {
          derived().write_i32(0);
          return;
        }
        auto existing = std::find(written_pointers_.begin(), written_pointers_.end(), value);
        if(existing != written_pointers_.end()) {
          std::int32_t index = static_cast<std::int32_t>(std::distance(written_pointers_.begin(), existing));
          std::int32_t delta = index - static_cast<std::int32_t>(written_pointers_.size());
          derived().write_i32(delta);
          return;
        }
        auto will_be_written = std::find_if(write_later_.begin(), write_later_.end(),
          [value](const auto& pr) { return pr.first == value; });
        if(will_be_written != write_later_.end()) {
          std::int32_t index = static_cast<std::int32_t>(std::distance(write_later_.begin(), will_be_written));
          derived().write_i32(index + 1);
          return;
        }
        std::int32_t index = static_cast<std::int32_t>(write_later_.size());
        derived().write_i32(index + 1);
        write_later_.push_back({value, write_later});
      }

      template <typename T>
      void write_pointer(std::weak_ptr<T> value, WriteLater write_later) {
        write_pointer(value.lock(), write_later);
      }

    protected:
      Derived& derived() noexcept { return static_cast<Derived&>(*this); }

      std::vector<std::shared_ptr<void>> written_pointers_;
      std::vector<std::pair<std::shared_ptr<void>, WriteLater>> write_later_;
  };

} // namespace serialization
