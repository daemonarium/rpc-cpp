#pragma once

#include "key_codec.hpp"

#include <ostream>
#include <stdexcept>

namespace serialization {

  class key_stream_writer : public key_encoder {
    public:
      explicit key_stream_writer(std::ostream& stream) : stream_(stream) {}

      // Serialize first, then call finish() to emit [data][meta][u8].
      void finish() {
        auto blob = key_encoder::finish();
        if(!blob.empty()) {
          stream_.write(reinterpret_cast<const char*>(blob.data()),
            static_cast<std::streamsize>(blob.size()));
        }
        finished_ = true;
      }

      bool finished() const { return finished_; }

    private:
      std::ostream& stream_;
      bool finished_ = false;
  };

} // namespace serialization
