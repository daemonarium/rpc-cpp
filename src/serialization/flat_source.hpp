#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace serialization {

  template <typename S>
  concept FlatSource = requires(const S& s, std::size_t off, void* dst, std::size_t n) {
    { s.size() } -> std::convertible_to<std::size_t>;
    { s.peek(off, dst, n) } -> std::convertible_to<std::size_t>;
  };

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

      // Future: circular_buffer_flat_source using circular_buffer_peek.

    private:
      std::span<const std::byte> data_;
  };

} // namespace serialization
