#pragma once

#include "reader.hpp"
#include "deserialize.hpp"
#include "flat_encoding.hpp"
#include "type_concepts.hpp"

#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace serialization {

  // CRTP base for flat readers. Factors out everything duplicated between
  // flat_stream_reader and flat_buffer_reader: the shared_ptr graph machinery
  // (read_pointers_/read_later_/skip_pointers_, mark_read_pointer,
  // make_shared_read_later, read_pointer, read_rest), scalar reads,
  // string/binary/length, and the typed/exception/message registration +
  // dispatch tables.
  //
  // Derived classes must provide hooks:
  //   - void read_raw(void* dest, std::size_t size)  — raw byte read (public)
  //
  // Derived classes keep their own Frame struct + frames_ vector + enter_frame/
  // exit_frame / save_offset / read_map (different Frame layouts) and
  // is_indexed / enter_indexed_frame / reset (buffer reader).
  template <typename Derived>
  class flat_reader_base : public reader {
    public:
      using AssignAfterRead = std::function<void(std::shared_ptr<void>&)>;

      // -----------------------------------------------------------------
      // Hook: raw byte read. Derived must provide this (public).
      // -----------------------------------------------------------------
      void read_raw(void* dest, std::size_t size) {
        derived().read_raw(dest, size);
      }

      void set_skip_pointers(bool skip) { skip_pointers_ = skip; }

      // -----------------------------------------------------------------
      // Scalar reads (all LE)
      // -----------------------------------------------------------------
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

      // -----------------------------------------------------------------
      // String / binary / length
      // -----------------------------------------------------------------
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

      // -----------------------------------------------------------------
      // Typed / exception / message registration + dispatch
      // -----------------------------------------------------------------
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

      template <typename T>
      void register_deserialize_exception_function() {
        auto type = std::type_index(typeid(T));
        exception_read_indexes_[type] = deserialize_exception_functions_.size();
        deserialize_exception_functions_.push_back([this]() -> std::exception_ptr {
          if constexpr (SimpleException<T>) {
            T ex("");
            ::serialization::deserialize(this->derived(), ex);
            return std::make_exception_ptr(std::move(ex));
          } else {
            T ex{};
            ::serialization::deserialize(this->derived(), ex);
            return std::make_exception_ptr(std::move(ex));
          }
        });
      }

      std::exception_ptr read_exception() {
        std::uint32_t index = 0;
        read_u32(index);
        if(index >= deserialize_exception_functions_.size()) {
          throw std::runtime_error("Exception type not registered for deserialization");
        }
        return deserialize_exception_functions_[index]();
      }

      template <typename T>
      void read_message(std::shared_ptr<T>& data) {
        std::uint32_t index = 0;
        read_u32(index);
        if(index >= deserialize_message_functions_.size()) {
          throw std::runtime_error("Message type not registered for deserialization");
        }
        void* ptr = &data;
        deserialize_message_functions_[index](ptr);
      }

      void read_message(std::weak_ptr<void>& data) {
        std::shared_ptr<void> shared;
        read_message(shared);
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
        register_deserialize_typed_function(type, [this](void* ptr) {
          std::shared_ptr<T>& shared = *((std::shared_ptr<T>*)ptr);
          std::shared_ptr<T> data = std::make_shared<T>();
          deserialize(this->derived(), *data);
          shared = data;
        });
      }

      void register_deserialize_message_function(std::type_index type,
          std::function<void(void*)> deserialize_fn) {
        message_indexes_[type] = deserialize_message_functions_.size();
        deserialize_message_functions_.push_back(std::move(deserialize_fn));
      }

      template <typename T>
      void register_deserialize_message_function() {
        auto type = std::type_index(typeid(T));
        register_deserialize_message_function(type, [this](void* ptr) {
          std::shared_ptr<T>& shared = *((std::shared_ptr<T>*)ptr);
          std::shared_ptr<T> data = std::make_shared<T>();
          deserialize(this->derived(), *data);
          shared = data;
        });
      }

      // -----------------------------------------------------------------
      // shared_ptr graph machinery (from previous 5f)
      // -----------------------------------------------------------------
      void mark_read_pointer(std::shared_ptr<void> value) {
        read_pointers_.push_back(std::move(value));
      }

      template <typename T>
      ReadLater make_shared_read_later() {
        return [this](std::shared_ptr<void>& data) {
          auto typed = std::make_shared<T>();
          deserialize(this->derived(), *typed);
          data = typed;
        };
      }

      void read_rest() {
        while(!read_later_.empty()) {
          ReadLater callbacks = std::move(read_later_.front().second);
          std::uint32_t encoded_size = 0;
          derived().read_u32(encoded_size);
          (void)encoded_size;
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

      template <typename T>
      void read_pointer(std::shared_ptr<T>& value, ReadLater read_later) {
        std::int32_t delta = 0;
        derived().read_i32(delta);
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

    protected:
      Derived& derived() noexcept { return static_cast<Derived&>(*this); }

      std::vector<std::shared_ptr<void>> read_pointers_;
      std::vector<std::pair<std::vector<AssignAfterRead>, ReadLater>> read_later_;
      bool skip_pointers_ = false;

      std::vector<std::function<void(void*)>> deserialize_typed_functions_;
      std::unordered_map<std::type_index, size_t> typed_indexes_;

      std::vector<std::function<void(void*)>> deserialize_message_functions_;
      std::unordered_map<std::type_index, size_t> message_indexes_;

      std::vector<std::function<std::exception_ptr()>> deserialize_exception_functions_;
      std::unordered_map<std::type_index, size_t> exception_read_indexes_;

    private:
      template <typename T>
      T read_le_value() {
        std::uint8_t buf[sizeof(T)];
        read_raw(buf, sizeof(T));
        return flat_load_le<T>(buf);
      }
  };

} // namespace serialization
