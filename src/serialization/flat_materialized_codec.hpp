#pragma once

#include "flat_buffer_reader.hpp"
#include "flat_circular_buffer_writer.hpp"
#include "flat_source.hpp"
#include "flat_message_codec_base.hpp"
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
  // Flat wire format (body, after length prefix) — shared with
  // flat_zero_copy_codec so the two are interoperable:
  //   [type index 4B u32 LE]
  //   [encoded_size_0 = N0+1 4B u32 LE][root T frame]
  //   [encoded_size_1 = N1+1][sub-frame 1] ...
  //   [0 sentinel 4B]
  //   [payload P B]
  // encoded_size_i = real_size_i + 1 (0 reserved as sentinel). payload_size
  // is derived by dispatch as (msg_len - cursor) after the sentinel.
  //
  // read_type_index / write_message_header / poke_u32 / payload_start_ come
  // from flat_message_codec_base. This class provides the materialized
  // dispatch handler (deserialize T, consume sentinel, invoke_handler<T>).
  class flat_materialized_codec
    : public flat_message_codec_base<flat_materialized_codec> {
    public:
      flat_materialized_codec(struct circular_buffer* input_buffer, struct circular_buffer* output_buffer)
        : reader_(std::make_unique<flat_buffer_reader<circular_flat_source>>(circular_flat_source(input_buffer))),
          writer_(std::make_unique<flat_circular_buffer_writer>(output_buffer, 0)) {}

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

          size_t payload_size = this->current_msg_len_ - reader_->cursor();
          this->template invoke_handler<T>(&msg, payload_size);
        };
      }

      template <typename T>
      void on_message(std::function<void(T&)> handler) {
        this->message_handlers_[std::type_index(typeid(T))] = [handler](void* ptr, size_t) {
          T& msg = *reinterpret_cast<T*>(ptr);
          handler(msg);
        };
      }

      template <typename T>
      void on_message(std::function<void(T&, size_t payload_size)> handler) {
        this->message_handlers_[std::type_index(typeid(T))] = [handler](void* ptr, size_t payload_size) {
          T& msg = *reinterpret_cast<T*>(ptr);
          handler(msg, payload_size);
        };
      }

      void reset_for_dispatch() {
        reader_->reset();
      }

      flat_buffer_reader<circular_flat_source>& reader() { return *reader_; }
      flat_circular_buffer_writer& writer() { return *writer_; }

    private:
      std::unique_ptr<flat_buffer_reader<circular_flat_source>> reader_;
      std::unique_ptr<flat_circular_buffer_writer> writer_;
  };

} // namespace serialization
