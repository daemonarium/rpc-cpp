#pragma once

#include "writer.hpp"

#include <vector>
#include <iostream>
#include <memory>
#include <functional>
#include <typeindex>
#include <unordered_map>

#ifndef __EMSCRIPTEN__
  #include <cxxabi.h>
#endif

namespace serialization {

  class debug_writer : public writer {
    public:
      debug_writer(std::ostream& stream) : stream_(stream), frame_offsets_() {
        frame_offsets_.push_back(0);
      }
      ~debug_writer() {}

      void begin_frame() {
        stream_ << std::string(indent_, ' ') << "beginFrame" << std::endl;
        indent_+=2;
      }
      void end_frame() {
        indent_-=2;
        stream_ << std::string(indent_, ' ') << "endFrame" << std::endl;
      }

      void write_u64(std::uint64_t value) {
        stream_ << std::string(indent_, ' ') << "writeU64 " << value << std::endl;
      }
      void write_i64(std::int64_t value) {
        stream_ << std::string(indent_, ' ') << "writeI64 " << value << std::endl;
      }
      void write_f64(double value) {
        stream_ << std::string(indent_, ' ') << "writeF64 " << value << std::endl;
      }
      void write_string(std::string_view value) {
        stream_ << std::string(indent_, ' ') << "writeString " << value << std::endl;
      }
      void write_bool(bool value) {
        stream_ << std::string(indent_, ' ') << "writeBool " << value << std::endl;
      }
      void write_binary(const std::vector<std::uint8_t>&& value) {
        stream_ << std::string(indent_, ' ') << "writeBinary " << value.size() << std::endl;
      }
      void write_length(std::size_t length) {
        stream_ << std::string(indent_, ' ') << "writeLength " << length << std::endl;
      }
      void save_offset() {
        stream_ << std::string(indent_, ' ') << "saveOffset" << std::endl;
      }

      template <typename T>
      void write_pointer(std::shared_ptr<T> value, WriteLater write_later) {
        stream_ << std::string(indent_, ' ') << "writePointer " << value << std::endl;
        if(!value) {
          stream_ << std::string(indent_+2, ' ') << "pointer is null - writing 0" << std::endl;
          return;
        }
        auto existing = std::find(written_pointers_.begin(), written_pointers_.end(), value);
        if (existing != written_pointers_.end()) {
          size_t index = std::distance(written_pointers_.begin(), existing);
          stream_ << std::string(indent_+2, ' ') << "pointer already written - writing index: " 
          << index << std::endl;          
          return;
        }
        auto will_be_written = std::find_if(write_later_.begin(), write_later_.end(), 
        [value](const auto& pair) {
            return pair.first == value;
          }
        );
        if (will_be_written != write_later_.end()) {
          size_t index = written_pointers_.size() + std::distance(write_later_.begin(), will_be_written);
          stream_ << std::string(indent_+2, ' ') << "pointer already in write_later queue - writing index: " 
            << index << std::endl;
          return;
        }
        size_t index = written_pointers_.size() + write_later_.size();
        stream_ << std::string(indent_+2, ' ') << "pointer not written - writing future index: "
          << (index + 1) << std::endl;          
        write_later_.push_back({value, write_later});
      }

      template <typename T>      
      void write_pointer(std::weak_ptr<T> value, WriteLater write_later) {
        write_pointer(value.lock(), write_later);
      }

      template <typename T>
      void write_typed(std::shared_ptr<T> data) {
        auto type_index = std::type_index(typeid(*data));
        int status;
#ifndef __EMSCRIPTEN__
        char* demangled =  abi::__cxa_demangle(type_index.name(), nullptr, nullptr, &status);
#else
        const char* demangled = type_index.name()+1;
#endif
        stream_ << std::string(indent_, ' ') << "writeTyped " << (demangled ? demangled : type_index.name()) << std::endl;
        indent_ += 2;
        auto it = typed_indexes_.find(type_index);
        if(it == typed_indexes_.end()) {
          stream_ << std::string(indent_, ' ') << "writeTyped " << (demangled ? demangled : type_index.name()) << " not registered" << std::endl;
          throw std::runtime_error("Type not registered");
        }
        size_t index = it->second;
        stream_ << std::string(indent_, ' ') << "type index: " << index << std::endl;
        void* ptr = &data;
        serialize_typed_functions_[index](ptr);        
        indent_ -= 2;
#ifndef __EMSCRIPTEN__
        free(demangled);
#endif
      }

      template <typename T>
      void write_message(std::shared_ptr<T> data) {
        auto type_index = std::type_index(typeid(*data));
        int status;
#ifndef __EMSCRIPTEN__
        char* demangled =  abi::__cxa_demangle(type_index.name(), nullptr, nullptr, &status);
#else
        const char* demangled = type_index.name()+1;
#endif
        stream_ << std::string(indent_, ' ') << "writeMessage " << (demangled ? demangled : type_index.name()) << std::endl;
        indent_ += 2;
        auto it = message_indexes_.find(type_index);
        if(it == message_indexes_.end()) {
          stream_ << std::string(indent_, ' ') << "writeMessage " << (demangled ? demangled : type_index.name()) << " not registered" << std::endl;
          throw std::runtime_error("Type not registered");
        }
        size_t index = it->second;
        stream_ << std::string(indent_, ' ') << "message index: " << index << std::endl;
        void* ptr = &data;
        serialize_message_functions_[index](ptr);        
        indent_ -= 2;
#ifndef __EMSCRIPTEN__
        free(demangled);
#endif
      }

      void write_rest() {
        stream_ << std::string(indent_, ' ') << "writeRest" << std::endl;
        while(!write_later_.empty()) {
          auto pair = write_later_.front();
          stream_ << std::string(indent_+2, ' ') << "write later: " << pair.first << std::endl;
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

    private:
      std::ostream& stream_;
      int indent_ = 0;
      std::vector<std::size_t> frame_offsets_;

      std::vector<std::shared_ptr<void>> written_pointers_;
      std::vector<std::pair<std::shared_ptr<void>, WriteLater>> write_later_;

      std::vector<std::function<void(void*)>> serialize_typed_functions_;
      std::unordered_map<std::type_index, size_t> typed_indexes_;

      std::vector<std::function<void(void*)>> serialize_message_functions_;
      std::unordered_map<std::type_index, size_t> message_indexes_;
  };
}