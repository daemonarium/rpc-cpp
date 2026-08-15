#pragma once

#include "flat_writer_base.hpp"
#include "flat_encoding.hpp"

#include <cstdint>
#include <ostream>

namespace serialization {

  // Flat LE writer into std::ostream. All scalar/frame/typed/exception/message
  // logic lives in flat_writer_base; this class provides only the
  // medium-specific hooks (current_pos, poke_u32, write_raw).
  class flat_stream_writer : public flat_writer_base<flat_stream_writer> {
    public:
      explicit flat_stream_writer(std::ostream& stream) : stream_(stream) {}

      std::size_t position() const { return pos_; }
      std::size_t current_pos() const { return pos_; }

      // Back-patch a u32 LE value at a previously reserved stream position.
      void poke_u32(std::size_t abs_offset, std::uint32_t value) {
        std::uint8_t buf[4];
        flat_store_le(buf, value);
        std::streampos saved = stream_.tellp();
        stream_.seekp(static_cast<std::streamoff>(abs_offset));
        stream_.write(reinterpret_cast<const char*>(buf), 4);
        stream_.seekp(saved);
      }

      // Raw byte write — public hook for flat_writer_base.
      void write_raw(const void* data, std::size_t size) {
        stream_.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        pos_ += size;
      }

    private:
      std::ostream& stream_;
      std::size_t pos_ = 0;
  };

} // namespace serialization
