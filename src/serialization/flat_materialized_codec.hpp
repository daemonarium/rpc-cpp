#pragma once

#include "flat_buffer_reader.hpp"
#include "flat_circular_buffer_writer.hpp"
#include "flat_source.hpp"
#include "message_codec_base.hpp"
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
  // C++ types (T). Compatible with socket_message_framing<Codec>.
  //
  // Flat wire format (body, after length prefix):
  //   [type index 4B u32 LE]
  //   [encoded_size_0 = N0+1 4B u32 LE][root T frame]
  //   [encoded_size_1 = N1+1][sub-frame 1] ...
  //   [0 sentinel 4B]
  //   [payload P B]
  // encoded_size_i = real_size_i + 1 (0 reserved as sentinel). payload_size
  // is derived by dispatch as (msg_len - cursor) after the sentinel.
  class flat_materialized_codec
    : public message_codec_base<flat_materialized_codec,
        flat_buffer_reader<circular_flat_source>, flat_circular_buffer_writer> {
    public:
      flat_materialized_codec(struct circular_buffer* input_buffer, struct circular_buffer* output_buffer)
        : reader_(std::make_unique<flat_buffer_reader<circular_flat_source>>(circular_flat_source(input_buffer))),
          writer_(std::make_unique<flat_circular_buffer_writer>(output_buffer, 0)) {}

      std::uint32_t read_type_index() {
        std::uint32_t index;
        reader_->read_u32(index);
        // Consume the root frame's per-frame size prefix (encoded_size_0).
        // The materialized reader reads fields sequentially via
        // enter_frame/exit_frame, so the size itself is not needed — only
        // consumed to keep the cursor aligned with the root frame body.
        std::uint32_t root_encoded_size;
        reader_->read_u32(root_encoded_size);
        (void)root_encoded_size;
        return index;
      }

      template <typename T>
      std::function<void()> make_dispatch_handler() {
        return [this]() {
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
          invoke_handler<T>(&msg, payload_size);
        };
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

      void reset_for_dispatch() {
        reader_->reset();
      }

      flat_buffer_reader<circular_flat_source>& reader() { return *reader_; }
      flat_circular_buffer_writer& writer() { return *writer_; }

    private:
      std::unique_ptr<flat_buffer_reader<circular_flat_source>> reader_;
      std::unique_ptr<flat_circular_buffer_writer> writer_;

      // Write offset at which payload (if any) begins. Set by
      // write_message_header (after the sentinel). Kept for introspection;
      // payload_size is derived by dispatch as (msg_len - cursor).
      std::size_t payload_start_ = 0;

      void poke_u32(std::size_t abs_offset, std::uint32_t value) {
        circular_buffer_poke(writer_->buffer_, abs_offset, &value, sizeof(value));
      }
  };

} // namespace serialization
