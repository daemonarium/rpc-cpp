#pragma once

#include "writer.hpp"
#include "flat_encoding.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace serialization {

  class flat_bytes_writer;

  template <class Map, class KeyCb, class ValCb>
  void flat_append_map_soa(std::vector<std::uint8_t>& out, const Map& map, KeyCb&& key_cb, ValCb&& val_cb);

  // Flat LE writer that appends into a std::vector (map key/value staging, nested frames).
  class flat_bytes_writer : public writer {
    public:
      explicit flat_bytes_writer(std::vector<std::uint8_t>& out) : out_(out) {}

      void begin_frame() {
        frames_.push_back(Frame{ out_.size(), {} });
      }

      void end_frame() {
        if(frames_.empty()) {
          throw std::runtime_error("flat_bytes_writer::end_frame without begin_frame");
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
        frame.cps.push_back(static_cast<std::uint32_t>(out_.size() - frame.start));
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
        std::uint8_t b = value ? 1 : 0;
        write_raw(&b, 1);
      }

      void write_string(std::string_view value) {
        write_u32(static_cast<std::uint32_t>(value.size()));
        write_raw(value.data(), value.size());
      }

      void write_binary(std::vector<std::uint8_t>&& value) {
        write_u32(static_cast<std::uint32_t>(value.size()));
        write_raw(value.data(), value.size());
      }

      void write_length(std::size_t length) {
        write_u32(static_cast<std::uint32_t>(length));
      }

      template <typename T>
      void write_typed(std::shared_ptr<T> data);

      void write_rest() {
        while(!write_later_.empty()) {
          auto pair = write_later_.front();
          pair.second();
          written_pointers_.push_back(pair.first);
          write_later_.erase(write_later_.begin());
        }
      }

      void register_serialize_typed_function(std::type_index type,
          std::function<void(void*)> serialize_fn) {
        typed_indexes_[type] = serialize_typed_functions_.size();
        serialize_typed_functions_.push_back(std::move(serialize_fn));
      }

      template <typename T>
      void register_serialize_typed_function();

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

      template <class Map, class KeyCb, class ValCb>
      void write_map(const Map& map, KeyCb&& key_cb, ValCb&& val_cb) {
        flat_append_map_soa(out_, map, std::forward<KeyCb>(key_cb), std::forward<ValCb>(val_cb));
      }

      std::size_t position() const { return out_.size(); }

      void write_raw(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        out_.insert(out_.end(), p, p + size);
      }

    private:
      struct Frame {
        std::size_t start;
        std::vector<std::uint32_t> cps;
      };

      std::vector<std::uint8_t>& out_;
      std::vector<Frame> frames_;
      std::vector<std::shared_ptr<void>> written_pointers_;
      std::vector<std::pair<std::shared_ptr<void>, WriteLater>> write_later_;
      std::vector<std::function<void(void*)>> serialize_typed_functions_;
      std::unordered_map<std::type_index, size_t> typed_indexes_;

      template <typename T>
      void write_le_value(T value) {
        std::uint8_t buf[sizeof(T)];
        flat_store_le(buf, value);
        write_raw(buf, sizeof(T));
      }
  };

  template <class Map, class KeyCb, class ValCb>
  void flat_append_map_soa(std::vector<std::uint8_t>& out, const Map& map, KeyCb&& key_cb, ValCb&& val_cb) {
    const std::size_t n = map.size();
    {
      std::uint8_t buf[4];
      flat_store_le(buf, static_cast<std::uint32_t>(n));
      out.insert(out.end(), buf, buf + 4);
    }
    if(n == 0) {
      return;
    }

    std::vector<std::uint8_t> keys_buf;
    flat_bytes_writer key_w(keys_buf);
    struct Entry {
      std::uint32_t off = 0;
      std::uint32_t len = 0;
      const typename Map::mapped_type* value = nullptr;
    };
    std::vector<Entry> entries;
    entries.reserve(n);
    for(const auto& element : map) {
      const std::uint32_t off = static_cast<std::uint32_t>(keys_buf.size());
      key_cb(key_w, element.first);
      const std::uint32_t len = static_cast<std::uint32_t>(keys_buf.size() - off);
      entries.push_back(Entry{off, len, &element.second});
    }
    key_w.write_rest();

    std::sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
      const std::size_t m = std::min(a.len, b.len);
      const int cmp = std::memcmp(keys_buf.data() + a.off, keys_buf.data() + b.off, m);
      if(cmp != 0) return cmp < 0;
      return a.len < b.len;
    });

    std::vector<std::uint8_t> sorted_keys;
    std::vector<std::uint32_t> key_offs(n + 1);
    sorted_keys.reserve(keys_buf.size());
    for(std::size_t i = 0; i < n; ++i) {
      key_offs[i] = static_cast<std::uint32_t>(sorted_keys.size());
      sorted_keys.insert(sorted_keys.end(),
        keys_buf.begin() + static_cast<std::ptrdiff_t>(entries[i].off),
        keys_buf.begin() + static_cast<std::ptrdiff_t>(entries[i].off + entries[i].len));
    }
    key_offs[n] = static_cast<std::uint32_t>(sorted_keys.size());

    flat_append_offset_table(out, key_offs.data(), n + 1);
    out.insert(out.end(), sorted_keys.begin(), sorted_keys.end());

    std::vector<std::uint8_t> values_buf;
    flat_bytes_writer val_w(values_buf);
    std::vector<std::uint32_t> val_offs(n);
    for(std::size_t i = 0; i < n; ++i) {
      val_offs[i] = static_cast<std::uint32_t>(values_buf.size());
      val_cb(val_w, *entries[i].value);
    }
    val_w.write_rest();

    out.insert(out.end(), values_buf.begin(), values_buf.end());
    flat_append_offset_trailer(out, val_offs.data(), n);
  }

} // namespace serialization

#include "serialize.hpp"

namespace serialization {

  template <typename T>
  void flat_bytes_writer::write_typed(std::shared_ptr<T> data) {
    auto type_index = std::type_index(typeid(*data));
    auto it = typed_indexes_.find(type_index);
    if(it == typed_indexes_.end()) {
      throw std::runtime_error("flat_bytes_writer: type not registered");
    }
    write_u32(static_cast<std::uint32_t>(it->second));
    serialize_typed_functions_[it->second](&data);
  }

  template <typename T>
  void flat_bytes_writer::register_serialize_typed_function() {
    auto type = std::type_index(typeid(T));
    register_serialize_typed_function(type, [&writer = *this](void* ptr) {
      std::shared_ptr<T> data = *((std::shared_ptr<T>*)ptr);
      serialize(writer, *data);
    });
  }

} // namespace serialization
