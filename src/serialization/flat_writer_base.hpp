#pragma once

#include "writer.hpp"
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

namespace serialization {

  // CRTP base for flat writers. Factors out everything duplicated between
  // flat_stream_writer and flat_circular_buffer_writer: the shared_ptr graph
  // machinery (written_pointers_/write_later_, mark_written_pointer,
  // make_shared_write_later, write_pointer, write_rest), frame management
  // (begin_frame/end_frame/save_offset), scalar writes, string/binary/length,
  // write_map, and the typed/exception/message registration + dispatch tables.
  //
  // Derived classes must provide hooks:
  //   - std::size_t current_pos() const        — current write position
  //   - void poke_u32(abs, val)               — back-patch a u32 LE
  //   - void write_raw(const void*, size_t)    — raw byte write (public)
  template <typename Derived>
  class flat_writer_base : public writer {
    public:
      // -----------------------------------------------------------------
      // Hook: raw byte write. Derived must provide this (public).
      // -----------------------------------------------------------------
      void write_raw(const void* data, std::size_t size) {
        derived().write_raw(data, size);
      }

      // -----------------------------------------------------------------
      // Frame management (identical Frame struct in both concrete writers)
      // -----------------------------------------------------------------
      struct Frame {
        std::size_t start;
        std::vector<std::uint32_t> cps;
      };

      void begin_frame() {
        frames_.push_back(Frame{ derived().current_pos(), {} });
      }

      void end_frame() {
        if(frames_.empty()) {
          throw std::runtime_error("flat writer::end_frame without begin_frame");
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
        frame.cps.push_back(static_cast<std::uint32_t>(derived().current_pos() - frame.start));
      }

      // -----------------------------------------------------------------
      // Scalar writes (all LE)
      // -----------------------------------------------------------------
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

      // -----------------------------------------------------------------
      // String / binary / length
      // -----------------------------------------------------------------
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

      // -----------------------------------------------------------------
      // Map (SoA layout)
      // -----------------------------------------------------------------
      template <class Map, class KeyCb, class ValCb>
      void write_map(const Map& map, KeyCb&& key_cb, ValCb&& val_cb) {
        std::vector<std::uint8_t> payload;
        flat_append_map_soa(payload, map, std::forward<KeyCb>(key_cb), std::forward<ValCb>(val_cb));
        if(!payload.empty()) {
          write_raw(payload.data(), payload.size());
        }
      }

      // -----------------------------------------------------------------
      // Typed / exception / message registration + dispatch
      // -----------------------------------------------------------------
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
        serialize_exception_functions_.push_back([this](const std::exception& e) {
          serialize(this->derived(), dynamic_cast<const T&>(e));
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
        register_serialize_typed_function(type, [this](void* ptr) {
          std::shared_ptr<T> data = *((std::shared_ptr<T>*)ptr);
          serialize(this->derived(), *data);
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
        register_serialize_message_function(type, [this](void* ptr) {
          std::shared_ptr<T> data = *((std::shared_ptr<T>*)ptr);
          serialize(this->derived(), *data);
        });
      }

      // -----------------------------------------------------------------
      // shared_ptr graph machinery (from previous 5f)
      // -----------------------------------------------------------------
      void mark_written_pointer(std::shared_ptr<void> value) {
        written_pointers_.push_back(std::move(value));
      }

      template <typename T>
      WriteLater make_shared_write_later(std::shared_ptr<T> value) {
        return [value, this]() { serialize(this->derived(), *value); };
      }

      void write_rest() {
        while(!write_later_.empty()) {
          auto pair = write_later_.front();
          std::size_t size_field = derived().current_pos();
          derived().write_u32(0);
          std::size_t frame_start = derived().current_pos();
          pair.second();
          std::uint32_t real_size =
            static_cast<std::uint32_t>(derived().current_pos() - frame_start);
          std::uint32_t encoded_size = real_size + 1;
          derived().poke_u32(size_field, encoded_size);
          written_pointers_.push_back(pair.first);
          write_later_.erase(write_later_.begin());
        }
      }

      template <typename T>
      void write_pointer(std::shared_ptr<T> value, WriteLater write_later) {
        if(!value) {
          derived().write_i32(0);
          return;
        }
        auto existing = std::find(written_pointers_.begin(), written_pointers_.end(), value);
        if(existing != written_pointers_.end()) {
          std::int32_t index = static_cast<std::int32_t>(std::distance(written_pointers_.begin(), existing));
          std::int32_t delta = index - static_cast<std::int32_t>(written_pointers_.size());
          derived().write_i32(delta);
          return;
        }
        auto will_be_written = std::find_if(write_later_.begin(), write_later_.end(),
          [value](const auto& pr) { return pr.first == value; });
        if(will_be_written != write_later_.end()) {
          std::int32_t index = static_cast<std::int32_t>(std::distance(write_later_.begin(), will_be_written));
          derived().write_i32(index + 1);
          return;
        }
        std::int32_t index = static_cast<std::int32_t>(write_later_.size());
        derived().write_i32(index + 1);
        write_later_.push_back({value, write_later});
      }

      template <typename T>
      void write_pointer(std::weak_ptr<T> value, WriteLater write_later) {
        write_pointer(value.lock(), write_later);
      }

    protected:
      Derived& derived() noexcept { return static_cast<Derived&>(*this); }

      std::vector<Frame> frames_;

      std::vector<std::function<void(void*)>> serialize_typed_functions_;
      std::unordered_map<std::type_index, size_t> typed_indexes_;

      std::vector<std::function<void(const std::exception&)>> serialize_exception_functions_;
      std::unordered_map<std::type_index, size_t> exception_indexes_;

      std::vector<std::function<void(void*)>> serialize_message_functions_;
      std::unordered_map<std::type_index, size_t> message_indexes_;

      std::vector<std::shared_ptr<void>> written_pointers_;
      std::vector<std::pair<std::shared_ptr<void>, WriteLater>> write_later_;

    private:
      template <typename T>
      void write_le_value(T value) {
        std::uint8_t buf[sizeof(T)];
        flat_store_le(buf, value);
        write_raw(buf, sizeof(T));
      }
  };

} // namespace serialization
