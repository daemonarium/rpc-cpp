#pragma once

#include "reader.hpp"
#include "deserialize.hpp"
#include "flat_encoding.hpp"
#include "flat_source.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace serialization {

  template <FlatSource Source = span_flat_source>
  class flat_buffer_reader : public reader {
    public:
      explicit flat_buffer_reader(Source source)
        : source_(std::move(source)), cursor_(0), frame_start_(0), frame_end_(source_.size()) {}

      flat_buffer_reader(Source source, std::size_t start, std::size_t end)
        : source_(std::move(source)), cursor_(start), frame_start_(start), frame_end_(end) {}

      void set_skip_pointers(bool skip) { skip_pointers_ = skip; }

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

      void read_u64(std::uint64_t& value) { value = read_le_value<std::uint64_t>(); }
      void read_i64(std::int64_t& value) { value = read_le_value<std::int64_t>(); }
      void read_u32(std::uint32_t& value) { value = read_le_value<std::uint32_t>(); }
      void read_i32(std::int32_t& value) { value = read_le_value<std::int32_t>(); }
      void read_u16(std::uint16_t& value) { value = read_le_value<std::uint16_t>(); }
      void read_i16(std::int16_t& value) { value = read_le_value<std::int16_t>(); }
      void read_u8(std::uint8_t& value) { read_raw(&value, 1); }
      void read_i8(std::int8_t& value) { read_raw(&value, 1); }

      void read_f64(double& value) {
        std::uint8_t buf[8];
        read_raw(buf, 8);
        value = flat_load_le<double>(buf);
      }

      void read_f32(float& value) {
        std::uint8_t buf[4];
        read_raw(buf, 4);
        value = flat_load_le<float>(buf);
      }

      void read_bool(bool& value) {
        std::uint8_t byte = 0;
        read_raw(&byte, 1);
        value = byte != 0;
      }

      void read_string(std::string& value) {
        std::uint32_t length = 0;
        read_u32(length);
        value.resize(length);
        if(length > 0) {
          read_raw(value.data(), length);
        }
      }

      void read_binary(std::vector<std::uint8_t>& value) {
        std::uint32_t length = 0;
        read_u32(length);
        value.resize(length);
        if(length > 0) {
          read_raw(value.data(), length);
        }
      }

      void read_length(std::size_t& value) {
        std::uint32_t length = 0;
        read_u32(length);
        value = length;
      }

      template <class Map, class KeyCb, class ValCb>
      void read_map(Map& map, KeyCb&& key_cb, ValCb&& val_cb) {
        frames_.push_back(Frame{ cursor_, frame_end_, 0, cursor_ });
        std::uint32_t n = 0;
        read_u32(n);
        if(n == 0) {
          frames_.pop_back();
          return;
        }
        std::uint8_t format_k = 0;
        read_u8(format_k);
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
        read_u8(format_v);
        const std::size_t wv = flat_unpack_width(format_v);
        cursor_ += n * wv;
        std::uint8_t format_v_end = 0;
        read_u8(format_v_end);
        if(format_v_end != format_v) {
          throw std::runtime_error("flat map value trailer format mismatch");
        }
        frames_.pop_back();
      }

      template <typename T>
      void read_typed(std::shared_ptr<T>& data) {
        std::uint32_t index = 0;
        read_u32(index);
        if(index >= deserialize_typed_functions_.size()) {
          throw std::runtime_error("typed index out of range");
        }
        void* ptr = &data;
        deserialize_typed_functions_[index](ptr);
      }

      void read_typed(std::weak_ptr<void>& data) {
        std::shared_ptr<void> shared;
        read_typed(shared);
        data = shared;
      }

      void register_deserialize_typed_function(std::type_index type,
          std::function<void(void*)> deserialize_fn) {
        typed_indexes_[type] = deserialize_typed_functions_.size();
        deserialize_typed_functions_.push_back(std::move(deserialize_fn));
      }

      template <typename T>
      void register_deserialize_typed_function() {
        auto type = std::type_index(typeid(T));
        register_deserialize_typed_function(type, [&reader = *this](void* ptr) {
          std::shared_ptr<T>& shared = *((std::shared_ptr<T>*)ptr);
          std::shared_ptr<T> data = std::make_shared<T>();
          deserialize(reader, *data);
          shared = data;
        });
      }

      template <typename T>
      void read_pointer(std::shared_ptr<T>& value, ReadLater read_later) {
        std::int32_t delta = 0;
        read_i32(delta);
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

      void read_rest() {
        while(!read_later_.empty()) {
          auto pair = read_later_.front();
          std::shared_ptr<void> result;
          pair.second(result);
          for(auto& assign : pair.first) {
            assign(result);
          }
          read_pointers_.push_back(result);
          read_later_.erase(read_later_.begin());
        }
      }

      const Source& source() const { return source_; }
      std::size_t cursor() const { return cursor_; }

    private:
      struct Frame {
        std::size_t start;
        std::size_t end;
        std::size_t checkpoint_count;
        std::size_t cursor_at_enter;
      };

      using AssignAfterRead = std::function<void(std::shared_ptr<void>&)>;

      Source source_;
      std::size_t cursor_;
      std::size_t frame_start_;
      std::size_t frame_end_;
      std::vector<Frame> frames_;
      bool skip_pointers_ = false;

      std::vector<std::shared_ptr<void>> read_pointers_;
      std::vector<std::pair<std::vector<AssignAfterRead>, ReadLater>> read_later_;

      std::vector<std::function<void(void*)>> deserialize_typed_functions_;
      std::unordered_map<std::type_index, size_t> typed_indexes_;

      void read_raw(void* dest, std::size_t size) {
        if(cursor_ + size > source_.size()) {
          throw std::runtime_error("flat_buffer_reader underflow");
        }
        if(source_.peek(cursor_, dest, size) != size) {
          throw std::runtime_error("flat_buffer_reader peek failed");
        }
        cursor_ += size;
      }

      template <typename T>
      T read_le_value() {
        std::uint8_t buf[sizeof(T)];
        read_raw(buf, sizeof(T));
        return flat_load_le<T>(buf);
      }
  };

} // namespace serialization
