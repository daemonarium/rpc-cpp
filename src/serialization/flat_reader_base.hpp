#pragma once

#include "reader.hpp"
#include "deserialize.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace serialization {

  // CRTP base for flat readers. Factors out the shared_ptr graph machinery
  // duplicated between flat_stream_reader and flat_buffer_reader: the
  // read_pointers_/read_later_ tables, skip_pointers_ flag, mark_read_pointer,
  // make_shared_read_later<T>, read_pointer<T> (+weak), and the per-frame
  // size prefix consumption loop in read_rest.
  //
  // Derived classes must provide read_u32 / read_i32 (already in concrete
  // readers) and keep their own read_raw / enter_frame / exit_frame / typed /
  // exception / message machinery (not shared here).
  template <typename Derived>
  class flat_reader_base : public reader {
    public:
      using AssignAfterRead = std::function<void(std::shared_ptr<void>&)>;

      void set_skip_pointers(bool skip) { skip_pointers_ = skip; }

      // Pre-register a deserialized object as already-read so that back-
      // references (negative deltas) to it resolve correctly. Used by the
      // shared-graph root path: the root is deref-deserialized (no
      // read_pointer) but must be discoverable by read_pointer for
      // back-references.
      void mark_read_pointer(std::shared_ptr<void> value) {
        read_pointers_.push_back(std::move(value));
      }

      // Flat graphs are homogeneous (all nodes type T): sub-frames carry no
      // per-frame type index, so the deferred callback deserializes the
      // pointee directly via the compile-time element type.
      template <typename T>
      ReadLater make_shared_read_later() {
        return [this](std::shared_ptr<void>& data) {
          auto typed = std::make_shared<T>();
          deserialize(this->derived(), *typed);
          data = typed;
        };
      }

      // Flush all deferred sub-frames. Each consumes the per-frame size
      // prefix (encoded = real_size + 1) written by the writer's write_rest.
      // The materialized reader reads fields sequentially, so the size itself
      // is not needed — only consumed to keep the cursor aligned with the
      // sub-frame body.
      void read_rest() {
        while(!read_later_.empty()) {
          // Move callback out; do not keep references across it — read_typed
          // may push_back new later-entries and reallocate. Self-refs append
          // assigns onto front().first during the callback; collect them
          // afterwards.
          ReadLater callbacks = std::move(read_later_.front().second);
          std::uint32_t encoded_size = 0;
          derived().read_u32(encoded_size);
          (void)encoded_size;
          std::shared_ptr<void> result;
          callbacks(result);
          auto assigns = std::move(read_later_.front().first);
          for(auto& assign : assigns) {
            assign(result);
          }
          read_pointers_.push_back(std::move(result));
          read_later_.erase(read_later_.begin());
        }
      }

      template <typename T>
      void read_pointer(std::shared_ptr<T>& value, ReadLater read_later) {
        std::int32_t delta = 0;
        derived().read_i32(delta);
        if(skip_pointers_) {
          value = nullptr;
          return;
        }
        if(delta == 0) {
          value = nullptr;
          return;
        }
        if(delta < 0) {
          size_t index = static_cast<size_t>(static_cast<std::ptrdiff_t>(read_pointers_.size()) + delta);
          value = std::static_pointer_cast<T>(read_pointers_[index]);
          return;
        }
        AssignAfterRead assign = [&value](std::shared_ptr<void>& data) {
          value = std::static_pointer_cast<T>(data);
        };
        delta--;
        if(static_cast<size_t>(delta) < read_later_.size()) {
          read_later_[static_cast<size_t>(delta)].first.push_back(assign);
          return;
        }
        if(static_cast<size_t>(delta) > read_later_.size()) {
          throw std::runtime_error("Pointer delta too large, input is corrupted");
        }
        read_later_.push_back({std::vector<AssignAfterRead>{assign}, read_later});
      }

      template <typename T>
      void read_pointer(std::weak_ptr<T>& value, ReadLater read_later) {
        std::shared_ptr<T> shared;
        read_pointer(shared, read_later);
        value = shared;
      }

    protected:
      Derived& derived() noexcept { return static_cast<Derived&>(*this); }

      std::vector<std::shared_ptr<void>> read_pointers_;
      std::vector<std::pair<std::vector<AssignAfterRead>, ReadLater>> read_later_;
      bool skip_pointers_ = false;
  };

} // namespace serialization
