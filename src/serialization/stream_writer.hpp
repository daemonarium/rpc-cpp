#pragma once

#include "writer.hpp"
#include <cstdint>
#include <ostream>
#include <typeindex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

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
      void write_f64(double value) { 
        debug("write_f64 %f", value);
        std::uint64_t v = *(std::uint64_t*)(&value);
        if(v & 0x8000000000000000) {
          v ^= 0x8000000000000000;
        } else {
          v = ~v;
        }
        stream_.write((char*)&v, sizeof(v));
       }
      void write_bool(bool value) {
        debug("write_bool %d", value);
        uint8_t number = value ? 1 : 0;
        stream_.write((char*)&number, 1);
      }
      void write_string(const std::string_view value) {
        debug("write_string %zd '%s'", value.size(), value.data());
        write_uvar_int(value.size());
        stream_.write(value.data(), value.size());
      };      
      void write_binary(const std::vector<std::uint8_t>&& value) {
        debug("write_binary %zd", value.size());
        write_uvar_int(value.size());
        stream_.write((char*)(value.data()), (size_t)(value.size()));
      }

      void write_length(std::size_t length) {
        debug("write_length %zd", length);
        write_uvar_int(length);
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
        write_uvar_int(index);
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
        write_uvar_int(index);
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
        register_serialization_function(type, [&writer = *this](void* ptr) {
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
          write_svar_int(0); // null = 0
          return;
        }
        auto existing = std::find(written_pointers_.begin(), written_pointers_.end(), value);
        if (existing != written_pointers_.end()) {
          std::int32_t index = std::distance(written_pointers_.begin(), existing);
          std::int32_t delta = index - written_pointers_.size();
          write_svar_int(delta); // always negative
          return;
        }
        auto will_be_written = std::find_if(write_later_.begin(), write_later_.end(), 
        [value](const auto& pair) {
            return pair.first == value;
          }
        );
        if (will_be_written != write_later_.end()) {
          size_t index = std::distance(write_later_.begin(), will_be_written);
          write_svar_int(index + 1); // always positive
          return;
        }
        size_t index = write_later_.size();
        write_svar_int(index + 1); // always positive
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
        std::uint64_t unsignedValue = negative ? -value : value;
        if(unsignedValue < 0x40) { // 6 bit signed number encoded in 1 byte
          uint8_t number = negative ? (uint8_t)((~unsignedValue) & 0x3F) : (uint8_t)(0x80 | unsignedValue);
          stream_.write((char*)&number, 1);          
        } else if(unsignedValue < 0x2000) { // 13 bit signed number encoded in 2 bytes
          if(negative) {
            uint8_t number[2] = {
              (uint8_t)(0x40 | (((~unsignedValue) >> 8)&0x1F)),
              (uint8_t)(((~unsignedValue) & 0xFF))};
            stream_.write((char*)number, 2);
          } else {
            uint8_t number[2] = {
              (uint8_t)(0x80 | 0x40 | (unsignedValue >> 8)), 
              (uint8_t)(unsignedValue & 0xFF)};
            stream_.write((char*)number, 2);
          }
        } else if(unsignedValue < 0x100000) { // 20 bit signed number encoded in 3 bytes
          if(negative) {
            uint8_t number[3] = {
              (uint8_t)(0x60 | (((~unsignedValue) >> 16)&0x0F)),
              (uint8_t)(((~unsignedValue) >> 8) & 0xFF),
              (uint8_t)(((~unsignedValue) & 0xFF))};
            stream_.write((char*)number, 3);
          } else {
            uint8_t number[3] = {
              (uint8_t)(0x80 | 0x60 | (unsignedValue >> 16)), 
              (uint8_t)((unsignedValue >> 8) & 0xFF),
              (uint8_t)(unsignedValue & 0xFF)};
            stream_.write((char*)number, 3);
          }
        } else if(unsignedValue < 0x8000000) { // 27 bit signed number encoded in 4 bytes
          if(negative) {
            uint8_t number[4] = {
              (uint8_t)(0x70 | (((~unsignedValue) >> 24)&0x07)),
              (uint8_t)(((~unsignedValue) >> 16) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 8) & 0xFF),
              (uint8_t)(((~unsignedValue) & 0xFF))};
            stream_.write((char*)number, 4);
          } else {
            uint8_t number[4] = {
              (uint8_t)(0x80 | 0x70 | (unsignedValue >> 24)), 
              (uint8_t)((unsignedValue >> 16) & 0xFF),
              (uint8_t)((unsignedValue >> 8) & 0xFF),
              (uint8_t)(unsignedValue & 0xFF)};   
            stream_.write((char*)number, 4);
          }
        } else if(unsignedValue < 0x400000000) { // 34 bit signed number encoded in 5 bytes
          if(negative) {
            uint8_t number[5] = {
              (uint8_t)(0x78 | (((~unsignedValue) >> 32)&0x03)),
              (uint8_t)(((~unsignedValue) >> 24) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 16) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 8) & 0xFF),
              (uint8_t)(((~unsignedValue) & 0xFF))};
            stream_.write((char*)number, 5);
          } else {
            uint8_t number[5] = {
              (uint8_t)(0x80 | 0x78 | (unsignedValue >> 32)), 
              (uint8_t)((unsignedValue >> 24) & 0xFF),
              (uint8_t)((unsignedValue >> 16) & 0xFF),
              (uint8_t)((unsignedValue >> 8) & 0xFF),
              (uint8_t)(unsignedValue & 0xFF)};
            stream_.write((char*)number, 5);
          }
        } else if(unsignedValue < 0x20000000000) { // 41 bit signed number encoded in 6 bytes
          if(negative) {
            uint8_t number[6] = {
              (uint8_t)(0x7C | (((~unsignedValue) >> 40)&0x01)),
              (uint8_t)(((~unsignedValue) >> 32) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 24) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 16) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 8) & 0xFF),
              (uint8_t)(((~unsignedValue) & 0xFF))};
            stream_.write((char*)number, 6);
          } else {
            uint8_t number[6] = {
              (uint8_t)(0x80 | 0x7C | (unsignedValue >> 40)),     
              (uint8_t)((unsignedValue >> 32) & 0xFF),
              (uint8_t)((unsignedValue >> 24) & 0xFF),
              (uint8_t)((unsignedValue >> 16) & 0xFF),
              (uint8_t)((unsignedValue >> 8) & 0xFF),
              (uint8_t)(unsignedValue & 0xFF)};
            stream_.write((char*)number, 6);
          }
        } else if(unsignedValue < 0x1000000000000) { // 48 bit signed number encoded in 7 bytes
          if(negative) {
            uint8_t number[7] = {
              (uint8_t)(0x7E),
              (uint8_t)(((~unsignedValue) >> 40) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 32) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 24) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 16) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 8) & 0xFF),
              (uint8_t)(((~unsignedValue) & 0xFF))};
            stream_.write((char*)number, 7);
          } else {
            uint8_t number[7] = {
              (uint8_t)(0x80 | 0x7E), 
              (uint8_t)((unsignedValue >> 40) & 0xFF),
              (uint8_t)((unsignedValue >> 32) & 0xFF),
              (uint8_t)((unsignedValue >> 24) & 0xFF),
              (uint8_t)((unsignedValue >> 16) & 0xFF),
              (uint8_t)((unsignedValue >> 8) & 0xFF),
              (uint8_t)(unsignedValue & 0xFF)};
            stream_.write((char*)number, 7);
          }
        } else { // 64 bit signed number encoded in 9 bytes
          if(negative) {
            uint8_t number[9] = {
              (uint8_t)(0x7F),
              (uint8_t)(((~unsignedValue) >> 56) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 48) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 40) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 32) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 24) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 16) & 0xFF),
              (uint8_t)(((~unsignedValue) >> 8) & 0xFF),
              (uint8_t)(((~unsignedValue) & 0xFF))
            };
            stream_.write((char*)number, 9); 
          } else {
            uint8_t number[9] = {
              (uint8_t)(0x80 | 0x7F),
              (uint8_t)((unsignedValue >> 56) & 0xFF),
              (uint8_t)((unsignedValue >> 40) & 0xFF),
              (uint8_t)((unsignedValue >> 32) & 0xFF),
              (uint8_t)((unsignedValue >> 24) & 0xFF),
              (uint8_t)((unsignedValue >> 16) & 0xFF),
              (uint8_t)((unsignedValue >> 8) & 0xFF),
              (uint8_t)(unsignedValue & 0xFF)
            };
            stream_.write((char*)number, 9);
          }
        }
      }
  };
}
