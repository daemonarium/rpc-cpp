#pragma once

#include "reader.hpp"
#include <cstdint>
#include <istream>
#include <typeindex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

#define SERIALIZATION_STREAM_READER_DEBUG

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
      void read_f64(double& value) {
        debug("read_f64");
        std::uint64_t v;
        stream_.read((char*)&v, sizeof(v));
        if(v & 0x8000000000000000) {
          v ^= 0x8000000000000000;
        } else {
          v = ~v;
        }
        value = *(double*)(&v);
        debug("  read_f64 result %f", value);
      }
      void read_bool(bool& value) {
        debug("read_bool");
        value = stream_.get() != 0;
        debug("  read_bool result %d", value);
      }
      void read_string(std::string& value) {
        debug("read_string");
        size_t length = read_uvar_int();
        value.resize(length);
        stream_.read(value.data(), length);
        debug("  read_string result %zd '%s'", length, value.c_str());
      }
      void read_binary(std::string& value) {
        debug("read_binary");
        size_t length = read_uvar_int();
        value.resize(length);
        stream_.read(value.data(), length);
        debug("  read_binary result %zd", length);
      }
      void read_length(std::size_t& value) {
        debug("read_length");
        value = read_uvar_int();
        debug("  read_length result %zd", value);
      }

      template <typename T>
      void read_typed(std::shared_ptr<T>& data) {
        debug("read_typed");
        size_t index = read_uvar_int();
        void* ptr = &data;
        debug("  read_typed type %d", index);
        deserialize_functions_[index](ptr);        
      }

      void read_typed(std::weak_ptr<void>& data) {
        debug("read_weak_ptr");
        std::shared_ptr<void> shared;
        read_typed(shared);
        data = shared;
        debug("  read_weak_ptr result %p", data.lock().get());
      }

      void register_deserialization_function(std::type_index type, 
          std::function<void(void*)> deserialize) {
        type_indexes_[type] = deserialize_functions_.size();
        deserialize_functions_.push_back(deserialize);
      }

      template <typename T>
      void register_deserialization_function() {
        auto type = std::type_index(typeid(T));        
        register_deserialization_function(type, [&reader = *this](void* ptr) {
          std::shared_ptr<T>& shared = *((std::shared_ptr<T>*)ptr);
          std::shared_ptr<T> data = std::make_shared<T>();
          deserialize(reader, *data);
          shared = data;
        });
      }

      template <typename T>
      void read_pointer(std::shared_ptr<T>& value, ReadLater read_later) {
        debug("read_pointer");
        std::int64_t delta = read_svar_int();
        if(delta < 0) { /// already read
          size_t index = read_pointers_.size() - delta;
          value = std::static_pointer_cast<T>(read_pointers_[index]);
          debug("  read_pointer - existsing %p", value.get());
          return;
        } 
        AssignAfterRead assign = [&value](std::shared_ptr<void>& data) {
          value = std::static_pointer_cast<T>(data);
        };
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

    private:
      std::istream& stream_;

      std::vector<std::shared_ptr<void>> read_pointers_;

      using AssignAfterRead = std::function<void(std::shared_ptr<void>&)>;
      std::vector<std::pair<std::vector<AssignAfterRead>, ReadLater>> read_later_;

      std::vector<std::function<void(void*)>> deserialize_functions_;
      std::unordered_map<std::type_index, size_t> type_indexes_;

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
        uint8_t wtf = number[0] & 0x80;
        if((number[0] & 0x80) == 0) { // 7 bit number encoded in 1 byte
          return number[0];
        } else if ((number[0] & 0x40) == 0) { // 14 bit number encoded in 2 bytes
          number[1] = stream_.get();
          return ((number[0] & 0x3F) << 8) | number[1];
        } else if ((number[0] & 0x20) == 0) { // 21 bit number encoded in 3 bytes
          stream_.read((char*)number + 1, 2);
          return ((number[0] & 0x1F) << 16) | (number[1] << 8) | number[2];
        } else if ((number[0] & 0x10) == 0) { // 28 bit number encoded in 4 bytes
          stream_.read((char*)number + 1, 3);
          return ((number[0] & 0x0F) << 24) | (number[1] << 16) | (number[2] << 8) | number[3];
        } else if ((number[0] & 0x08) == 0) { // 35 bit number encoded in 5 bytes
          stream_.read((char*)number + 1, 4);
          return ((uint64_t)(number[0] & 0x07) << 32) | (number[1] << 24) | (number[2] << 16) | (number[3] << 8)
              | number[4];
        } else if((number[0] & 0x04) == 0) { // 42 bit number encoded in 6 bytes
          stream_.read((char*)number + 1, 5);
          return ((uint64_t)(number[0] & 0x03) << 40) | ((uint64_t)number[1] << 32) | (number[2] << 24) | (number[3] << 16)
              | (number[4] << 8) | number[5];
        } else if((number[0] & 0x02) == 0) { // 49 bit number encoded in 7 bytes
          stream_.read((char*)number + 1, 6);
          return ((uint64_t)(number[0] & 0x01) << 48) | ((uint64_t)number[1] << 40) | ((uint64_t)number[2] << 32)
              | (number[3] << 24) | (number[4] << 16) | (number[5] << 8) | number[6];
        } else if((number[0] & 0x01) == 0) { // 56 bit number encoded in 8 bytes
          stream_.read((char*)number + 1, 7);
          return ((uint64_t)number[1] << 48) | ((uint64_t)number[2] << 40) | ((uint64_t)number[3] << 32)
              | (number[4] << 24) | (number[5] << 16) | (number[6] << 8) | number[7];
        } else { // 64 bit number encoded in 9 bytes
          stream_.read((char*)number + 1, 8);
          return ((uint64_t)number[1] << 56) | ((uint64_t)number[2] << 48) | ((uint64_t)number[3] << 40)
              | ((uint64_t)number[4] << 32) | (number[5] << 24) | (number[6] << 16) | (number[7] << 8) | number[8];
        }
      }

      std::int64_t read_svar_int() {
        uint8_t number[9];
        number[0] = stream_.get();
        bool negative = (number[0] & 0x80) == 0;
        if((number[0] & 0x40) == 0) { // 6 bit signed number encoded in 1 byte
          return negative 
            ? -((~number[0]) & 0x3F)
            : (number[0] & 0x3F);
        } else if((number[0] & 0x20) == 0) { // 13 bit signed number encoded in 2 bytes
          number[1] = stream_.get();
          return negative
            ? -(~(((number[0] & 0x1F) << 8) | number[1]) & 0x3FFF)
            : ((number[0] & 0x1F) << 8) | number[1];
        } else if((number[0] & 0x10) == 0) { // 20 bit signed number encoded in 3 bytes
          stream_.read((char*)number + 1, 2);
          return negative
            ? -(~(((number[0] & 0x0F) << 16) | (number[1] << 8) | number[2]) & 0x1FFFF)
            : (((number[0] & 0x0F) << 16) | (number[1] << 8) | number[2]);
        } else if((number[0] & 0x08) == 0) { // 27 bit signed number encoded in 4 bytes
          stream_.read((char*)number + 1, 3);
          return negative
            ? -(~(((number[0] & 0x07) << 24) | (number[1] << 16) | (number[2] << 8) | number[3]) & 0xFFFFFF)
            : (((number[0] & 0x07) << 24) | (number[1] << 16) | (number[2] << 8) | number[3]);
        } else if((number[0] & 0x04) == 0) { // 34 bit signed number encoded in 5 bytes
          stream_.read((char*)number + 1, 4);
          return negative
            ? -(~(((std::uint64_t)(number[0] & 0x03) << 32) | (number[1] << 24) | (number[2] << 16) | (number[3] << 8) | number[4]) & 0x7FFFFFFF)
            : (((std::uint64_t)(number[0] & 0x03) << 32) | (number[1] << 24) | (number[2] << 16) | (number[3] << 8) | number[4]);
        } else if((number[0] & 0x02) == 0) { // 41 bit signed number encoded in 6 bytes
          stream_.read((char*)number + 1, 5);
          return negative
            ? -(~(((std::uint64_t)(number[0] & 0x01) << 40) | ((std::uint64_t)number[1] << 32) | (number[2] << 24) | (number[3] << 16) | (number[4] << 8) | number[5]) & 0x7FFFFFFFFF)
            : (((std::uint64_t)(number[0] & 0x01) << 40) | ((std::uint64_t)number[1] << 32) | (number[2] << 24) | (number[3] << 16) | (number[4] << 8) | number[5]);
        } else if((number[0] & 0x01) == 0) { // 48 bit signed number encoded in 7 bytes
          stream_.read((char*)number + 1, 6);
          return negative
            ? -(~(((std::uint64_t)(number[0] & 0x00) << 48) | ((std::uint64_t)number[1] << 40) | ((std::uint64_t)number[2] << 32) | (number[3] << 24) | (number[4] << 16) | (number[5] << 8) | number[6]) & 0x7FFFFFFFFFFFFF)
            : (((std::uint64_t)(number[0] & 0x00) << 48) | ((std::uint64_t)number[1] << 40) | ((std::uint64_t)number[2] << 32) | (number[3] << 24) | (number[4] << 16) | (number[5] << 8) | number[6]);
        } else { // 64 bit signed number encoded in 9 bytes
          stream_.read((char*)number + 1, 8);
          return negative
            ? -(~(((std::uint64_t)number[1] << 56) | ((std::uint64_t)number[2] << 48) | ((std::uint64_t)number[3] << 40) | ((std::uint64_t)number[4] << 32) | (number[5] << 24) | (number[6] << 16) | (number[7] << 8) | number[8]) & 0x7FFFFFFFFFFFFFFF)
            : (((std::uint64_t)number[1] << 56) | ((std::uint64_t)number[2] << 48) | ((std::uint64_t)number[3] << 40) | ((std::uint64_t)number[4] << 32) | (number[5] << 24) | (number[6] << 16) | (number[7] << 8) | number[8]);
        }
      }
    };
  }