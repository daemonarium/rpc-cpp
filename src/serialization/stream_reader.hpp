#pragma once

#include "reader.hpp"
#include "deserialize.hpp"
#include <cstdint>
#include <istream>
#include <typeindex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <exception>

#ifndef __EMSCRIPTEN__
//#define SERIALIZATION_STREAM_READER_DEBUG
#endif

namespace serialization {

  class stream_reader : public reader {
    public:
      stream_reader(std::istream& stream) : stream_(stream) {}
      ~stream_reader() {}

      consteval bool is_indexed() { return false; }
      void enter_frame() { 
        debug("enter_frame");
        debug_indent(1);
        // ignore
      }
      void exit_frame() { 
        debug_indent(-1);
        debug("exit_frame");
        // ignore
      }
      void save_offset() {
        // no-op for compressed stream format
      }
      void enter_indexed_frame(size_t id) { throw std::runtime_error("Not indexed"); }

      void read_u64(std::uint64_t& value) {
        debug("read_u64");
        value = read_uvar_int();
        debug("  read_u64 result %d", value);
      }
      void read_i64(std::int64_t& value) {
        debug("read_i64");
        value = read_svar_int();
        debug("  read_i64 result %d", value);
      }
      void read_u32(std::uint32_t& value) {
        debug("read_u32");
        value = read_uvar_int_32();
        debug("  read_u32 result %u", value);
      }
      void read_i32(std::int32_t& value) {
        debug("read_i32");
        value = read_svar_int_32();
        debug("  read_i32 result %d", value);
      }
      void read_u16(std::uint16_t& value) {
        debug("read_u16");
        value = read_uvar_int_16();
        debug("  read_u16 result %u", value);
      }
      void read_i16(std::int16_t& value) {
        debug("read_i16");
        value = read_svar_int_16();
        debug("  read_i16 result %d", value);
      }
      void read_u8(std::uint8_t& value) {
        debug("read_u8");
        value = static_cast<std::uint8_t>(stream_.get());
        debug("  read_u8 result %u", value);
      }
      void read_i8(std::int8_t& value) {
        debug("read_i8");
        std::uint8_t byte = static_cast<std::uint8_t>(stream_.get());
        value = static_cast<std::int8_t>(byte ^ 0x80);
        debug("  read_i8 result %d", value);
      }
      void read_f64(double& value) {
        debug("read_f64");
        std::uint64_t v;
        stream_.read((char*)&v, sizeof(v));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        v = __builtin_bswap64(v);
#endif
        if(v & 0x8000000000000000) {
          v ^= 0x8000000000000000;
        } else {
          v = ~v;
        }
        value = *(double*)(&v);
        debug("  read_f64 result %f", value);
      }
      void read_f32(float& value) {
        debug("read_f32");
        std::uint32_t v;
        stream_.read((char*)&v, sizeof(v));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        v = __builtin_bswap32(v);
#endif
        if(v & 0x80000000) {
          v ^= 0x80000000;
        } else {
          v = ~v;
        }
        value = *(float*)(&v);
        debug("  read_f32 result %f", value);
      }
      void read_bool(bool& value) {
        debug("read_bool");
        value = stream_.get() != 0;
        debug("  read_bool result %d", value);
      }
      void read_string(std::string& value) {
        debug("read_string");
        std::uint32_t length = read_uvar_int_32();
        value.resize(length);
        stream_.read(value.data(), length);
        debug("  read_string result %u '%s'", length, value.c_str());
      }
      void read_binary(std::vector<std::uint8_t>& value) {
        debug("read_binary");
        std::uint32_t length = read_uvar_int_32();
        value.resize(length);
        stream_.read((char*)(value.data()), length);
        debug("  read_binary result %u", length);
      }
      void read_length(std::size_t& value) {
        debug("read_length");
        value = read_uvar_int_32();
        debug("  read_length result %zd", value);
      }

      template <class Map, class KeyCb, class ValCb>
      void read_map(Map& map, KeyCb&& key_cb, ValCb&& val_cb) {
        std::size_t length = 0;
        read_length(length);
        for(std::size_t i = 0; i < length; ++i) {
          typename Map::key_type key{};
          typename Map::mapped_type item{};
          key_cb(*this, key);
          val_cb(*this, item);
          map.insert(std::make_pair(std::move(key), std::move(item)));
        }
      }

      template <typename T>
      void register_deserialize_exception_function() {
        auto type = std::type_index(typeid(T));
        exception_read_indexes_[type] = deserialize_exception_functions_.size();
        deserialize_exception_functions_.push_back([&reader = *this]() -> std::exception_ptr {
          if constexpr (std::is_same_v<T, std::runtime_error>) {
            std::runtime_error ex("");
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
        debug("read_typed");
        std::uint32_t index = read_uvar_int_32();
        void* ptr = &data;
        debug("  read_typed type %u", index);
        deserialize_typed_functions_[index](ptr);
      }

      void read_typed(std::weak_ptr<void>& data) {
        debug("read_weak_ptr");
        std::shared_ptr<void> shared;
        read_typed(shared);
        data = shared;
        debug("  read_weak_ptr result %p", data.lock().get());
      }

      template <typename T>
      void read_message(std::shared_ptr<T>& data) {
        debug("read_message");
        std::uint32_t index = read_uvar_int_32();
        void* ptr = &data;
        debug("  read_message type %u", index);
        deserialize_message_functions_[index](ptr);
      }

      void read_message(std::weak_ptr<void>& data) {
        debug("read_weak_ptr_message");
        std::shared_ptr<void> shared;
        read_message(shared);
        data = shared;
        debug("  read_weak_ptr_message result %p", data.lock().get());
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
        debug("read_pointer");
        std::int32_t delta = read_svar_int_32();
        debug("  read_pointer delta %d", delta);
        if(delta == 0) {
          value = nullptr;
          return;
        }
        if(delta < 0) {  // already read
          size_t index = static_cast<size_t>(static_cast<std::ptrdiff_t>(read_pointers_.size()) + delta);
          value = std::static_pointer_cast<T>(read_pointers_[index]);
          debug("  read_pointer - existsing %p", value.get());
          return;
        } 
        AssignAfterRead assign = [&value](std::shared_ptr<void>& data) {
          value = std::static_pointer_cast<T>(data);
        };
        delta--; // decrement delta, because 0 is null
        if(delta < read_later_.size()) { // will be read later          
          read_later_[delta].first.push_back(assign);
          debug("  read_pointer - will be read later %p", value.get());
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
        debug("read_rest %zd", read_later_.size());
        while(!read_later_.empty()) {
          debug("  read_rest - reading later %zd", read_later_.size());
          // Move callback out; do not keep references across it — read_typed may
          // push_back new later-entries and reallocate. Self-refs append assigns
          // onto front().first during the callback; collect them afterwards.
          ReadLater callbacks = std::move(read_later_.front().second);
          std::shared_ptr<void> result;
          callbacks(result);
          auto assigns = std::move(read_later_.front().first);
          for(auto& assign : assigns) {
            assign(result);
          }
          read_pointers_.push_back(std::move(result));
          read_later_.erase(read_later_.begin());
        }
      }

    public:
      std::istream& stream_;
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

      #ifdef SERIALIZATION_STREAM_READER_DEBUG
      uint32_t indent_ = 0;
      #endif

      inline void debug_indent(int delta) {
        #ifdef SERIALIZATION_STREAM_READER_DEBUG
        indent_ += delta;        
        #endif
      }

      inline void debug(const char* format, ...) {
        #ifdef SERIALIZATION_STREAM_READER_DEBUG
        va_list args;
        va_start(args, format);
        fprintf(stderr, "at %04zx: ", (size_t)stream_.tellg());
        fprintf(stderr, "%s", std::string(indent_*2, ' ').c_str());
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
        va_end(args);
        #endif
      }

      std::uint64_t read_uvar_int() {
        uint8_t number[9];
        number[0] = stream_.get();
        if((number[0] & 0x80) == 0) { // 7 bit number encoded in 1 byte
          return number[0];
        } else if ((number[0] & 0x40) == 0) { // 14 bit number encoded in 2 bytes
          number[1] = stream_.get();
          return (((uint64_t)number[0] & 0x3F) << 8) | number[1];
        } else if ((number[0] & 0x20) == 0) { // 21 bit number encoded in 3 bytes
          stream_.read((char*)number + 1, 2);
          return (((uint64_t)number[0] & 0x1F) << 16) | ((uint64_t)number[1] << 8) | number[2];
        } else if ((number[0] & 0x10) == 0) { // 28 bit number encoded in 4 bytes
          stream_.read((char*)number + 1, 3);
          return (((uint64_t)number[0] & 0x0F) << 24) | ((uint64_t)number[1] << 16) | ((uint64_t)number[2] << 8) 
               | number[3];
        } else if ((number[0] & 0x08) == 0) { // 35 bit number encoded in 5 bytes
          stream_.read((char*)number + 1, 4);          
          return ((uint64_t)(number[0] & 0x07) << 32) | ((uint64_t)number[1] << 24) | ((uint64_t)number[2] << 16) 
               | ((uint64_t)number[3] << 8) | number[4];
        } else if((number[0] & 0x04) == 0) { // 42 bit number encoded in 6 bytes
          stream_.read((char*)number + 1, 5);
          return ((uint64_t)(number[0] & 0x03) << 40) | ((uint64_t)number[1] << 32) | (number[2] << 24)
               | ((uint64_t)number[3] << 16) | ((uint64_t)number[4] << 8) | number[5];
        } else if((number[0] & 0x02) == 0) { // 49 bit number encoded in 7 bytes
          stream_.read((char*)number + 1, 6);
          return ((uint64_t)(number[0] & 0x01) << 48) | ((uint64_t)number[1] << 40) | ((uint64_t)number[2] << 32)
               | ((uint64_t)number[3] << 24) | ((uint64_t)number[4] << 16) | ((uint64_t)number[5] << 8) | number[6];
        } else if((number[0] & 0x01) == 0) { // 56 bit number encoded in 8 bytes
          stream_.read((char*)number + 1, 7);
          return ((uint64_t)number[1] << 48) | ((uint64_t)number[2] << 40) | ((uint64_t)number[3] << 32)
               | ((uint64_t)number[4] << 24) | ((uint64_t)number[5] << 16) | ((uint64_t)number[6] << 8) | number[7];
        } else { // 64 bit number encoded in 9 bytes
          stream_.read((char*)number + 1, 8);
          return ((uint64_t)number[1] << 56) | ((uint64_t)number[2] << 48) | ((uint64_t)number[3] << 40)
               | ((uint64_t)number[4] << 32) | ((uint64_t)number[5] << 24) | ((uint64_t)number[6] << 16) 
               | ((uint64_t)number[7] << 8) | number[8];
        }
      }

      std::int64_t read_svar_int() {
        uint8_t number[9];
        number[0] = stream_.get();
        bool negative = (number[0] & 0x80) == 0;

        if(negative) {
          // Negative: length = leading zeros then a one. Prefix: 01 (1B), 001 (2B), ...
          if((number[0] & 0x40) != 0) { // 1 byte, 6 bit. Prefix 01.
            return -((~number[0]) & 0x3F);
          }
          if((number[0] & 0xE0) == 0x20) { // 2 bytes, 13 bit. Prefix 001.
            number[1] = stream_.get();
            uint64_t encoded = (((uint64_t)number[0] & 0x1F) << 8) | number[1];
            return -(0x1FFF - encoded);
          }
          if((number[0] & 0xF0) == 0x10) { // 3 bytes, 20 bit. Prefix 0001.
            stream_.read((char*)number + 1, 2);
            uint64_t encoded = (((uint64_t)number[0] & 0x0F) << 16)
                             | ((uint64_t)number[1] << 8)
                             | number[2];
            return -static_cast<std::int64_t>((uint64_t)0xFFFFF - encoded);
          }
          if((number[0] & 0xF8) == 0x08) { // 4 bytes, 27 bit. Prefix 00001.
            stream_.read((char*)number + 1, 3);
            uint64_t encoded = (((uint64_t)number[0] & 0x07) << 24)
                             | ((uint64_t)number[1] << 16)
                             | ((uint64_t)number[2] << 8)
                             | number[3];
            return -static_cast<std::int64_t>((uint64_t)0x7FFFFFF - encoded);
          }
          if((number[0] & 0xFC) == 0x04) { // 5 bytes, 34 bit. Prefix 000001.
            stream_.read((char*)number + 1, 4);
            uint64_t encoded = (((uint64_t)number[0] & 0x03) << 32)
                             | ((uint64_t)number[1] << 24)
                             | ((uint64_t)number[2] << 16)
                             | ((uint64_t)number[3] << 8)
                             | number[4];
            return -static_cast<std::int64_t>((uint64_t)0x3FFFFFFFF - encoded);
          }
          if((number[0] & 0xFE) == 0x02) { // 6 bytes, 41 bit. Prefix 0000001.
            stream_.read((char*)number + 1, 5);
            uint64_t encoded = (((uint64_t)number[0] & 0x01) << 40)
                             | ((uint64_t)number[1] << 32)
                             | ((uint64_t)number[2] << 24)
                             | ((uint64_t)number[3] << 16)
                             | ((uint64_t)number[4] << 8)
                             | number[5];
            return -static_cast<std::int64_t>((uint64_t)0x1FFFFFFFFFF - encoded);
          }
          if(number[0] == 0x01) { // 7 bytes, 48 bit. Prefix 00000001.
            stream_.read((char*)number + 1, 6);
            uint64_t encoded = ((uint64_t)number[1] << 40)
                             | ((uint64_t)number[2] << 32)
                             | ((uint64_t)number[3] << 24)
                             | ((uint64_t)number[4] << 16)
                             | ((uint64_t)number[5] << 8)
                             | number[6];
            return -static_cast<std::int64_t>((uint64_t)0xFFFFFFFFFFFF - encoded);
          }
          // 9 bytes, 64 bit. Prefix 00000000.
          stream_.read((char*)number + 1, 8);
          uint64_t encoded = ((uint64_t)number[1] << 56)
                           | ((uint64_t)number[2] << 48)
                           | ((uint64_t)number[3] << 40)
                           | ((uint64_t)number[4] << 32)
                           | ((uint64_t)number[5] << 24)
                           | ((uint64_t)number[6] << 16)
                           | ((uint64_t)number[7] << 8)
                           | number[8];
          return -static_cast<std::int64_t>((uint64_t)0xFFFFFFFFFFFFFFFF - encoded);
        } else {
          // Positive: length = leading ones then a zero. Prefix: 10 (1B), 110 (2B), ...
          if((number[0] & 0x40) == 0) { // 1 byte, 6 bit. Prefix 10.
            return (number[0] & 0x3F);
          }
          if((number[0] & 0x20) == 0) { // 2 bytes, 13 bit. Prefix 110.
            number[1] = stream_.get();
            uint64_t encoded = (((uint64_t)number[0] & 0x1F) << 8) | number[1];
            return static_cast<std::int64_t>(encoded);
          }
          if((number[0] & 0x10) == 0) { // 3 bytes, 20 bit. Prefix 1110.
            stream_.read((char*)number + 1, 2);
            uint64_t encoded = (((uint64_t)number[0] & 0x0F) << 16)
                             | ((uint64_t)number[1] << 8)
                             | number[2];
            return static_cast<std::int64_t>(encoded);
          }
          if((number[0] & 0x08) == 0) { // 4 bytes, 27 bit. Prefix 11110.
            stream_.read((char*)number + 1, 3);
            uint64_t encoded = (((uint64_t)number[0] & 0x07) << 24)
                             | ((uint64_t)number[1] << 16)
                             | ((uint64_t)number[2] << 8)
                             | number[3];
            return static_cast<std::int64_t>(encoded);
          }
          if((number[0] & 0x04) == 0) { // 5 bytes, 34 bit. Prefix 111110.
            stream_.read((char*)number + 1, 4);
            uint64_t encoded = (((uint64_t)number[0] & 0x03) << 32)
                             | ((uint64_t)number[1] << 24)
                             | ((uint64_t)number[2] << 16)
                             | ((uint64_t)number[3] << 8)
                             | number[4];
            return static_cast<std::int64_t>(encoded);
          }
          if((number[0] & 0x02) == 0) { // 6 bytes, 41 bit. Prefix 1111110.
            stream_.read((char*)number + 1, 5);
            uint64_t encoded = (((uint64_t)number[0] & 0x01) << 40)
                             | ((uint64_t)number[1] << 32)
                             | ((uint64_t)number[2] << 24)
                             | ((uint64_t)number[3] << 16)
                             | ((uint64_t)number[4] << 8)
                             | number[5];
            return static_cast<std::int64_t>(encoded);
          }
          if((number[0] & 0x01) == 0) { // 7 bytes, 48 bit. Prefix 11111110.
            stream_.read((char*)number + 1, 6);
            uint64_t encoded = ((uint64_t)number[1] << 40)
                             | ((uint64_t)number[2] << 32)
                             | ((uint64_t)number[3] << 24)
                             | ((uint64_t)number[4] << 16)
                             | ((uint64_t)number[5] << 8)
                             | number[6];
            return static_cast<std::int64_t>(encoded);
          }
          // 9 bytes, 64 bit. Prefix 11111111.
          stream_.read((char*)number + 1, 8);
          uint64_t encoded = ((uint64_t)number[1] << 56)
                           | ((uint64_t)number[2] << 48)
                           | ((uint64_t)number[3] << 40)
                           | ((uint64_t)number[4] << 32)
                           | ((uint64_t)number[5] << 24)
                           | ((uint64_t)number[6] << 16)
                           | ((uint64_t)number[7] << 8)
                           | number[8];
          return static_cast<std::int64_t>(encoded);
        }
      }

      std::uint32_t read_uvar_int_32() {
        uint8_t number[5];
        number[0] = stream_.get();
        if ((number[0] & 0x80) == 0) return number[0];
        if ((number[0] & 0x40) == 0) {
          number[1] = stream_.get();
          return (static_cast<std::uint32_t>(number[0] & 0x3F) << 8) | number[1];
        }
        if ((number[0] & 0x20) == 0) {
          stream_.read((char*)number + 1, 2);
          return (static_cast<std::uint32_t>(number[0] & 0x1F) << 16) | (static_cast<std::uint32_t>(number[1]) << 8) | number[2];
        }
        if ((number[0] & 0x10) == 0) {
          stream_.read((char*)number + 1, 3);
          return (static_cast<std::uint32_t>(number[0] & 0x0F) << 24) | (static_cast<std::uint32_t>(number[1]) << 16) | (static_cast<std::uint32_t>(number[2]) << 8) | number[3];
        }
        if ((number[0] & 0x08) == 0) {
          stream_.read((char*)number + 1, 4);
          if ((number[0] & 0x07) != 0) throw std::runtime_error("uvarint32 overflow");
          return (static_cast<std::uint32_t>(number[1]) << 24) | (static_cast<std::uint32_t>(number[2]) << 16) | (static_cast<std::uint32_t>(number[3]) << 8) | number[4];
        }
        throw std::runtime_error("uvarint32 invalid");
      }

      std::int32_t read_svar_int_32() {
        uint8_t number[5];
        number[0] = stream_.get();
        bool negative = (number[0] & 0x80) == 0;
        if (negative) {
          if ((number[0] & 0x40) != 0) return -static_cast<std::int32_t>((~(static_cast<std::uint32_t>(number[0]))) & 0x3F);
          if ((number[0] & 0xE0) == 0x20) {
            number[1] = stream_.get();
            std::uint32_t encoded = (static_cast<std::uint32_t>(number[0] & 0x1F) << 8) | number[1];
            return -static_cast<std::int32_t>(0x1FFF - encoded);
          }
          if ((number[0] & 0xF0) == 0x10) {
            stream_.read((char*)number + 1, 2);
            std::uint32_t encoded = (static_cast<std::uint32_t>(number[0] & 0x0F) << 16) | (static_cast<std::uint32_t>(number[1]) << 8) | number[2];
            return -static_cast<std::int32_t>(0xFFFFF - encoded);
          }
          if ((number[0] & 0xF8) == 0x08) {
            stream_.read((char*)number + 1, 3);
            std::uint32_t encoded = (static_cast<std::uint32_t>(number[0] & 0x07) << 24) | (static_cast<std::uint32_t>(number[1]) << 16) | (static_cast<std::uint32_t>(number[2]) << 8) | number[3];
            return -static_cast<std::int32_t>(0x7FFFFFF - encoded);
          }
          if ((number[0] & 0xFC) == 0x04) {
            stream_.read((char*)number + 1, 4);
            std::uint32_t encoded = (static_cast<std::uint32_t>(number[1]) << 24) | (static_cast<std::uint32_t>(number[2]) << 16) | (static_cast<std::uint32_t>(number[3]) << 8) | number[4];
            return -static_cast<std::int32_t>(0xFFFFFFFF - encoded);
          }
          throw std::runtime_error("svarint32 invalid");
        } else {
          if ((number[0] & 0x40) == 0) return static_cast<std::int32_t>(number[0] & 0x3F);
          if ((number[0] & 0x20) == 0) {
            number[1] = stream_.get();
            return static_cast<std::int32_t>((static_cast<std::uint32_t>(number[0] & 0x1F) << 8) | number[1]);
          }
          if ((number[0] & 0x10) == 0) {
            stream_.read((char*)number + 1, 2);
            return static_cast<std::int32_t>((static_cast<std::uint32_t>(number[0] & 0x0F) << 16) | (static_cast<std::uint32_t>(number[1]) << 8) | number[2]);
          }
          if ((number[0] & 0x08) == 0) {
            stream_.read((char*)number + 1, 3);
            std::uint32_t encoded = (static_cast<std::uint32_t>(number[0] & 0x07) << 24) | (static_cast<std::uint32_t>(number[1]) << 16) | (static_cast<std::uint32_t>(number[2]) << 8) | number[3];
            if (encoded > 0x7FFFFFFF) throw std::runtime_error("svarint32 overflow");
            return static_cast<std::int32_t>(encoded);
          }
          if ((number[0] & 0x04) == 0) {
            stream_.read((char*)number + 1, 4);
            if ((number[0] & 0x03) != 0) throw std::runtime_error("svarint32 overflow");
            std::uint32_t encoded = (static_cast<std::uint32_t>(number[1]) << 24) | (static_cast<std::uint32_t>(number[2]) << 16) | (static_cast<std::uint32_t>(number[3]) << 8) | number[4];
            if (encoded > 0x7FFFFFFF) throw std::runtime_error("svarint32 overflow");
            return static_cast<std::int32_t>(encoded);
          }
          throw std::runtime_error("svarint32 invalid");
        }
      }

      std::uint16_t read_uvar_int_16() {
        uint8_t number[3];
        number[0] = stream_.get();
        if ((number[0] & 0x80) == 0) return number[0];
        if ((number[0] & 0x40) == 0) {
          number[1] = stream_.get();
          return static_cast<std::uint16_t>((static_cast<std::uint16_t>(number[0] & 0x3F) << 8) | number[1]);
        }
        if ((number[0] & 0x20) == 0) {
          stream_.read((char*)number + 1, 2);
          if ((number[0] & 0x1F) != 0) throw std::runtime_error("uvarint16 overflow");
          return static_cast<std::uint16_t>((static_cast<std::uint16_t>(number[1]) << 8) | number[2]);
        }
        throw std::runtime_error("uvarint16 invalid");
      }

      std::int16_t read_svar_int_16() {
        uint8_t number[3];
        number[0] = stream_.get();
        if ((number[0] & 0x80) == 0) {
          if ((number[0] & 0x40) != 0) return -static_cast<std::int16_t>(static_cast<std::uint16_t>((~(static_cast<std::uint16_t>(number[0]))) & 0x3F));
          if ((number[0] & 0xE0) == 0x20) {
            number[1] = stream_.get();
            std::uint16_t encoded = static_cast<std::uint16_t>((static_cast<std::uint16_t>(number[0] & 0x1F) << 8) | number[1]);
            return -static_cast<std::int16_t>(static_cast<std::uint16_t>(0x1FFF - encoded));
          }
          if ((number[0] & 0xF0) == 0x10) {
            stream_.read((char*)number + 1, 2);
            std::uint32_t encoded = (static_cast<std::uint32_t>(number[0] & 0x0F) << 16) | (static_cast<std::uint32_t>(number[1]) << 8) | number[2];
            std::uint32_t mag = 0xFFFFF - encoded;
            if (mag > 0x8000) throw std::runtime_error("svarint16 overflow");
            return -static_cast<std::int16_t>(static_cast<std::uint16_t>(mag));
          }
          throw std::runtime_error("svarint16 invalid");
        } else {
          if ((number[0] & 0x40) == 0) return static_cast<std::int16_t>(static_cast<std::uint16_t>(number[0] & 0x3F));
          if ((number[0] & 0x20) == 0) {
            number[1] = stream_.get();
            return static_cast<std::int16_t>(static_cast<std::uint16_t>((static_cast<std::uint16_t>(number[0] & 0x1F) << 8) | number[1]));
          }
          if ((number[0] & 0x10) == 0) {
            stream_.read((char*)number + 1, 2);
            if ((number[0] & 0x0F) != 0) throw std::runtime_error("svarint16 overflow");
            std::uint16_t encoded = static_cast<std::uint16_t>((static_cast<std::uint16_t>(number[1]) << 8) | number[2]);
            if (encoded > 0x7FFF) throw std::runtime_error("svarint16 overflow");
            return static_cast<std::int16_t>(encoded);
          }
          throw std::runtime_error("svarint16 invalid");
        }
      }
    };
  }