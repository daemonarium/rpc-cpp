#pragma once

#include "circular_buffer_reader.hpp"
#include "circular_buffer_writer.hpp"
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

  // Compressed-format codec: owns circular_buffer_reader/writer, format-specific
  // type index (uvarint64) and dispatch handler (materialize T). Framing
  // (length-prefix, socket I/O) is handled by socket_message_framing<Codec>.
  class compressed_codec
    : public message_codec_base<compressed_codec, circular_buffer_reader, circular_buffer_writer> {
    public:
      compressed_codec(struct circular_buffer* input_buffer, struct circular_buffer* output_buffer)
        : reader_(std::make_unique<circular_buffer_reader>(input_buffer, 0)),
          writer_(std::make_unique<circular_buffer_writer>(output_buffer, 0)) {}

      std::uint64_t read_type_index() {
        std::uint64_t index;
        reader_->read_u64(index);
        return index;
      }

      template <typename T>
      std::function<void()> make_dispatch_handler() {
        return [this]() {
          T msg;
          deserialize(*reader_, msg);
          reader_->read_rest();

          size_t payload_size = current_msg_len_ - reader_->read_offset_;
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

      void reset_for_dispatch() {
        reader_->read_offset_ = 0;
      }

      circular_buffer_reader& reader() { return *reader_; }
      circular_buffer_writer& writer() { return *writer_; }

    private:
      std::unique_ptr<circular_buffer_reader> reader_;
      std::unique_ptr<circular_buffer_writer> writer_;
  };

} // namespace serialization
