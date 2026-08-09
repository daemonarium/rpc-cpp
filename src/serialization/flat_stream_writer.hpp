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
#include <ostream>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace serialization {

  class flat_stream_writer : public writer {
    public:
      explicit flat_stream_writer(std::ostream& stream) : stream_(stream) {}

      void begin_frame() {
        frames_.push_back(Frame{ pos_, {} });
      }

      void end_frame() {
        if(frames_.empty()) {
          throw std::runtime_error("flat_stream_writer::end_frame without begin_frame");
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
        frame.cps.push_back(static_cast<std::uint32_t>(pos_ - frame.start));
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

      void write_rest() {
        while(!write_later_.empty()) {
          auto pair = write_later_.front();
          // Per-frame size prefix: encoded_size = real_size + 1 (0 = sentinel).
          std::size_t size_field = pos_;
          write_u32(0);
          std::size_t frame_start = pos_;
          pair.second();
          std::uint32_t real_size = static_cast<std::uint32_t>(pos_ - frame_start);
          std::uint32_t encoded_size = real_size + 1;
          std::uint8_t buf[4];
          flat_store_le(buf, encoded_size);
          std::streampos saved = stream_.tellp();
          stream_.seekp(static_cast<std::streamoff>(size_field));
          stream_.write(reinterpret_cast<const char*>(buf), 4);
          stream_.seekp(saved);
          written_pointers_.push_back(pair.first);
          write_later_.erase(write_later_.begin());
        }
      }

      // Flat graphs are homogeneous: sub-frames carry no per-frame type index.
      template <typename T>
      WriteLater make_shared_write_later(std::shared_ptr<T> value) {
        return [value, this]() { serialize(*this, *value); };
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

      template <typename T>
      void write_pointer(std::shared_ptr<T> value, WriteLater write_later) {
        if(!value) {
          write_i32(0);
          return;
        }
        auto existing = std::find(written_pointers_.begin(), written_pointers_.end(), value);
        if(existing != written_pointers_.end()) {
          std::int32_t index = static_cast<std::int32_t>(std::distance(written_pointers_.begin(), existing));
          std::int32_t delta = index - static_cast<std::int32_t>(written_pointers_.size());
          write_i32(delta);
          return;
        }
        auto will_be_written = std::find_if(write_later_.begin(), write_later_.end(),
          [value](const auto& pair) { return pair.first == value; });
        if(will_be_written != write_later_.end()) {
          std::int32_t index = static_cast<std::int32_t>(std::distance(write_later_.begin(), will_be_written));
          write_i32(index + 1);
          return;
        }
        std::int32_t index = static_cast<std::int32_t>(write_later_.size());
        write_i32(index + 1);
        write_later_.push_back({value, write_later});
      }

      template <typename T>
      void write_pointer(std::weak_ptr<T> value, WriteLater write_later) {
        write_pointer(value.lock(), write_later);
      }

      std::size_t position() const { return pos_; }

      // Pre-register a shared_ptr as already-written so that self-references
      // (and any later references) to it use negative back-reference deltas
      // instead of creating duplicate sub-frames. Used by the shared-graph
      // root path: the root is deref-serialized (no write_pointer) but must
      // still be discoverable by write_pointer for back-references.
      void mark_written_pointer(std::shared_ptr<void> value) {
        written_pointers_.push_back(std::move(value));
      }

      // Poke a u32 LE value at a previously reserved stream position. Used to
      // back-patch per-frame size prefixes after the frame body is complete.
      void poke_u32(std::size_t abs_offset, std::uint32_t value) {
        std::uint8_t buf[4];
        flat_store_le(buf, value);
        std::streampos saved = stream_.tellp();
        stream_.seekp(static_cast<std::streamoff>(abs_offset));
        stream_.write(reinterpret_cast<const char*>(buf), 4);
        stream_.seekp(saved);
      }

    private:
      struct Frame {
        std::size_t start;
        std::vector<std::uint32_t> cps;
      };

      std::ostream& stream_;
      std::size_t pos_ = 0;
      std::vector<Frame> frames_;

      std::vector<std::shared_ptr<void>> written_pointers_;
      std::vector<std::pair<std::shared_ptr<void>, WriteLater>> write_later_;

      std::vector<std::function<void(void*)>> serialize_typed_functions_;
      std::unordered_map<std::type_index, size_t> typed_indexes_;

      std::vector<std::function<void(const std::exception&)>> serialize_exception_functions_;
      std::unordered_map<std::type_index, size_t> exception_indexes_;

      std::vector<std::function<void(void*)>> serialize_message_functions_;
      std::unordered_map<std::type_index, size_t> message_indexes_;

      void write_raw(const void* data, std::size_t size) {
        stream_.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        pos_ += size;
      }

      template <typename T>
      void write_le_value(T value) {
        std::uint8_t buf[sizeof(T)];
        flat_store_le(buf, value);
        write_raw(buf, sizeof(T));
      }
  };

} // namespace serialization
