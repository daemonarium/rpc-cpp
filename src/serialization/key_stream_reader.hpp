#pragma once

#include "key_codec.hpp"

#include <istream>
#include <vector>
#include <cstdint>

namespace serialization {

  class key_stream_reader : public key_decoder {
    public:
      explicit key_stream_reader(std::istream& stream)
        : key_decoder(load_all(stream)) {}

    private:
      static std::vector<std::uint8_t> load_all(std::istream& stream) {
        std::vector<std::uint8_t> buf;
        char tmp[4096];
        while(stream) {
          stream.read(tmp, sizeof(tmp));
          auto n = stream.gcount();
          if(n > 0) {
            buf.insert(buf.end(),
              reinterpret_cast<const std::uint8_t*>(tmp),
              reinterpret_cast<const std::uint8_t*>(tmp) + n);
          }
        }
        stream.clear();
        return buf;
      }
  };

} // namespace serialization
