#pragma once

#include "flat_buffer_reader.hpp"
#include "flat_circular_buffer_writer.hpp"
#include "flat_source.hpp"
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

  // Flat-format codec with materialization: deserializes messages into owned
  // C++ types (T). Compatibile with socket_message_framing<Codec>.
  //
  // Mirror of compressed_codec with flat reader/writer and u32 LE type index.
  // Framing (length-prefix, socket I/O) is handled by socket_message_framing<Codec>.
  class flat_materialized_codec {
    public:
      flat_materialized_codec(struct circular_buffer* input_buffer, struct circular_buffer* output_buffer)
        : reader_(std::make_unique<flat_buffer_reader<circular_flat_source>>(circular_flat_source(input_buffer))),
          writer_(std::make_unique<flat_circular_buffer_writer>(output_buffer, 0)) {}

      template <typename T>
      void register_message_type() {
        writer_->register_serialize_message_function<T>();

        size_t index = dispatch_table_.size();
        message_type_indexes_[std::type_index(typeid(T))] = index;

        dispatch_table_.push_back([this]() {
          T msg;
          deserialize(*reader_, msg);
          reader_->read_rest();

          size_t payload_size = current_msg_len_ - reader_->cursor();

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
      //
      // Flat wire format (body, after length prefix):
      //   [type index 4B u32 LE][root_frame_size 4B u32 LE][payload_size 4B u32 LE]
      //   [root T frame][shared sub-frames][payload]
      // root_frame_size is stamped here (known after serialize). payload_size
      // is stamped by finalize_send() (called from framing.commit_send) once
      // the caller has written any payload bytes.
      template <typename T>
      void write_message_header(const T& message) {
        auto type = std::type_index(typeid(T));
        auto it = message_type_indexes_.find(type);
        if(it == message_type_indexes_.end()) {
          throw std::runtime_error("Message type not registered for sending: " + std::string(type.name()));
        }
        size_t index = it->second;
        writer_->write_u32(static_cast<std::uint32_t>(index));

        // root_frame_size placeholder (filled after serialize)
        root_frame_size_field_offset_ = writer_->write_offset_;
        writer_->write_u32(0);
        // payload_size placeholder (filled by finalize_send)
        payload_size_field_offset_ = writer_->write_offset_;
        writer_->write_u32(0);

        std::size_t frame_start = writer_->write_offset_;
        serialize(*writer_, message);
        std::size_t root_frame_size = writer_->write_offset_ - frame_start;
        poke_u32(root_frame_size_field_offset_, static_cast<std::uint32_t>(root_frame_size));

        writer_->write_rest();
        payload_start_ = writer_->write_offset_;
      }

      // Stamp payload_size into the header once payload (if any) is written.
      // Called by framing.commit_send before the length prefix is committed.
      void finalize_send() {
        std::uint32_t payload_size =
          static_cast<std::uint32_t>(writer_->write_offset_ - payload_start_);
        poke_u32(payload_size_field_offset_, payload_size);
      }

      // Reset reader for a new message frame. Called by framing before dispatch.
      void reset_for_dispatch() {
        reader_->reset();
      }

      // Read type index and dispatch to the registered handler.
      // Throws std::runtime_error on unknown index. Framing is responsible for
      // skipping msg_len bytes after dispatch (including on throw).
      void dispatch(size_t msg_len) {
        current_msg_len_ = msg_len;

        std::uint32_t index;
        reader_->read_u32(index);
        // Skip root_frame_size and payload_size — materialized derives
        // payload_size from (msg_len - cursor) after deserialize+read_rest,
        // so the header size fields are not needed on this path.
        std::uint32_t ignored;
        reader_->read_u32(ignored);
        reader_->read_u32(ignored);

        if(index >= dispatch_table_.size()) {
          throw std::runtime_error("Unknown message type index: " + std::to_string(index));
        }

        dispatch_table_[static_cast<std::size_t>(index)]();
      }

      flat_buffer_reader<circular_flat_source>& reader() { return *reader_; }
      flat_circular_buffer_writer& writer() { return *writer_; }

    private:
      std::unique_ptr<flat_buffer_reader<circular_flat_source>> reader_;
      std::unique_ptr<flat_circular_buffer_writer> writer_;

      std::vector<std::function<void()>> dispatch_table_;
      std::unordered_map<std::type_index, size_t> message_type_indexes_;
      std::unordered_map<std::type_index, std::function<void(void*, size_t)>> message_handlers_;

      size_t current_msg_len_ = 0;

      // Offsets of the flat header size fields within the output buffer, and
      // the write offset at which payload (if any) begins. Set by
      // write_message_header, consumed by finalize_send.
      std::size_t root_frame_size_field_offset_ = 0;
      std::size_t payload_size_field_offset_ = 0;
      std::size_t payload_start_ = 0;

      void poke_u32(std::size_t abs_offset, std::uint32_t value) {
        circular_buffer_poke(writer_->buffer_, abs_offset, &value, sizeof(value));
      }
  };

} // namespace serialization
