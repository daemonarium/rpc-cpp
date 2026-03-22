#pragma once

#include <cstdint>
#include <typeindex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <exception>

extern "C" {
#include <io/circular_buffer.h>
}

#include "reader.hpp"
#include "deserialize.hpp"

namespace serialization {

  class circular_buffer_reader : public reader {
    public:

      class underflow_error : public std::runtime_error {
        public:
          underflow_error() : std::runtime_error("Underflow error") {}
      };

      explicit circular_buffer_reader(struct circular_buffer* buffer, size_t read_offset = 0) : buffer_(buffer), read_offset_(read_offset) {
        if(read_offset > circular_buffer_readable_bytes(buffer_)) {
          throw std::runtime_error("Read offset is greater than the number of readable bytes");
        }
      }
      ~circular_buffer_reader() = default;

      consteval bool is_indexed() { return false; }

      void enter_frame() {
        // no-op for circular buffer backend
      }

      void exit_frame() {
        // no-op for circular buffer backend
      }

      void enter_indexed_frame(size_t id) {
        throw std::runtime_error("Not indexed");
      }

      void read_u64(std::uint64_t& value) {
        value = read_uvar_int();
      }

      void read_i64(std::int64_t& value) {
        value = read_svar_int();
      }

      void read_u32(std::uint32_t& value) {
        value = read_uvar_int_32();
      }

      void read_i32(std::int32_t& value) {
        value = read_svar_int_32();
      }

      void read_u16(std::uint16_t& value) {
        value = read_uvar_int_16();
      }

      void read_i16(std::int16_t& value) {
        value = read_svar_int_16();
      }

      void read_u8(std::uint8_t& value) {
        read_bytes(&value, 1);
      }
      void read_i8(std::int8_t& value) {
        uint8_t byte = 0;
        read_bytes(&byte, 1);
        value = byte ^ 0x80;
      }

      void read_f64(double& value) {
        std::uint64_t v;
        read_bytes(&v, sizeof(v));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        // We're on a little-endian system, so reverse the byte order before reading
        v = __builtin_bswap64(v);
#endif
        if(v & 0x8000000000000000) {
          v ^= 0x8000000000000000;
        } else {
          v = ~v;          
        }
        value = *(double*)(&v);
      }

      void read_f32(float& value) {
        std::uint32_t v;
        read_bytes(&v, sizeof(v));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        // We're on a little-endian system, so reverse the byte order before reading
        v = __builtin_bswap32(v);
#endif
        if(v & 0x80000000) {
          v ^= 0x80000000;
        } else {
          v = ~v;          
        }
        value = *(float*)(&v);
      }

      void read_bool(bool& value) {
        uint8_t byte = 0;
        read_bytes(&byte, 1);
        value = byte != 0;
      }

      void read_string(std::string& value) {
        std::uint32_t length = read_uvar_int_32();
        value.resize(length);
        if(length > 0) {
          read_bytes(value.data(), length);
        }
      }

      void read_binary(std::vector<std::uint8_t>& value) {
        std::uint32_t length = read_uvar_int_32();
        value.resize(length);
        if(length > 0) {
          read_bytes(value.data(), length);
        }
      }

      void read_length(std::size_t& value) {
        value = read_uvar_int_32();
      }

      template <typename T>
      void register_deserialize_exception_function() {
        auto type = std::type_index(typeid(T));
        exception_read_indexes_[type] = deserialize_exception_functions_.size();
        deserialize_exception_functions_.push_back([&reader = *this]() -> std::exception_ptr {
          if constexpr (SimpleException<T>) {
            T ex("");
            ::serialization::deserialize(reader, ex);
            return std::make_exception_ptr(std::move(ex));
          } else {
            T ex{};
            ::serialization::deserialize(reader, ex);
            return std::make_exception_ptr(std::move(ex));
          }
        });
      }

      std::exception_ptr read_exception() {
        std::uint32_t index = read_uvar_int_32();
        if(index >= deserialize_exception_functions_.size()) {
          throw std::runtime_error("Exception type not registered for deserialization");
        }
        return deserialize_exception_functions_[index]();
      }

      template <typename T>
      void read_typed(std::shared_ptr<T>& data) {
        std::uint32_t index = read_uvar_int_32();
        void* ptr = &data;
        deserialize_typed_functions_[index](ptr);
      }

      void read_typed(std::weak_ptr<void>& data) {
        std::shared_ptr<void> shared;
        read_typed(shared);
        data = shared;
      }

      template <typename T>
      void read_message(std::shared_ptr<T>& data) {
        std::uint32_t index = read_uvar_int_32();
        void* ptr = &data;
        deserialize_message_functions_[index](ptr);
      }

      void read_message(std::weak_ptr<void>& data) {
        std::shared_ptr<void> shared;
        read_message(shared);
        data = shared;
      }

      void register_deserialize_typed_function(std::type_index type,
          std::function<void(void*)> deserialize) {
        typed_indexes_[type] = deserialize_typed_functions_.size();
        deserialize_typed_functions_.push_back(deserialize);
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

      void register_deserialize_message_function(std::type_index type,
          std::function<void(void*)> deserialize) {
        message_indexes_[type] = deserialize_message_functions_.size();
        deserialize_message_functions_.push_back(deserialize);
      }

      template <typename T>
      void register_deserialize_message_function() {
        auto type = std::type_index(typeid(T));
        register_deserialize_message_function(type, [&reader = *this](void* ptr) {
          std::shared_ptr<T>& shared = *((std::shared_ptr<T>*)ptr);
          std::shared_ptr<T> data = std::make_shared<T>();
          deserialize(reader, *data);
          shared = data;
        });
      }

      template <typename T>
      void read_pointer(std::shared_ptr<T>& value, ReadLater read_later) {
        std::int32_t delta = read_svar_int_32();
        if(delta == 0) {
          value = nullptr;
          return;
        }
        if(delta < 0) {  // already read
          size_t index = static_cast<size_t>(static_cast<std::ptrdiff_t>(read_pointers_.size()) + delta);
          value = std::static_pointer_cast<T>(read_pointers_[index]);
          return;
        }
        AssignAfterRead assign = [&value](std::shared_ptr<void>& data) {
          value = std::static_pointer_cast<T>(data);
        };
        delta--; // decrement delta, because 0 is null
        if(delta < read_later_.size()) { // will be read later
          read_later_[delta].first.push_back(assign);
          return;
        }
        if(delta > read_later_.size()) { // writer should always write pointers in the same order
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

    public:
      struct circular_buffer* buffer_;
      size_t read_offset_;
    private:

      std::vector<std::shared_ptr<void>> read_pointers_;

      using AssignAfterRead = std::function<void(std::shared_ptr<void>&)>;
      std::vector<std::pair<std::vector<AssignAfterRead>, ReadLater>> read_later_;

      std::vector<std::function<void(void*)>> deserialize_typed_functions_;
      std::unordered_map<std::type_index, size_t> typed_indexes_;

      std::vector<std::function<void(void*)>> deserialize_message_functions_;
      std::unordered_map<std::type_index, size_t> message_indexes_;

      std::vector<std::function<std::exception_ptr()>> deserialize_exception_functions_;
      std::unordered_map<std::type_index, size_t> exception_read_indexes_;

      void ensure_readable(size_t bytes) {
        size_t available = circular_buffer_readable_bytes(buffer_) - read_offset_;
        if(available < bytes) {
          throw underflow_error();
        }
      }

      void read_bytes(void* dest, size_t bytes) {
        ensure_readable(bytes);
        size_t read = circular_buffer_peek(buffer_, read_offset_, dest, bytes);
        if(read != bytes) {
          throw underflow_error();
        }
        read_offset_ += read;
      }

      std::uint64_t read_uvar_int() {
        uint64_t value = 0;
        size_t read = circular_buffer_peek_uvarint64(buffer_, read_offset_, &value);
        if(read == 0) {
          throw underflow_error();
        }
        read_offset_ += read;
        return value;
      }

      std::int64_t read_svar_int() {
        int64_t value = 0;
        size_t read = circular_buffer_peek_svarint64(buffer_, read_offset_, &value);
        if(read == 0) {
          throw underflow_error();
        }
        read_offset_ += read;
        return value;
      }

      std::uint32_t read_uvar_int_32() {
        uint32_t value = 0;
        size_t read = circular_buffer_peek_uvarint32(buffer_, read_offset_, &value);
        if(read == 0) throw underflow_error();
        read_offset_ += read;
        return value;
      }

      std::int32_t read_svar_int_32() {
        int32_t value = 0;
        size_t read = circular_buffer_peek_svarint32(buffer_, read_offset_, &value);
        if(read == 0) throw underflow_error();
        read_offset_ += read;
        return value;
      }

      std::uint16_t read_uvar_int_16() {
        uint16_t value = 0;
        size_t read = circular_buffer_peek_uvarint16(buffer_, read_offset_, &value);
        if(read == 0) throw underflow_error();
        read_offset_ += read;
        return value;
      }

      std::int16_t read_svar_int_16() {
        int16_t value = 0;
        size_t read = circular_buffer_peek_svarint16(buffer_, read_offset_, &value);
        if(read == 0) throw underflow_error();
        read_offset_ += read;
        return value;
      }
  };
}


