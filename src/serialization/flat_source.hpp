#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

namespace serialization {

  template <typename S>
  concept FlatSource = requires(const S& s, std::size_t off, void* dst, std::size_t n) {
    { s.size() } -> std::convertible_to<std::size_t>;
    { s.peek(off, dst, n) } -> std::convertible_to<std::size_t>;
  };

  template <class S>
  inline constexpr bool flat_source_is_contiguous_v = false;

  class span_flat_source {
    public:
      span_flat_source() = default;
      explicit span_flat_source(std::span<const std::byte> data) : data_(data) {}
      explicit span_flat_source(std::span<const std::uint8_t> data)
        : data_(reinterpret_cast<const std::byte*>(data.data()), data.size()) {}
      explicit span_flat_source(const std::vector<std::uint8_t>& data)
        : data_(reinterpret_cast<const std::byte*>(data.data()), data.size()) {}
      explicit span_flat_source(const std::string& data)
        : data_(reinterpret_cast<const std::byte*>(data.data()), data.size()) {}

      std::size_t size() const { return data_.size(); }

      std::size_t peek(std::size_t offset, void* dest, std::size_t n) const {
        if(offset > data_.size()) return 0;
        const std::size_t available = data_.size() - offset;
        const std::size_t to_copy = n < available ? n : available;
        if(to_copy > 0) {
          std::memcpy(dest, data_.data() + offset, to_copy);
        }
        return to_copy;
      }

      std::span<const std::byte> bytes() const { return data_; }

      bool region_contiguous(std::size_t offset, std::size_t len) const {
        return offset + len <= data_.size();
      }

      const std::uint8_t* try_ptr(std::size_t offset, std::size_t len) const {
        if(offset + len > data_.size()) return nullptr;
        return reinterpret_cast<const std::uint8_t*>(data_.data() + offset);
      }

    private:
      std::span<const std::byte> data_;
  };

  template <>
  inline constexpr bool flat_source_is_contiguous_v<span_flat_source> = true;

  class circular_flat_source {
    public:
      circular_flat_source() = default;
      explicit circular_flat_source(struct circular_buffer* buffer) : buffer_(buffer) {}

      std::size_t size() const {
        return buffer_ ? circular_buffer_readable_bytes(buffer_) : 0;
      }

      std::size_t peek(std::size_t offset, void* dest, std::size_t n) const {
        if(!buffer_) return 0;
        return circular_buffer_peek(buffer_, offset, dest, n);
      }

      bool region_contiguous(std::size_t offset, std::size_t len) const {
        if(!buffer_) return false;
        return circular_buffer_region_contiguous(buffer_, offset, len) != 0;
      }

      const std::uint8_t* try_ptr(std::size_t offset, std::size_t len) const {
        if(!buffer_) return nullptr;
        const char* ptr = nullptr;
        if(circular_buffer_try_contiguous_ptr(buffer_, offset, len, &ptr) != len) {
          return nullptr;
        }
        return reinterpret_cast<const std::uint8_t*>(ptr);
      }

      int get_segments(std::size_t offset, std::size_t len,
          struct circular_buffer_segment out[2]) const {
        if(!buffer_ || !out) return 0;
        return circular_buffer_get_segments(buffer_, offset, len, out);
      }

      struct circular_buffer* buffer() const { return buffer_; }

    private:
      struct circular_buffer* buffer_ = nullptr;
  };

  template <class S>
  void flat_load_bytes(const S& source, std::size_t offset, void* dest, std::size_t n) {
    if(source.peek(offset, dest, n) != n) {
      throw std::runtime_error("flat_load_bytes: short read");
    }
  }

  inline std::vector<std::uint8_t> flat_peek_range(struct circular_buffer* buffer,
      std::size_t offset, std::size_t len) {
    std::vector<std::uint8_t> out(len);
    if(len == 0) return out;
    if(circular_buffer_peek(buffer, offset, out.data(), len) != len) {
      throw std::runtime_error("flat_peek_range: short read");
    }
    return out;
  }

} // namespace serialization
