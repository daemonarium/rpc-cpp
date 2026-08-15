#pragma once

#include "key_codec.hpp"

#include <stdexcept>
#include <vector>
#include <cstdint>

extern "C" {
#include <io/circular_buffer.h>
}

namespace serialization {

  class key_circular_buffer_reader : public key_decoder {
    public:
      // message_len: logical size of one key blob at read_offset (peek space).
      key_circular_buffer_reader(struct circular_buffer* buffer, std::size_t message_len)
        : key_decoder(peek_message(buffer, message_len)) {}

    private:
      static std::vector<std::uint8_t> peek_message(struct circular_buffer* buffer,
          std::size_t message_len) {
        if(!buffer) {
          throw std::runtime_error("key_circular_buffer_reader: null buffer");
        }
        if(message_len > circular_buffer_readable_bytes(buffer)) {
          throw std::runtime_error("key_circular_buffer_reader: message_len exceeds readable");
        }
        std::vector<std::uint8_t> blob(message_len);
        if(message_len > 0) {
          if(circular_buffer_peek(buffer, 0, blob.data(), message_len) != message_len) {
            throw std::runtime_error("key_circular_buffer_reader: short peek");
          }
        }
        return blob;
      }
  };

} // namespace serialization
