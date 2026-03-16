#pragma once

#include <cstdint>
#include <typeindex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <algorithm>

extern "C" {
#include <io/circular_buffer.h>
}

#include "writer.hpp"

namespace serialization {

  class circular_buffer_writer : public writer {
    public:
      explicit circular_buffer_writer(struct circular_buffer* buffer, size_t write_offset = 0) : buffer_(buffer), write_offset_(write_offset) {
        if(write_offset > circular_buffer_writable_bytes(buffer_)) {
          throw std::runtime_error("Write offset is greater than the number of writable bytes");
        }
      }
      ~circular_buffer_writer() = default;

      void begin_frame() {
        // no-op for circular buffer backend
      }

      void end_frame() {
        // no-op for circular buffer backend
      }

      void write_u64(std::uint64_t value) {
        write_uvar_int(value);
      }

      void write_i64(std::int64_t value) {
        write_svar_int(value);
      }

      void write_u32(std::uint32_t value) {
        write_uvar_int_32(value);
      }

      void write_i32(std::int32_t value) {
        write_svar_int_32(value);
      }

      void write_u16(std::uint16_t value) {
        write_uvar_int_16(value);
      }

      void write_i16(std::int16_t value) {
        write_svar_int_16(value);
      }

      void write_u8(std::uint8_t value) {
        write_bytes(&value, 1);
      }
      void write_i8(std::int8_t value) {
        value ^= 0x80; // to be sortable
        write_bytes(&value, 1);
      }

      void write_f64(double value) {
        std::uint64_t v = *(std::uint64_t*)(&value);
        if(v & 0x8000000000000000) {
          v = ~v;
        } else {
          v ^= 0x8000000000000000;          
        }
        if(value == 0) v = 0x8000000000000000;
        // INSERT_YOUR_CODE
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
          // We're on a little-endian system, so reverse the byte order before writing
          v = __builtin_bswap64(v);
  #endif
        write_bytes(&v, sizeof(v));
      }

      void write_f32(float value) {
        std::uint32_t v = *(std::uint32_t*)(&value);
        if(v & 0x80000000) {
          v = ~v;
        } else {
          v ^= 0x80000000;          
        }
        if(value == 0) v = 0x80000000;
        // INSERT_YOUR_CODE
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
          // We're on a little-endian system, so reverse the byte order before writing
          v = __builtin_bswap32(v);
  #endif
        write_bytes(&v, sizeof(v));
      }

      void write_bool(bool value) {
        uint8_t number = value ? 1 : 0;
        write_bytes(&number, 1);
      }

      void write_string(const std::string_view value) {
        write_uvar_int_32(static_cast<std::uint32_t>(value.size()));
        write_bytes(value.data(), value.size());
      }

      void write_binary(const std::vector<std::uint8_t>&& value) {
        write_uvar_int_32(static_cast<std::uint32_t>(value.size()));
        if(!value.empty()) {
          write_bytes(value.data(), value.size());
        }
      }

      void write_length(std::size_t length) {
        write_uvar_int_32(static_cast<std::uint32_t>(length));
      }

      template <typename T>
      void write_typed(std::shared_ptr<T> data) {
        auto type_index = std::type_index(typeid(*data));
        auto it = typed_indexes_.find(type_index);
        if(it == typed_indexes_.end()) {
          throw std::runtime_error("Type not registered");
        }
        size_t index = it->second;
        write_uvar_int_32(static_cast<std::uint32_t>(index));
        serialize_typed_functions_[index](&data);
      }

      template <typename T>
      void write_message(std::shared_ptr<T> data) {
        auto type_index = std::type_index(typeid(*data));
        auto it = message_indexes_.find(type_index);
        if(it == message_indexes_.end()) {
          throw std::runtime_error("Type not registered");
        }
        size_t index = it->second;
        write_uvar_int_32(static_cast<std::uint32_t>(index));
        serialize_message_functions_[index](&data);
      }

      void save_offset() { /* no-op */ }

      void write_rest() {
        while(!write_later_.empty()) {
          auto pair = write_later_.front();
          pair.second();
          written_pointers_.push_back(pair.first);
          write_later_.erase(write_later_.begin());
        }
      }

      void register_serialize_typed_function(std::type_index type,
          std::function<void(void*)> serialize) {
        typed_indexes_[type] = serialize_typed_functions_.size();
        serialize_typed_functions_.push_back(serialize);
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
          std::function<void(void*)> serialize) {
        message_indexes_[type] = serialize_message_functions_.size();
        serialize_message_functions_.push_back(serialize);
      }

      template <typename T>
      void register_serialize_message_function() {
        auto type = std::type_index(typeid(T));
        register_serialize_message_function(type, [&writer = *this](void* ptr) {
          std::shared_ptr<T> data = *((std::shared_ptr<T>*)ptr);
          serialize(writer, *data);
        });
      }

      template <typename T>
      void write_pointer(std::shared_ptr<T> value, WriteLater write_later) {
        if(!value) {
          write_svar_int_32(0);  // null = 0
          return;
        }
        auto existing = std::find(written_pointers_.begin(), written_pointers_.end(), value);
        if (existing != written_pointers_.end()) {
          std::int32_t index = std::distance(written_pointers_.begin(), existing);
          std::int32_t delta = index - static_cast<std::int32_t>(written_pointers_.size());
          write_svar_int_32(delta);  // always negative
          return;
        }
        auto will_be_written = std::find_if(write_later_.begin(), write_later_.end(),
          [value](const auto& pair) {
            return pair.first == value;
          }
        );
        if (will_be_written != write_later_.end()) {
          std::int32_t index = static_cast<std::int32_t>(std::distance(write_later_.begin(), will_be_written));
          write_svar_int_32(index + 1);  // always positive
          return;
        }
        std::int32_t index = static_cast<std::int32_t>(write_later_.size());
        write_svar_int_32(index + 1);  // always positive
        write_later_.push_back({value, write_later});
      }

      template <typename T>
      void write_pointer(std::weak_ptr<T> value, WriteLater write_later) {
        write_pointer(value.lock(), write_later);
      }

    public:
      struct circular_buffer* buffer_;
      size_t write_offset_;
    private:
      
      std::vector<std::shared_ptr<void>> written_pointers_;
      std::vector<std::pair<std::shared_ptr<void>, WriteLater>> write_later_;

      std::vector<std::function<void(void*)>> serialize_typed_functions_;
      std::unordered_map<std::type_index, size_t> typed_indexes_;

      std::vector<std::function<void(void*)>> serialize_message_functions_;
      std::unordered_map<std::type_index, size_t> message_indexes_;

      void ensure_writable(size_t bytes) {
        size_t available = circular_buffer_reserve_for_writing(buffer_, bytes + write_offset_);
        if(available < bytes) {
          throw std::runtime_error("Not enough space in circular buffer");
        }
      }

      void write_bytes(const void* source, size_t bytes) {
        ensure_writable(bytes);
        size_t written = circular_buffer_poke(buffer_, write_offset_, source, bytes);
        if(written != bytes) {
          throw std::runtime_error("Failed to write to circular buffer");
        }
        write_offset_ += written;
      }

      void write_uvar_int(std::uint64_t value) {
        ensure_writable(9);
        size_t written = circular_buffer_poke_uvarint64(buffer_, write_offset_, value);
        if(written == 0) {
          throw std::runtime_error("Failed to write uvarint");
        }
        write_offset_ += written;
      }

      void write_svar_int(std::int64_t value) {
        ensure_writable(9);
        size_t written = circular_buffer_poke_svarint64(buffer_, write_offset_, value);
        if(written == 0) {
          throw std::runtime_error("Failed to write svarint");
        }
        write_offset_ += written;
      }

      void write_uvar_int_32(std::uint32_t value) {
        ensure_writable(5);
        size_t written = circular_buffer_poke_uvarint32(buffer_, write_offset_, value);
        if(written == 0) throw std::runtime_error("Failed to write uvarint32");
        write_offset_ += written;
      }

      void write_svar_int_32(std::int32_t value) {
        ensure_writable(5);
        size_t written = circular_buffer_poke_svarint32(buffer_, write_offset_, value);
        if(written == 0) throw std::runtime_error("Failed to write svarint32");
        write_offset_ += written;
      }

      void write_uvar_int_16(std::uint16_t value) {
        ensure_writable(3);
        size_t written = circular_buffer_poke_uvarint16(buffer_, write_offset_, value);
        if(written == 0) throw std::runtime_error("Failed to write uvarint16");
        write_offset_ += written;
      }

      void write_svar_int_16(std::int16_t value) {
        ensure_writable(3);
        size_t written = circular_buffer_poke_svarint16(buffer_, write_offset_, value);
        if(written == 0) throw std::runtime_error("Failed to write svarint16");
        write_offset_ += written;
      }
  };
}


