#pragma once

#include "circular_buffer_reader.hpp"
#include "circular_buffer_writer.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

namespace serialization {

  // Compressed-format codec: owns circular_buffer_reader/writer, dispatch table,
  // message handlers, type/exception/dynamic registrations.
  //
  // Framing (length-prefix, socket I/O) is handled by socket_message_framing<Codec>.
  // This codec is format-specific: type index is uvarint64, payload uses compressed
  // (varint) encoding via circular_buffer_reader/writer.
  class compressed_codec {
    public:
      compressed_codec(struct circular_buffer* input_buffer, struct circular_buffer* output_buffer)
        : reader_(std::make_unique<circular_buffer_reader>(input_buffer, 0)),
          writer_(std::make_unique<circular_buffer_writer>(output_buffer, 0)) {}

      template <typename T>
      void register_message_type() {
        writer_->register_serialize_message_function<T>();

        size_t index = dispatch_table_.size();
        message_type_indexes_[std::type_index(typeid(T))] = index;

        dispatch_table_.push_back([this]() {
          T msg;
          deserialize(*reader_, msg);
          reader_->read_rest();

          size_t payload_size = current_msg_len_ - reader_->read_offset_;

          auto it = message_handlers_.find(std::type_index(typeid(T)));
          if(it != message_handlers_.end()) {
            it->second(&msg, payload_size);
          }
          // Framing does circular_buffer_skip_read(msg_len) after dispatch returns,
          // which automatically skips any unread payload.
        });
      }

      template <typename T>
      void register_exception_type() {
        writer_->register_serialize_exception_function<T>();
        reader_->register_deserialize_exception_function<T>();
      }

      template <typename T>
      void register_dynamic_type() {
        writer_->register_serialize_typed_function<T>();
        reader_->register_deserialize_typed_function<T>();
      }

      template <typename T>
      void on_message(std::function<void(T&)> handler) {
        message_handlers_[std::type_index(typeid(T))] = [handler](void* ptr, size_t) {
          T& msg = *reinterpret_cast<T*>(ptr);
          handler(msg);
        };
      }

      template <typename T>
      void on_message(std::function<void(T&, size_t payload_size)> handler) {
        message_handlers_[std::type_index(typeid(T))] = [handler](void* ptr, size_t payload_size) {
          T& msg = *reinterpret_cast<T*>(ptr);
          handler(msg, payload_size);
        };
      }

      // Write type index + serialized header. Called by framing.begin_send<T>()
      // after the length-prefix placeholder is reserved and writer offset set.
      template <typename T>
      void write_message_header(const T& message) {
        auto type = std::type_index(typeid(T));
        auto it = message_type_indexes_.find(type);
        if(it == message_type_indexes_.end()) {
          throw std::runtime_error("Message type not registered for sending: " + std::string(type.name()));
        }
        size_t index = it->second;
        writer_->write_u64(index);
        serialize(*writer_, message);
        writer_->write_rest();
      }

      // Reset reader for a new message frame. Called by framing before dispatch.
      void reset_for_dispatch() {
        reader_->read_offset_ = 0;
      }

      // Called by framing.commit_send after the caller has written any payload.
      // No-op for compressed: payload_size is derived on read (msg_len - cursor),
      // no size field is stamped into the wire. Flat codecs override to poke
      // payload_size into the header.
      void finalize_send() {}

      // Read type index and dispatch to the registered handler.
      // Throws std::runtime_error on unknown index. Framing is responsible for
      // skipping msg_len bytes after dispatch (including on throw).
      void dispatch(size_t msg_len) {
        current_msg_len_ = msg_len;

        std::uint64_t index;
        reader_->read_u64(index);

        if(index >= dispatch_table_.size()) {
          throw std::runtime_error("Unknown message type index: " + std::to_string(index));
        }

        dispatch_table_[static_cast<std::size_t>(index)]();
      }

      circular_buffer_reader& reader() { return *reader_; }
      circular_buffer_writer& writer() { return *writer_; }

    private:
      std::unique_ptr<circular_buffer_reader> reader_;
      std::unique_ptr<circular_buffer_writer> writer_;

      std::vector<std::function<void()>> dispatch_table_;
      std::unordered_map<std::type_index, size_t> message_type_indexes_;
      std::unordered_map<std::type_index, std::function<void(void*, size_t)>> message_handlers_;

      size_t current_msg_len_ = 0;
  };

} // namespace serialization
