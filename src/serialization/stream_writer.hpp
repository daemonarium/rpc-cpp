#pragma once

#include "writer.hpp"
#include <cstdint>
#include <ostream>
#include <typeindex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <exception>
#include "serialization/rpc.hpp"

//#define SERIALIZATION_STREAM_WRITER_DEBUG

namespace serialization {

  template <typename S>
  concept AnyStream = requires(S& stream) {
    { stream.write("test", (size_t)4) } -> std::same_as<S&>;
  };

  class stream_writer : public writer {
    public:
      stream_writer(std::ostream& stream) : stream_(stream) {}
      ~stream_writer() {}

      void begin_frame() { 
        debug("begin_frame");
        debug_indent(1);
        // ignore
      }
      void end_frame() { 
        debug_indent(-1);
        debug("end_frame");
        // ignore
      }

      void write_u64(std::uint64_t value) {
        debug("write_u64 %d", value);
        write_uvar_int(value);
      }
      void write_i64(std::int64_t value) {
        debug("write_i64 %d", value);
        write_svar_int(value);
      }
      void write_u32(std::uint32_t value) {
        debug("write_u32 %u", value);
        write_uvar_int_32(value);
      }
      void write_i32(std::int32_t value) {
        debug("write_i32 %d", value);
        write_svar_int_32(value);
      }
      void write_u16(std::uint16_t value) {
        debug("write_u16 %u", value);
        write_uvar_int_16(value);
      }
      void write_i16(std::int16_t value) {
        debug("write_i16 %d", value);
        write_svar_int_16(value);
      }
      void write_u8(std::uint8_t value) {
        debug("write_u8 %u", value);
        stream_.write((char*)&value, 1);
      }
      void write_i8(std::int8_t value) {
        debug("write_i8 %d", value);
        std::int8_t v = value ^ 0x80;
        stream_.write((char*)&v, 1);
      }
      void write_f64(double value) {
        debug("write_f64 %f", value);
        std::uint64_t v = *(std::uint64_t*)(&value);
        if(v & 0x8000000000000000) {
          v = ~v;
        } else {
          v ^= 0x8000000000000000;
        }
        if(value == 0) v = 0x8000000000000000;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        v = __builtin_bswap64(v);
#endif
        stream_.write((char*)&v, sizeof(v));
      }
      void write_f32(float value) {
        debug("write_f32 %f", value);
        std::uint32_t v = *(std::uint32_t*)(&value);
        if(v & 0x80000000) {
          v = ~v;
        } else {
          v ^= 0x80000000;
        }
        if(value == 0) v = 0x80000000;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        v = __builtin_bswap32(v);
#endif
        stream_.write((char*)&v, sizeof(v));
      }
      void write_bool(bool value) {
        debug("write_bool %d", value);
        uint8_t number = value ? 1 : 0;
        stream_.write((char*)&number, 1);
      }
      void write_string(const std::string_view value) {
        debug("write_string %zd '%s'", value.size(), value.data());
        write_uvar_int_32(static_cast<std::uint32_t>(value.size()));
        stream_.write(value.data(), value.size());
      };
      void write_binary(const std::vector<std::uint8_t>&& value) {
        debug("write_binary %zd", value.size());
        write_uvar_int_32(static_cast<std::uint32_t>(value.size()));
        stream_.write((char*)(value.data()), (size_t)(value.size()));
      }

      void write_length(std::size_t length) {
        debug("write_length %zd", length);
        write_uvar_int_32(static_cast<std::uint32_t>(length));
      }

      template <class Map, class KeyCb, class ValCb>
      void write_map(const Map& map, KeyCb&& key_cb, ValCb&& val_cb) {
        write_length(map.size());
        for(const auto& element : map) {
          key_cb(*this, element.first);
          val_cb(*this, element.second);
        }
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
        size_t index = it->second;
        write_uvar_int_32(static_cast<std::uint32_t>(index));
        serialize_exception_functions_[index](e);
      }
      
      template <typename T>
      void write_typed(std::shared_ptr<T> data) {        
        auto type_index = std::type_index(typeid(*data));
        debug("write_typed %s", type_index.name());
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
        debug("write_message %s", type_index.name());
        auto it = message_indexes_.find(type_index);
        if(it == message_indexes_.end()) {
          throw std::runtime_error("Type not registered");
        }
        size_t index = it->second;
        write_uvar_int_32(static_cast<std::uint32_t>(index));
        serialize_message_functions_[index](&data);
      }

      void save_offset() { /* ignore */ }
      
      void write_rest() {        
        debug("write_rest %zd", write_later_.size());
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
        debug("write_pointer %p", value.get());
        if(!value) {
          write_svar_int_32(0);  // null = 0
          return;
        }
        auto existing = std::find(written_pointers_.begin(), written_pointers_.end(), value);
        if (existing != written_pointers_.end()) {
          std::int32_t index = static_cast<std::int32_t>(std::distance(written_pointers_.begin(), existing));
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

    private:

      std::ostream& stream_;

      std::vector<std::shared_ptr<void>> written_pointers_;
      std::vector<std::pair<std::shared_ptr<void>, WriteLater>> write_later_;

      std::vector<std::function<void(void*)>> serialize_typed_functions_;
      std::unordered_map<std::type_index, size_t> typed_indexes_;

      std::vector<std::function<void(const std::exception&)>> serialize_exception_functions_;
      std::unordered_map<std::type_index, size_t> exception_indexes_;

      std::vector<std::function<void(void*)>> serialize_message_functions_;
      std::unordered_map<std::type_index, size_t> message_indexes_;

      #ifdef SERIALIZATION_STREAM_WRITER_DEBUG
      uint32_t indent_ = 0;
      #endif

      inline void debug_indent(int delta) {
        #ifdef SERIALIZATION_STREAM_WRITER_DEBUG
        indent_ += delta;        
        #endif
      }

      inline void debug(const char* format, ...) {
        #ifdef SERIALIZATION_STREAM_WRITER_DEBUG
        va_list args;
        va_start(args, format);
        fprintf(stderr, "at %04zx: ", (size_t)stream_.tellp());
        fprintf(stderr, "%s", std::string(indent_*2, ' ').c_str());
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
        va_end(args);
        #endif
      }

      void write_uvar_int(std::uint64_t value) {        
        debug("  write_uvar_int %d", value);
        if(value < 0x80) { // 7 bit number encoded in 1 byte
          uint8_t number = value;
          stream_.write((char*)&number, 1);
        } else if(value < 0x4000) { // 14 bit number encoded in 2 bytes         
          uint8_t number[2] = {(uint8_t)(0x80 | (value >> 8)), (uint8_t)(value & 0xFF)};
          stream_.write((char*)number, 2);
        } else if(value < 0x200000) { // 21 bit number encoded in 3 bytes
          uint8_t number[3] = {(uint8_t)(0xC0 | (value >> 16)), (uint8_t)((value >> 8) & 0xFF), (uint8_t)(value & 0xFF)};
          stream_.write((char*)number, 3);
        } else if(value < 0x10000000) { // 28 bit number encoded in 4 bytes
          uint8_t number[4] = {(uint8_t)(0xE0 | (value >> 24)), (uint8_t)((value >> 16) & 0xFF), (uint8_t)((value >> 8) & 0xFF), (uint8_t)(value & 0xFF)};
          stream_.write((char*)number, 4);
        } else if(value < 0x800000000) { // 35 bit number encoded in 5 bytes
          uint8_t number[5] = {(uint8_t)(0xF0 | (value >> 32)), (uint8_t)((value >> 24) & 0xFF), (uint8_t)((value >> 16) & 0xFF), (uint8_t)((value >> 8) & 0xFF),
              (uint8_t)(value & 0xFF)};
          stream_.write((char*)number, 5);
        } else if(value < 0x40000000000) { // 42 bit number encoded in 6 bytes
          uint8_t number[6] = {(uint8_t)(0xF8 | (value >> 40)), (uint8_t)((value >> 32) & 0xFF), (uint8_t)((value >> 24) & 0xFF), (uint8_t)((value >> 16) & 0xFF),
              (uint8_t)((value >> 8) & 0xFF), (uint8_t)(value & 0xFF)};
          stream_.write((char*)number, 6);
        } else if(value < 0x2000000000000) { // 49 bit number encoded in 7 bytes
          uint8_t number[7] = {(uint8_t)(0xFC | (value >> 48)), (uint8_t)((value >> 40) & 0xFF), (uint8_t)((value >> 32) & 0xFF), (uint8_t)((value >> 24) & 0xFF),
              (uint8_t)((value >> 16) & 0xFF), (uint8_t)((value >> 8) & 0xFF), (uint8_t)(value & 0xFF)};
          stream_.write((char*)number, 7);
        } else if(value < 0x100000000000000) { // 56 bit number encoded in 8 bytes
          uint8_t number[8] = {(uint8_t)(0xFE), (uint8_t)((value >> 48) & 0xFF), (uint8_t)((value >> 40) & 0xFF), (uint8_t)((value >> 32) & 0xFF), (uint8_t)((value >> 24) & 0xFF),
              (uint8_t)((value >> 16) & 0xFF), (uint8_t)((value >> 8) & 0xFF), (uint8_t)(value & 0xFF)};
          stream_.write((char*)number, 8);
        } else { // 64 bit number encoded in 9 bytes
          uint8_t number[9] = {(uint8_t)(0xFF), (uint8_t)((value >> 56) & 0xFF), (uint8_t)((value >> 48) & 0xFF), (uint8_t)((value >> 40) & 0xFF), (uint8_t)((value >> 32) & 0xFF),
                               (uint8_t)((value >> 24) & 0xFF), (uint8_t)((value >> 16) & 0xFF), (uint8_t)((value >> 8) & 0xFF), (uint8_t)(value & 0xFF)};
          stream_.write((char*)number, 9);
        }
      }

      void write_svar_int(std::int64_t value) {
        debug("  write_svar_int %d", value);
        bool negative = value < 0;
        std::uint64_t unsigned_value = negative ? (std::uint64_t)(-value) : (std::uint64_t)value;

        if(negative) {
          // Negative: length = leading zeros then a one. Prefix: 01 (1B), 001 (2B), ...
          if(unsigned_value < 0x40) { // 1 byte, 6 bit. Prefix 01.
            uint8_t number = (uint8_t)(0x40 | ((~unsigned_value) & 0x3F));
            stream_.write((char*)&number, 1);
          } else if(unsigned_value < 0x2000) { // 2 bytes, 13 bit. Prefix 001.
            uint8_t number[2] = {
                (uint8_t)(0x20 | (((~unsigned_value) >> 8) & 0x1F)),
                (uint8_t)((~unsigned_value) & 0xFF)};
            stream_.write((char*)number, 2);
          } else if(unsigned_value < 0x100000) { // 3 bytes, 20 bit. Prefix 0001.
            uint8_t number[3] = {
                (uint8_t)(0x10 | (((~unsigned_value) >> 16) & 0x0F)),
                (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
                (uint8_t)((~unsigned_value) & 0xFF)};
            stream_.write((char*)number, 3);
          } else if(unsigned_value < 0x8000000) { // 4 bytes, 27 bit. Prefix 00001.
            uint8_t number[4] = {
                (uint8_t)(0x08 | (((~unsigned_value) >> 24) & 0x07)),
                (uint8_t)(((~unsigned_value) >> 16) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
                (uint8_t)((~unsigned_value) & 0xFF)};
            stream_.write((char*)number, 4);
          } else if(unsigned_value < 0x400000000) { // 5 bytes, 34 bit. Prefix 000001.
            uint8_t number[5] = {
                (uint8_t)(0x04 | (((~unsigned_value) >> 32) & 0x03)),
                (uint8_t)(((~unsigned_value) >> 24) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 16) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
                (uint8_t)((~unsigned_value) & 0xFF)};
            stream_.write((char*)number, 5);
          } else if(unsigned_value < 0x20000000000) { // 6 bytes, 41 bit. Prefix 0000001.
            uint8_t number[6] = {
                (uint8_t)(0x02 | (((~unsigned_value) >> 40) & 0x01)),
                (uint8_t)(((~unsigned_value) >> 32) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 24) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 16) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
                (uint8_t)((~unsigned_value) & 0xFF)};
            stream_.write((char*)number, 6);
          } else if(unsigned_value < 0x1000000000000) { // 7 bytes, 48 bit. Prefix 00000001.
            uint8_t number[7] = {
                (uint8_t)(0x01),
                (uint8_t)(((~unsigned_value) >> 40) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 32) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 24) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 16) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
                (uint8_t)((~unsigned_value) & 0xFF)};
            stream_.write((char*)number, 7);
          } else { // 9 bytes, 64 bit. Prefix 00000000.
            uint8_t number[9] = {
                (uint8_t)(0x00),
                (uint8_t)(((~unsigned_value) >> 56) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 48) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 40) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 32) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 24) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 16) & 0xFF),
                (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
                (uint8_t)((~unsigned_value) & 0xFF)};
            stream_.write((char*)number, 9);
          }
        } else {
          // Positive: length = leading ones then a zero. Prefix: 10 (1B), 110 (2B), ...
          if(unsigned_value < 0x40) { // 1 byte, 6 bit. Prefix 10.
            uint8_t number = (uint8_t)(0x80 | unsigned_value);
            stream_.write((char*)&number, 1);
          } else if(unsigned_value < 0x2000) { // 2 bytes, 13 bit. Prefix 110.
            uint8_t number[2] = {
                (uint8_t)(0x80 | 0x40 | (unsigned_value >> 8)),
                (uint8_t)(unsigned_value & 0xFF)};
            stream_.write((char*)number, 2);
          } else if(unsigned_value < 0x100000) { // 3 bytes, 20 bit. Prefix 1110.
            uint8_t number[3] = {
                (uint8_t)(0x80 | 0x60 | (unsigned_value >> 16)),
                (uint8_t)((unsigned_value >> 8) & 0xFF),
                (uint8_t)(unsigned_value & 0xFF)};
            stream_.write((char*)number, 3);
          } else if(unsigned_value < 0x8000000) { // 4 bytes, 27 bit. Prefix 11110.
            uint8_t number[4] = {
                (uint8_t)(0x80 | 0x70 | (unsigned_value >> 24)),
                (uint8_t)((unsigned_value >> 16) & 0xFF),
                (uint8_t)((unsigned_value >> 8) & 0xFF),
                (uint8_t)(unsigned_value & 0xFF)};
            stream_.write((char*)number, 4);
          } else if(unsigned_value < 0x400000000) { // 5 bytes, 34 bit. Prefix 111110.
            uint8_t number[5] = {
                (uint8_t)(0x80 | 0x78 | (unsigned_value >> 32)),
                (uint8_t)((unsigned_value >> 24) & 0xFF),
                (uint8_t)((unsigned_value >> 16) & 0xFF),
                (uint8_t)((unsigned_value >> 8) & 0xFF),
                (uint8_t)(unsigned_value & 0xFF)};
            stream_.write((char*)number, 5);
          } else if(unsigned_value < 0x20000000000) { // 6 bytes, 41 bit. Prefix 1111110.
            uint8_t number[6] = {
                (uint8_t)(0x80 | 0x7C | (unsigned_value >> 40)),
                (uint8_t)((unsigned_value >> 32) & 0xFF),
                (uint8_t)((unsigned_value >> 24) & 0xFF),
                (uint8_t)((unsigned_value >> 16) & 0xFF),
                (uint8_t)((unsigned_value >> 8) & 0xFF),
                (uint8_t)(unsigned_value & 0xFF)};
            stream_.write((char*)number, 6);
          } else if(unsigned_value < 0x1000000000000) { // 7 bytes, 48 bit. Prefix 11111110.
            uint8_t number[7] = {
                (uint8_t)(0x80 | 0x7E),
                (uint8_t)((unsigned_value >> 40) & 0xFF),
                (uint8_t)((unsigned_value >> 32) & 0xFF),
                (uint8_t)((unsigned_value >> 24) & 0xFF),
                (uint8_t)((unsigned_value >> 16) & 0xFF),
                (uint8_t)((unsigned_value >> 8) & 0xFF),
                (uint8_t)(unsigned_value & 0xFF)};
            stream_.write((char*)number, 7);
          } else { // 9 bytes, 64 bit. Prefix 11111111.
            uint8_t number[9] = {
                (uint8_t)(0x80 | 0x7F),
                (uint8_t)((unsigned_value >> 56) & 0xFF),
                (uint8_t)((unsigned_value >> 48) & 0xFF),
                (uint8_t)((unsigned_value >> 40) & 0xFF),
                (uint8_t)((unsigned_value >> 32) & 0xFF),
                (uint8_t)((unsigned_value >> 24) & 0xFF),
                (uint8_t)((unsigned_value >> 16) & 0xFF),
                (uint8_t)((unsigned_value >> 8) & 0xFF),
                (uint8_t)(unsigned_value & 0xFF)};
            stream_.write((char*)number, 9);
          }
        }
      }

      void write_uvar_int_32(std::uint32_t value) {
        if (value < 0x80) {
          uint8_t number = static_cast<uint8_t>(value);
          stream_.write((char*)&number, 1);
        } else if (value < 0x4000) {
          uint8_t number[2] = {static_cast<uint8_t>(0x80 | (value >> 8)), static_cast<uint8_t>(value & 0xFF)};
          stream_.write((char*)number, 2);
        } else if (value < 0x200000) {
          uint8_t number[3] = {static_cast<uint8_t>(0xC0 | (value >> 16)), static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF)};
          stream_.write((char*)number, 3);
        } else if (value < 0x10000000) {
          uint8_t number[4] = {static_cast<uint8_t>(0xE0 | (value >> 24)), static_cast<uint8_t>((value >> 16) & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF)};
          stream_.write((char*)number, 4);
        } else {
          uint8_t number[5] = {0xF0, static_cast<uint8_t>((value >> 24) & 0xFF), static_cast<uint8_t>((value >> 16) & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF)};
          stream_.write((char*)number, 5);
        }
      }

      void write_svar_int_32(std::int32_t value) {
        std::uint32_t u = (value < 0) ? static_cast<std::uint32_t>(-static_cast<std::uint32_t>(value)) : static_cast<std::uint32_t>(value);
        if (value < 0) {
          if (u <= 0x3F) {
            uint8_t number = static_cast<uint8_t>(0x40 | ((~u) & 0x3F));
            stream_.write((char*)&number, 1);
          } else if (u <= 0x1FFF) {
            uint8_t number[2] = {static_cast<uint8_t>(0x20 | (((~u) >> 8) & 0x1F)), static_cast<uint8_t>((~u) & 0xFF)};
            stream_.write((char*)number, 2);
          } else if (u <= 0xFFFFF) {
            uint8_t number[3] = {static_cast<uint8_t>(0x10 | (((~u) >> 16) & 0x0F)), static_cast<uint8_t>(((~u) >> 8) & 0xFF), static_cast<uint8_t>((~u) & 0xFF)};
            stream_.write((char*)number, 3);
          } else if (u <= 0x7FFFFFF) {
            uint8_t number[4] = {static_cast<uint8_t>(0x08 | (((~u) >> 24) & 0x07)), static_cast<uint8_t>(((~u) >> 16) & 0xFF), static_cast<uint8_t>(((~u) >> 8) & 0xFF), static_cast<uint8_t>((~u) & 0xFF)};
            stream_.write((char*)number, 4);
          } else {
            uint8_t number[5] = {0x07, static_cast<uint8_t>(((~u) >> 24) & 0xFF), static_cast<uint8_t>(((~u) >> 16) & 0xFF), static_cast<uint8_t>(((~u) >> 8) & 0xFF), static_cast<uint8_t>((~u) & 0xFF)};
            stream_.write((char*)number, 5);
          }
        } else {
          if (u <= 0x3F) {
            uint8_t number = static_cast<uint8_t>(0x80 | u);
            stream_.write((char*)&number, 1);
          } else if (u <= 0x1FFF) {
            uint8_t number[2] = {static_cast<uint8_t>(0xC0 | (u >> 8)), static_cast<uint8_t>(u & 0xFF)};
            stream_.write((char*)number, 2);
          } else if (u <= 0xFFFFF) {
            uint8_t number[3] = {static_cast<uint8_t>(0xE0 | (u >> 16)), static_cast<uint8_t>((u >> 8) & 0xFF), static_cast<uint8_t>(u & 0xFF)};
            stream_.write((char*)number, 3);
          } else if (u <= 0x7FFFFFF) {
            uint8_t number[4] = {static_cast<uint8_t>(0xF0 | (u >> 24)), static_cast<uint8_t>((u >> 16) & 0xFF), static_cast<uint8_t>((u >> 8) & 0xFF), static_cast<uint8_t>(u & 0xFF)};
            stream_.write((char*)number, 4);
          } else {
            uint8_t number[5] = {0xF8, static_cast<uint8_t>((u >> 24) & 0xFF), static_cast<uint8_t>((u >> 16) & 0xFF), static_cast<uint8_t>((u >> 8) & 0xFF), static_cast<uint8_t>(u & 0xFF)};
            stream_.write((char*)number, 5);
          }
        }
      }

      void write_uvar_int_16(std::uint16_t value) {
        if (value < 0x80) {
          uint8_t number = static_cast<uint8_t>(value);
          stream_.write((char*)&number, 1);
        } else if (value < 0x4000) {
          uint8_t number[2] = {static_cast<uint8_t>(0x80 | (value >> 8)), static_cast<uint8_t>(value & 0xFF)};
          stream_.write((char*)number, 2);
        } else {
          uint8_t number[3] = {0xC0, static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF)};
          stream_.write((char*)number, 3);
        }
      }

      void write_svar_int_16(std::int16_t value) {
        std::uint16_t u = (value < 0) ? static_cast<std::uint16_t>(-static_cast<std::uint16_t>(value)) : static_cast<std::uint16_t>(value);
        if (value < 0) {
          if (u <= 0x3F) {
            uint8_t number = static_cast<uint8_t>(0x40 | ((~u) & 0x3F));
            stream_.write((char*)&number, 1);
          } else if (u <= 0x1FFF) {
            uint8_t number[2] = {static_cast<uint8_t>(0x20 | (((~u) >> 8) & 0x1F)), static_cast<uint8_t>((~u) & 0xFF)};
            stream_.write((char*)number, 2);
          } else {
            std::uint32_t encoded = static_cast<std::uint32_t>(0xF0000) + static_cast<std::uint32_t>(0xFFFF - u);
            uint8_t number[3] = {static_cast<uint8_t>(0x10 | (encoded >> 16)), static_cast<uint8_t>((encoded >> 8) & 0xFF), static_cast<uint8_t>(encoded & 0xFF)};
            stream_.write((char*)number, 3);
          }
        } else {
          if (u <= 0x3F) {
            uint8_t number = static_cast<uint8_t>(0x80 | u);
            stream_.write((char*)&number, 1);
          } else if (u <= 0x1FFF) {
            uint8_t number[2] = {static_cast<uint8_t>(0xC0 | (u >> 8)), static_cast<uint8_t>(u & 0xFF)};
            stream_.write((char*)number, 2);
          } else {
            uint8_t number[3] = {0xE0, static_cast<uint8_t>((u >> 8) & 0xFF), static_cast<uint8_t>(u & 0xFF)};
            stream_.write((char*)number, 3);
          }
        }
      }
  };
}
