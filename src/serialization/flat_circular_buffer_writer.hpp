#pragma once

#include "flat_writer_base.hpp"
#include "flat_encoding.hpp"

#include <cstdint>
#include <stdexcept>

extern "C" {
#include <io/circular_buffer.h>
}

namespace serialization {

  // Flat LE writer into circular_buffer via poke (no auto-commit).
  // Caller commits with circular_buffer_commit_write(buffer, writer.write_offset_).
  // All scalar/frame/typed/exception/message logic lives in flat_writer_base;
  // this class provides only the medium-specific hooks.
  class flat_circular_buffer_writer : public flat_writer_base<flat_circular_buffer_writer> {
    public:
      explicit flat_circular_buffer_writer(struct circular_buffer* buffer, size_t write_offset = 0)
        : buffer_(buffer), write_offset_(write_offset) {
        if(!buffer_) {
          throw std::runtime_error("flat_circular_buffer_writer: null buffer");
        }
        if(write_offset > circular_buffer_writable_bytes(buffer_)) {
          throw std::runtime_error("flat_circular_buffer_writer: write offset exceeds writable");
        }
      }

      struct circular_buffer* buffer_ = nullptr;
      size_t write_offset_ = 0;

      std::size_t current_pos() const { return write_offset_; }

      // Back-patch a u32 LE value at a previously reserved writer position.
      void poke_u32(std::size_t abs_offset, std::uint32_t value) {
        circular_buffer_poke(buffer_, abs_offset, &value, sizeof(value));
      }

      // Raw byte write — public hook for flat_writer_base.
      void write_raw(const void* data, std::size_t size) {
        if(size == 0) {
          return;
        }
        ensure_writable(size);
        size_t written = circular_buffer_poke(buffer_, write_offset_, data, size);
        if(written != size) {
          throw std::runtime_error("flat_circular_buffer_writer: short poke");
        }
        write_offset_ += written;
      }

    private:
      void ensure_writable(size_t bytes) {
        size_t available = circular_buffer_reserve_for_writing(buffer_, bytes + write_offset_);
        if(available < bytes) {
          throw std::runtime_error("flat_circular_buffer_writer: not enough space");
        }
      }
  };

} // namespace serialization
