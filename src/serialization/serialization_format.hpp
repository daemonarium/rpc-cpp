#pragma once

#include <cstdint>
#include <stdexcept>

namespace serialization {

  // Encoding of *data* described by a type_registry (messages, rows, index keys).
  // The type_registry blob body itself is always compressed; see type_registry_blob.hpp.
  enum class serialization_format : std::uint8_t {
    compressed = 0,
    flat = 1,
    key = 2,
  };

  // Flat shared_ptr graph layout.
  // 1 = homogeneous sub-frames (no per-frame type index). No longer written.
  // 2 = each deferred sub-frame is [encoded_size u32][type index u32][body].
  // Socket framing has no separate version byte; peers must both be version 2.
  // Callers that do exchange a version reject anything older.
  inline constexpr std::uint32_t FLAT_WIRE_VERSION = 2;

  inline void require_flat_wire_version(std::uint32_t peer_version) {
    if(peer_version < FLAT_WIRE_VERSION) {
      throw std::runtime_error("unsupported_protocol_version");
    }
  }

  inline constexpr std::uint8_t SERIALIZATION_FORMAT_ENVELOPE = 0x80;
  inline constexpr std::uint8_t SERIALIZATION_FORMAT_RESERVED_MASK = 0x70;
  inline constexpr std::uint8_t SERIALIZATION_FORMAT_VALUE_MASK = 0x0f;

  inline std::uint8_t pack_serialization_format_envelope(serialization_format format) {
    auto v = static_cast<std::uint8_t>(format);
    if(v > static_cast<std::uint8_t>(serialization_format::key)) {
      throw std::runtime_error("serialization_format: unknown format value");
    }
    return static_cast<std::uint8_t>(SERIALIZATION_FORMAT_ENVELOPE | v);
  }

  // Returns true if `byte` is a v1 envelope (bit7 set). On true, writes unpacked format.
  // Throws if reserved bits are set or format nibble is unknown.
  inline bool try_unpack_serialization_format_envelope(std::uint8_t byte, serialization_format& out) {
    if((byte & SERIALIZATION_FORMAT_ENVELOPE) == 0) {
      return false;
    }
    if((byte & SERIALIZATION_FORMAT_RESERVED_MASK) != 0) {
      throw std::runtime_error("serialization_format: reserved envelope bits must be 0");
    }
    auto v = static_cast<std::uint8_t>(byte & SERIALIZATION_FORMAT_VALUE_MASK);
    if(v > static_cast<std::uint8_t>(serialization_format::key)) {
      throw std::runtime_error("serialization_format: unknown format nibble");
    }
    out = static_cast<serialization_format>(v);
    return true;
  }

} // namespace serialization
