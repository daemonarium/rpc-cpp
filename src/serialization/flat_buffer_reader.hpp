#pragma once

#include "flat_reader_base.hpp"
#include "flat_encoding.hpp"
#include "flat_source.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace serialization {

  // Flat LE reader from a FlatSource (span or circular). All scalar/typed/
  // exception/message logic lives in flat_reader_base; this class provides
  // only the medium-specific hooks (read_raw) and frame management (different
  // Frame struct from flat_stream_reader — buffer reader tracks bounds and
  // cursor, uses cursor_ += to skip trailers).
  template <FlatSource Source = span_flat_source>
  class flat_buffer_reader : public flat_reader_base<flat_buffer_reader<Source>> {
    public:
      explicit flat_buffer_reader(Source source)
        : source_(std::move(source)), cursor_(0), frame_start_(0), frame_end_(source_.size()) {}

      flat_buffer_reader(Source source, std::size_t start, std::size_t end)
        : source_(std::move(source)), cursor_(start), frame_start_(start), frame_end_(end) {}

      consteval bool is_indexed() { return true; }

      void enter_frame() {
        frames_.push_back(Frame{ cursor_, frame_end_, 0, cursor_ });
      }

      void exit_frame() {
        if(frames_.empty()) {
          throw std::runtime_error("flat_buffer_reader::exit_frame without enter_frame");
        }
        Frame frame = frames_.back();
        frames_.pop_back();
        const std::size_t k = frame.checkpoint_count;
        if(k == 0) {
          return;
        }
        std::uint8_t format = 0;
        read_raw(&format, 1);
        const std::size_t width = flat_unpack_width(format);
        cursor_ += k * width;
        std::uint8_t format_end = 0;
        read_raw(&format_end, 1);
        if(format_end != format) {
          throw std::runtime_error("flat trailer format mismatch");
        }
        (void)frame;
      }

      void save_offset() {
        if(frames_.empty()) {
          return;
        }
        frames_.back().checkpoint_count++;
      }

      void enter_indexed_frame(size_t id) {
        throw std::runtime_error("enter_indexed_frame: use flat_view for field seek");
        (void)id;
      }

      template <class Map, class KeyCb, class ValCb>
      void read_map(Map& map, KeyCb&& key_cb, ValCb&& val_cb) {
        frames_.push_back(Frame{ cursor_, frame_end_, 0, cursor_ });
        std::uint32_t n = 0;
        this->read_u32(n);
        if(n == 0) {
          frames_.pop_back();
          return;
        }
        std::uint8_t format_k = 0;
        this->read_u8(format_k);
        const std::size_t wk = flat_unpack_width(format_k);
        cursor_ += (n + 1) * wk;

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
        cursor_ += n * wv;
        std::uint8_t format_v_end = 0;
        this->read_u8(format_v_end);
        if(format_v_end != format_v) {
          throw std::runtime_error("flat map value trailer format mismatch");
        }
        frames_.pop_back();
      }

      const Source& source() const { return source_; }
      std::size_t cursor() const { return cursor_; }

      // Advance the cursor by n bytes without reading (no copy). Used by
      // zero-copy dispatch to position the reader over the payload region
      // that follows the flat frame(s). Bounds-checked against source size.
      void skip(std::size_t n) {
        if(cursor_ + n > source_.size()) {
          throw std::runtime_error("flat_buffer_reader skip out of range");
        }
        cursor_ += n;
      }

      // Reset reader to the start of the current readable region for a new
      // message frame. Preserves registered deserialize functions. Called by
      // codec.reset_for_dispatch() between messages.
      void reset() {
        cursor_ = 0;
        frame_start_ = 0;
        frame_end_ = source_.size();
        frames_.clear();
        this->read_later_.clear();
      }

      // Raw byte read — public hook for flat_reader_base.
      void read_raw(void* dest, std::size_t size) {
        if(cursor_ + size > source_.size()) {
          throw std::runtime_error("flat_buffer_reader underflow");
        }
        if(source_.peek(cursor_, dest, size) != size) {
          throw std::runtime_error("flat_buffer_reader peek failed");
        }
        cursor_ += size;
      }

    private:
      struct Frame {
        std::size_t start;
        std::size_t end;
        std::size_t checkpoint_count;
        std::size_t cursor_at_enter;
      };

      Source source_;
      std::size_t cursor_;
      std::size_t frame_start_;
      std::size_t frame_end_;
      std::vector<Frame> frames_;
  };

} // namespace serialization
