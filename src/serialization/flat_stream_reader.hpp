#pragma once

#include "flat_reader_base.hpp"
#include "flat_encoding.hpp"

#include <cstdint>
#include <istream>
#include <stdexcept>
#include <vector>

namespace serialization {

  // Flat LE reader from std::istream. All scalar/typed/exception/message
  // logic lives in flat_reader_base; this class provides only the
  // medium-specific hooks (read_raw) and frame management (different Frame
  // struct from flat_buffer_reader — stream reader only tracks checkpoint
  // count and uses stream_.ignore to skip trailers).
  class flat_stream_reader : public flat_reader_base<flat_stream_reader> {
    public:
      explicit flat_stream_reader(std::istream& stream) : stream_(stream) {}

      consteval bool is_indexed() { return false; }

      void enter_frame() {
        frames_.push_back(Frame{ 0 });
      }

      void exit_frame() {
        if(frames_.empty()) {
          throw std::runtime_error("flat_stream_reader::exit_frame without enter_frame");
        }
        const std::size_t k = frames_.back().checkpoint_count;
        frames_.pop_back();
        if(k == 0) {
          return;
        }
        std::uint8_t format = 0;
        read_raw(&format, 1);
        const std::size_t width = flat_unpack_width(format);
        stream_.ignore(static_cast<std::streamsize>(k * width));
        std::uint8_t format_end = 0;
        read_raw(&format_end, 1);
        if(format_end != format) {
          throw std::runtime_error("flat trailer format mismatch");
        }
      }

      void save_offset() {
        if(frames_.empty()) {
          return;
        }
        frames_.back().checkpoint_count++;
      }

      void enter_indexed_frame(size_t) {
        throw std::runtime_error("flat_stream_reader is not indexed");
      }

      template <class Map, class KeyCb, class ValCb>
      void read_map(Map& map, KeyCb&& key_cb, ValCb&& val_cb) {
        frames_.push_back(Frame{ 0 });
        std::uint32_t n = 0;
        this->read_u32(n);
        if(n == 0) {
          frames_.pop_back();
          return;
        }
        std::uint8_t format_k = 0;
        this->read_u8(format_k);
        const std::size_t wk = flat_unpack_width(format_k);
        stream_.ignore(static_cast<std::streamsize>((n + 1) * wk));

        std::vector<typename Map::key_type> keys;
        keys.reserve(n);
        for(std::uint32_t i = 0; i < n; ++i) {
          typename Map::key_type key{};
          key_cb(*this, key);
          keys.push_back(std::move(key));
        }
        for(std::uint32_t i = 0; i < n; ++i) {
          typename Map::mapped_type item{};
          val_cb(*this, item);
          map.insert(std::make_pair(std::move(keys[i]), std::move(item)));
        }
        std::uint8_t format_v = 0;
        this->read_u8(format_v);
        const std::size_t wv = flat_unpack_width(format_v);
        stream_.ignore(static_cast<std::streamsize>(n * wv));
        std::uint8_t format_v_end = 0;
        this->read_u8(format_v_end);
        if(format_v_end != format_v) {
          throw std::runtime_error("flat map value trailer format mismatch");
        }
        frames_.pop_back();
      }

      // Raw byte read — public hook for flat_reader_base.
      void read_raw(void* dest, std::size_t size) {
        stream_.read(static_cast<char*>(dest), static_cast<std::streamsize>(size));
        if(static_cast<std::size_t>(stream_.gcount()) != size) {
          throw std::runtime_error("flat_stream_reader underflow");
        }
      }

    private:
      struct Frame {
        std::size_t checkpoint_count;
      };

      std::istream& stream_;
      std::vector<Frame> frames_;
  };

} // namespace serialization
