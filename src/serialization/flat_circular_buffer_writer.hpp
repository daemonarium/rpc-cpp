#pragma once

#include "flat_writer_base.hpp"
#include "flat_encoding.hpp"
#include "flat_bytes_writer.hpp"
#include "serialize.hpp"
#include "serialization/rpc.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

namespace serialization {

  // Flat LE writer into circular_buffer via poke (no auto-commit).
  // Caller commits with circular_buffer_commit_write(buffer, writer.write_offset_).
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

      void begin_frame() {
        frames_.push_back(Frame{ write_offset_, {} });
      }

      void end_frame() {
        if(frames_.empty()) {
          throw std::runtime_error("flat_circular_buffer_writer::end_frame without begin_frame");
        }
        Frame frame = std::move(frames_.back());
        frames_.pop_back();
        if(frame.cps.empty()) {
          return;
        }
        std::uint32_t max_cp = 0;
        for(std::uint32_t cp : frame.cps) {
          if(cp > max_cp) max_cp = cp;
        }
        const std::size_t width = flat_offset_width(max_cp);
        const std::uint8_t format = flat_pack_format(width);
        write_raw(&format, 1);
        for(std::uint32_t cp : frame.cps) {
          std::uint8_t buf[4];
          flat_store_cp(buf, cp, width);
          write_raw(buf, width);
        }
        write_raw(&format, 1);
      }

      void save_offset() {
        if(frames_.empty()) {
          return;
        }
        Frame& frame = frames_.back();
        frame.cps.push_back(static_cast<std::uint32_t>(write_offset_ - frame.start));
      }

      void write_u64(std::uint64_t value) { write_le_value(value); }
      void write_i64(std::int64_t value) { write_le_value(value); }
      void write_u32(std::uint32_t value) { write_le_value(value); }
      void write_i32(std::int32_t value) { write_le_value(value); }
      void write_u16(std::uint16_t value) { write_le_value(value); }
      void write_i16(std::int16_t value) { write_le_value(value); }
      void write_u8(std::uint8_t value) { write_raw(&value, 1); }
      void write_i8(std::int8_t value) { write_raw(&value, 1); }

      void write_f64(double value) {
        std::uint8_t buf[8];
        flat_store_le(buf, value);
        write_raw(buf, 8);
      }

      void write_f32(float value) {
        std::uint8_t buf[4];
        flat_store_le(buf, value);
        write_raw(buf, 4);
      }

      void write_bool(bool value) {
        std::uint8_t number = value ? 1 : 0;
        write_raw(&number, 1);
      }

      void write_string(const std::string_view value) {
        write_u32(static_cast<std::uint32_t>(value.size()));
        if(!value.empty()) {
          write_raw(value.data(), value.size());
        }
      }

      void write_binary(const std::vector<std::uint8_t>&& value) {
        write_u32(static_cast<std::uint32_t>(value.size()));
        if(!value.empty()) {
          write_raw(value.data(), value.size());
        }
      }

      void write_length(std::size_t length) {
        write_u32(static_cast<std::uint32_t>(length));
      }

      template <class Map, class KeyCb, class ValCb>
      void write_map(const Map& map, KeyCb&& key_cb, ValCb&& val_cb) {
        std::vector<std::uint8_t> payload;
        flat_append_map_soa(payload, map, std::forward<KeyCb>(key_cb), std::forward<ValCb>(val_cb));
        if(!payload.empty()) {
          write_raw(payload.data(), payload.size());
        }
      }

      template <typename T>
      void write_typed(std::shared_ptr<T> data) {
        auto type_index = std::type_index(typeid(*data));
        auto it = typed_indexes_.find(type_index);
        if(it == typed_indexes_.end()) {
          throw std::runtime_error("Type not registered");
        }
        write_u32(static_cast<std::uint32_t>(it->second));
        serialize_typed_functions_[it->second](&data);
      }

      template <typename T>
      void register_serialize_exception_function() {
        auto type = std::type_index(typeid(T));
        exception_indexes_[type] = serialize_exception_functions_.size();
        serialize_exception_functions_.push_back([&writer = *this](const std::exception& e) {
          serialize(writer, dynamic_cast<const T&>(e));
        });
      }

      void write_exception(const std::exception& e) {
        auto type_index = std::type_index(typeid(e));
        auto it = exception_indexes_.find(type_index);
        if(it == exception_indexes_.end()) {
          throw unknown_exception("Exception type not registered");
        }
        write_u32(static_cast<std::uint32_t>(it->second));
        serialize_exception_functions_[it->second](e);
      }

      template <typename T>
      void write_message(std::shared_ptr<T> data) {
        auto type_index = std::type_index(typeid(*data));
        auto it = message_indexes_.find(type_index);
        if(it == message_indexes_.end()) {
          throw std::runtime_error("Type not registered");
        }
        write_u32(static_cast<std::uint32_t>(it->second));
        serialize_message_functions_[it->second](&data);
      }

      void register_serialize_typed_function(std::type_index type,
          std::function<void(void*)> serialize_fn) {
        typed_indexes_[type] = serialize_typed_functions_.size();
        serialize_typed_functions_.push_back(std::move(serialize_fn));
      }

      template <typename T>
      void register_serialize_typed_function() {
        auto type = std::type_index(typeid(T));
        register_serialize_typed_function(type, [&writer = *this](void* ptr) {
          std::shared_ptr<T> data = *((std::shared_ptr<T>*)ptr);
          serialize(writer, *data);
        });
      }

      void register_serialize_message_function(std::type_index type,
          std::function<void(void*)> serialize_fn) {
        message_indexes_[type] = serialize_message_functions_.size();
        serialize_message_functions_.push_back(std::move(serialize_fn));
      }

      template <typename T>
      void register_serialize_message_function() {
        auto type = std::type_index(typeid(T));
        register_serialize_message_function(type, [&writer = *this](void* ptr) {
          std::shared_ptr<T> data = *((std::shared_ptr<T>*)ptr);
          serialize(writer, *data);
        });
      }

      struct circular_buffer* buffer_ = nullptr;
      size_t write_offset_ = 0;

      std::size_t current_pos() const { return write_offset_; }

      // Back-patch a u32 LE value at a previously reserved writer position.
      void poke_u32(std::size_t abs_offset, std::uint32_t value) {
        circular_buffer_poke(buffer_, abs_offset, &value, sizeof(value));
      }

    private:
      struct Frame {
        std::size_t start;
        std::vector<std::uint32_t> cps;
      };

      std::vector<Frame> frames_;

      std::vector<std::function<void(void*)>> serialize_typed_functions_;
      std::unordered_map<std::type_index, size_t> typed_indexes_;

      std::vector<std::function<void(const std::exception&)>> serialize_exception_functions_;
      std::unordered_map<std::type_index, size_t> exception_indexes_;

      std::vector<std::function<void(void*)>> serialize_message_functions_;
      std::unordered_map<std::type_index, size_t> message_indexes_;

      void ensure_writable(size_t bytes) {
        size_t available = circular_buffer_reserve_for_writing(buffer_, bytes + write_offset_);
        if(available < bytes) {
          throw std::runtime_error("flat_circular_buffer_writer: not enough space");
        }
      }

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

      template <typename T>
      void write_le_value(T value) {
        std::uint8_t buf[sizeof(T)];
        flat_store_le(buf, value);
        write_raw(buf, sizeof(T));
      }
  };

} // namespace serialization
