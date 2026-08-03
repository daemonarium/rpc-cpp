#pragma once

#include "writer.hpp"
#include "reader.hpp"
#include "stream_writer.hpp"
#include "stream_reader.hpp"

#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <memory>
#include <typeindex>
#include <functional>

namespace serialization {

  // Builds [data][meta][u8 meta_size]. Scalar encoding matches compressed stream format.
  // meta_size is at most 255; finish() throws if meta exceeds that.
  class key_encoder : public writer {
    public:
      void begin_frame() {}
      void end_frame() {}
      void save_offset() {}
      void write_rest() {}

      void write_u64(std::uint64_t value) { write_data([&](stream_writer& w) { w.write_u64(value); }); }
      void write_i64(std::int64_t value) { write_data([&](stream_writer& w) { w.write_i64(value); }); }
      void write_u32(std::uint32_t value) { write_data([&](stream_writer& w) { w.write_u32(value); }); }
      void write_i32(std::int32_t value) { write_data([&](stream_writer& w) { w.write_i32(value); }); }
      void write_u16(std::uint16_t value) { write_data([&](stream_writer& w) { w.write_u16(value); }); }
      void write_i16(std::int16_t value) { write_data([&](stream_writer& w) { w.write_i16(value); }); }
      void write_u8(std::uint8_t value) { write_data([&](stream_writer& w) { w.write_u8(value); }); }
      void write_i8(std::int8_t value) { write_data([&](stream_writer& w) { w.write_i8(value); }); }
      void write_f64(double value) { write_data([&](stream_writer& w) { w.write_f64(value); }); }
      void write_f32(float value) { write_data([&](stream_writer& w) { w.write_f32(value); }); }
      void write_bool(bool value) { write_data([&](stream_writer& w) { w.write_bool(value); }); }

      void write_length(std::size_t length) {
        write_meta([&](stream_writer& w) { w.write_length(length); });
      }

      void write_string(const std::string_view value) {
        write_length(value.size());
        data_.insert(data_.end(),
          reinterpret_cast<const std::uint8_t*>(value.data()),
          reinterpret_cast<const std::uint8_t*>(value.data()) + value.size());
      }

      void write_binary(const std::vector<std::uint8_t>& value) {
        write_length(value.size());
        data_.insert(data_.end(), value.begin(), value.end());
      }

      void write_binary(std::vector<std::uint8_t>&& value) {
        write_binary(static_cast<const std::vector<std::uint8_t>&>(value));
      }

      template <class Map, class KeyCb, class ValCb>
      void write_map(const Map&, KeyCb&&, ValCb&&) {
        throw std::runtime_error("key format: map not supported");
      }

      template <typename T>
      void write_pointer(std::shared_ptr<T>, WriteLater) {
        throw std::runtime_error("key format: shared_ptr not supported");
      }

      template <typename T>
      void write_pointer(std::weak_ptr<T>, WriteLater) {
        throw std::runtime_error("key format: weak_ptr not supported");
      }

      template <typename T>
      void write_typed(std::shared_ptr<T>) {
        throw std::runtime_error("key format: write_typed not supported");
      }

      template <typename T>
      void write_message(std::shared_ptr<T>) {
        throw std::runtime_error("key format: write_message not supported");
      }

      void write_exception(const std::exception&) {
        throw std::runtime_error("key format: write_exception not supported");
      }

      void register_serialize_typed_function(std::type_index, std::function<void(void*)>) {
        throw std::runtime_error("key format: typed registry not supported");
      }

      template <typename T>
      void register_serialize_typed_function() {
        throw std::runtime_error("key format: typed registry not supported");
      }

      void register_serialize_message_function(std::type_index, std::function<void(void*)>) {
        throw std::runtime_error("key format: message registry not supported");
      }

      template <typename T>
      void register_serialize_message_function() {
        throw std::runtime_error("key format: message registry not supported");
      }

      template <typename T>
      void register_serialize_exception_function() {
        throw std::runtime_error("key format: exception registry not supported");
      }

      std::vector<std::uint8_t> finish() {
        if(meta_.size() > 255) {
          throw std::runtime_error("key format: meta too large");
        }
        std::vector<std::uint8_t> out;
        out.reserve(data_.size() + meta_.size() + 1);
        out.insert(out.end(), data_.begin(), data_.end());
        out.insert(out.end(), meta_.begin(), meta_.end());
        out.push_back(static_cast<std::uint8_t>(meta_.size()));
        return out;
      }

      const std::vector<std::uint8_t>& data() const { return data_; }
      const std::vector<std::uint8_t>& meta() const { return meta_; }

    private:
      template <class F>
      void write_data(F&& f) {
        std::ostringstream os;
        stream_writer w(os);
        f(w);
        auto s = os.str();
        data_.insert(data_.end(),
          reinterpret_cast<const std::uint8_t*>(s.data()),
          reinterpret_cast<const std::uint8_t*>(s.data()) + s.size());
      }

      template <class F>
      void write_meta(F&& f) {
        std::ostringstream os;
        stream_writer w(os);
        f(w);
        auto s = os.str();
        meta_.insert(meta_.end(),
          reinterpret_cast<const std::uint8_t*>(s.data()),
          reinterpret_cast<const std::uint8_t*>(s.data()) + s.size());
      }

      std::vector<std::uint8_t> data_;
      std::vector<std::uint8_t> meta_;
  };

  class key_decoder : public reader {
    public:
      explicit key_decoder(std::span<const std::uint8_t> blob) {
        if(blob.size() < 1) {
          throw std::runtime_error("key format: blob too small");
        }
        const std::uint8_t meta_size = blob.back();
        if(static_cast<std::size_t>(meta_size) + 1 > blob.size()) {
          throw std::runtime_error("key format: meta_size out of range");
        }
        const std::size_t data_size = blob.size() - 1 - meta_size;
        data_bytes_.assign(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(data_size));
        meta_bytes_.assign(blob.begin() + static_cast<std::ptrdiff_t>(data_size),
          blob.end() - 1);
        data_stream_ = std::istringstream(
          std::string(reinterpret_cast<const char*>(data_bytes_.data()), data_bytes_.size()));
        meta_stream_ = std::istringstream(
          std::string(reinterpret_cast<const char*>(meta_bytes_.data()), meta_bytes_.size()));
        data_reader_ = std::make_unique<stream_reader>(data_stream_);
        meta_reader_ = std::make_unique<stream_reader>(meta_stream_);
      }

      // Copies blob into internal data/meta streams.
      explicit key_decoder(std::vector<std::uint8_t> blob)
        : key_decoder(std::span<const std::uint8_t>(blob.data(), blob.size())) {}

      consteval bool is_indexed() { return false; }
      void enter_frame() {}
      void exit_frame() {}
      void save_offset() {}
      void enter_indexed_frame(std::size_t) {
        throw std::runtime_error("key format: not indexed");
      }
      void read_rest() {}

      void read_u64(std::uint64_t& value) { data_reader_->read_u64(value); }
      void read_i64(std::int64_t& value) { data_reader_->read_i64(value); }
      void read_u32(std::uint32_t& value) { data_reader_->read_u32(value); }
      void read_i32(std::int32_t& value) { data_reader_->read_i32(value); }
      void read_u16(std::uint16_t& value) { data_reader_->read_u16(value); }
      void read_i16(std::int16_t& value) { data_reader_->read_i16(value); }
      void read_u8(std::uint8_t& value) { data_reader_->read_u8(value); }
      void read_i8(std::int8_t& value) { data_reader_->read_i8(value); }
      void read_f64(double& value) { data_reader_->read_f64(value); }
      void read_f32(float& value) { data_reader_->read_f32(value); }
      void read_bool(bool& value) { data_reader_->read_bool(value); }

      void read_length(std::size_t& value) { meta_reader_->read_length(value); }

      void read_string(std::string& value) {
        std::size_t length = 0;
        read_length(length);
        value.resize(length);
        if(length > 0) {
          data_stream_.read(value.data(), static_cast<std::streamsize>(length));
          if(static_cast<std::size_t>(data_stream_.gcount()) != length) {
            throw std::runtime_error("key format: short string read");
          }
        }
      }

      void read_binary(std::vector<std::uint8_t>& value) {
        std::size_t length = 0;
        read_length(length);
        value.resize(length);
        if(length > 0) {
          data_stream_.read(reinterpret_cast<char*>(value.data()),
            static_cast<std::streamsize>(length));
          if(static_cast<std::size_t>(data_stream_.gcount()) != length) {
            throw std::runtime_error("key format: short binary read");
          }
        }
      }

      template <class Map, class KeyCb, class ValCb>
      void read_map(Map&, KeyCb&&, ValCb&&) {
        throw std::runtime_error("key format: map not supported");
      }

      template <typename T>
      void read_pointer(std::shared_ptr<T>&, ReadLater) {
        throw std::runtime_error("key format: shared_ptr not supported");
      }

      template <typename T>
      void read_typed(std::shared_ptr<T>&) {
        throw std::runtime_error("key format: read_typed not supported");
      }

      void read_typed(std::weak_ptr<void>&) {
        throw std::runtime_error("key format: read_typed not supported");
      }

      template <typename T>
      void register_deserialize_typed_function() {
        throw std::runtime_error("key format: typed registry not supported");
      }

    private:
      std::vector<std::uint8_t> data_bytes_;
      std::vector<std::uint8_t> meta_bytes_;
      std::istringstream data_stream_;
      std::istringstream meta_stream_;
      std::unique_ptr<stream_reader> data_reader_;
      std::unique_ptr<stream_reader> meta_reader_;
  };

} // namespace serialization
