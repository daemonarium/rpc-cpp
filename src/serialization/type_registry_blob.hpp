#pragma once

/**
 * Wire framing for type_registry blobs (rpc_description / registry.*.bin):
 *
 *   [envelope u8][compressed type_registry body…]
 *
 * Envelope (v1): bit7=1, bits4–6=0 (reserved), bits0–3=serialization_format
 * (compressed=0, flat=1, key=2). Legacy blobs without envelope → data format
 * compressed. Registry body is always compressed stream encoding.
 */

#include "serialization_format.hpp"
#include "type_registry.hpp"
#include "type_description.hpp"
#include "annotations.hpp"
#include "stream_writer.hpp"
#include "stream_reader.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"

#include <cstring>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace serialization {

  inline std::string serialize_type_registry_blob(type_registry& registry) {
    std::ostringstream output_stream;
    stream_writer writer(output_stream);
    writer.write_u8(pack_serialization_format_envelope(registry.data_format()));
    writer.register_serialize_typed_function<type_description>();
    writer.register_serialize_typed_function<read_annotation>();
    serialize(writer, registry);
    writer.write_rest();
    return output_stream.str();
  }

  inline size_t serialize_type_registry_blob_malloc(type_registry& registry, char** buffer) {
    const std::string bytes = serialize_type_registry_blob(registry);
    *buffer = static_cast<char*>(std::malloc(bytes.size()));
    if(!*buffer) {
      return 0;
    }
    std::memcpy(*buffer, bytes.data(), bytes.size());
    return bytes.size();
  }

  inline void deserialize_type_registry_blob(std::string_view bytes, type_registry& registry) {
    if(bytes.empty()) {
      throw std::runtime_error("type_registry blob: empty");
    }
    serialization_format format = serialization_format::compressed;
    std::size_t offset = 0;
    const auto first = static_cast<std::uint8_t>(bytes[0]);
    if(try_unpack_serialization_format_envelope(first, format)) {
      offset = 1;
    }
    registry.set_data_format(format);
    std::istringstream input_stream(std::string(bytes.data() + offset, bytes.size() - offset), std::ios::binary);
    stream_reader reader(input_stream);
    reader.register_deserialize_typed_function<type_description>();
    deserialize(reader, registry);
    reader.read_rest();
  }

  inline void deserialize_type_registry_blob(const std::vector<std::uint8_t>& bytes, type_registry& registry) {
    deserialize_type_registry_blob(
      std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
      registry
    );
  }

} // namespace serialization
