#pragma once

#include "key_codec.hpp"

#include <stdexcept>
#include <cstdint>

extern "C" {
#include <io/circular_buffer.h>
}

namespace serialization {

  class key_circular_buffer_writer : public key_encoder {
    public:
      explicit key_circular_buffer_writer(struct circular_buffer* buffer) : buffer_(buffer) {
        if(!buffer_) {
          throw std::runtime_error("key_circular_buffer_writer: null buffer");
        }
      }

      void finish() {
        auto blob = key_encoder::finish();
        if(blob.empty()) return;
        size_t written = circular_buffer_write(buffer_, blob.data(), blob.size());
        if(written != blob.size()) {
          throw std::runtime_error("key_circular_buffer_writer: short write");
        }
        finished_ = true;
      }

      bool finished() const { return finished_; }

    private:
      struct circular_buffer* buffer_;
      bool finished_ = false;
  };

} // namespace serialization
