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
          // Consume the 0 sentinel that terminates the frame list.
          std::uint32_t sentinel = 1;
          reader_->read_u32(sentinel);
          if(sentinel != 0) {
            throw std::runtime_error("flat_materialized_codec: expected frame sentinel, got " + std::to_string(sentinel));
          }

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

      // Write type index + per-frame size prefix + serialized root + shared
      // sub-frames (each with its own per-frame prefix, written by write_rest)
      // + 0 sentinel. Called by framing.begin_send<T>() after the length-prefix
      // placeholder is reserved and writer offset set.
      //
      // Flat wire format (body, after length prefix):
      //   [type index 4B u32 LE]
      //   [encoded_size_0 = N0+1 4B u32 LE][root T frame]
      //   [encoded_size_1 = N1+1][sub-frame 1] ...
      //   [0 sentinel 4B]
      //   [payload P B]
      // encoded_size_i = real_size_i + 1 (0 reserved as sentinel). payload_size
      // is derived by dispatch as (msg_len - cursor) after the sentinel.
      template <typename T>
      void write_message_header(const T& message) {
        auto type = std::type_index(typeid(T));
        auto it = message_type_indexes_.find(type);
        if(it == message_type_indexes_.end()) {
          throw std::runtime_error("Message type not registered for sending: " + std::string(type.name()));
        }
        size_t index = it->second;
        writer_->write_u32(static_cast<std::uint32_t>(index));

        // encoded_size_0 placeholder for the root frame (filled after serialize)
        std::size_t root_size_offset = writer_->write_offset_;
        writer_->write_u32(0);
        std::size_t root_frame_start = writer_->write_offset_;
        serialize(*writer_, message);
        std::uint32_t root_real_size =
          static_cast<std::uint32_t>(writer_->write_offset_ - root_frame_start);
        poke_u32(root_size_offset, root_real_size + 1);

        // Sub-frames: each wrapped with its own per-frame size prefix by
        // write_rest. Then a 0 sentinel terminates the frame list.
        writer_->write_rest();
        writer_->write_u32(0); // sentinel
        payload_start_ = writer_->write_offset_;
      }

      // No-op: the sentinel is written in write_message_header, and payload_size
      // is derived by dispatch (msg_len - cursor after sentinel). Kept for
      // socket_message_framing<Codec> compatibility.
      void finalize_send() {}

      // Reset reader for a new message frame. Called by framing before dispatch.
      void reset_for_dispatch() {
        reader_->reset();
      }

      // Read type index + encoded_size_0, dispatch to the registered handler.
      // The materialized path deserializes the root (enter_frame/exit_frame,
      // no prefix) then read_rest (consumes per-frame prefixes for sub-frames),
      // then reads the 0 sentinel. payload_size = msg_len - cursor.
      // Throws std::runtime_error on unknown index. Framing is responsible for
      // skipping msg_len bytes after dispatch (including on throw).
      void dispatch(size_t msg_len) {
        current_msg_len_ = msg_len;

        std::uint32_t index;
        reader_->read_u32(index);
        // Consume the root frame's per-frame size prefix (encoded_size_0).
        // The materialized reader reads fields sequentially via
        // enter_frame/exit_frame, so the size itself is not needed — only
        // consumed to keep the cursor aligned with the root frame body.
        std::uint32_t root_encoded_size;
        reader_->read_u32(root_encoded_size);
        (void)root_encoded_size;

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

      // Write offset at which payload (if any) begins. Set by
      // write_message_header (after the sentinel). Kept for introspection;
      // payload_size is derived by dispatch as (msg_len - cursor).
      std::size_t payload_start_ = 0;

      void poke_u32(std::size_t abs_offset, std::uint32_t value) {
        circular_buffer_poke(writer_->buffer_, abs_offset, &value, sizeof(value));
      }
  };

} // namespace serialization
