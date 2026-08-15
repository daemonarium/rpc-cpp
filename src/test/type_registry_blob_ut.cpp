#include <boost/ut.hpp>

#include <cstdint>
#include <string>
#include <stdexcept>

#include "serialization/serialization_format.hpp"
#include "serialization/type_registry.hpp"
#include "serialization/type_registry_blob.hpp"
#include "serialization/type_description.hpp"
#include "serialization/annotations.hpp"
#include "serialization/stream_writer.hpp"
#include "serialization/serialize.hpp"

using namespace boost::ut;
using namespace serialization;

int main() {
  "pack/unpack envelope for compressed flat key"_test = [] {
    expect(eq(pack_serialization_format_envelope(serialization_format::compressed), std::uint8_t{0x80}));
    expect(eq(pack_serialization_format_envelope(serialization_format::flat), std::uint8_t{0x81}));
    expect(eq(pack_serialization_format_envelope(serialization_format::key), std::uint8_t{0x82}));

    serialization_format fmt{};
    expect(try_unpack_serialization_format_envelope(0x80, fmt) == true);
    expect(fmt == serialization_format::compressed);
    expect(try_unpack_serialization_format_envelope(0x81, fmt) == true);
    expect(fmt == serialization_format::flat);
    expect(try_unpack_serialization_format_envelope(0x82, fmt) == true);
    expect(fmt == serialization_format::key);
    expect(try_unpack_serialization_format_envelope(0x05, fmt) == false);
  };

  "envelope rejects reserved bits and unknown nibble"_test = [] {
    serialization_format fmt{};
    expect(throws<std::runtime_error>([&] {
      try_unpack_serialization_format_envelope(0x90, fmt);
    }));
    expect(throws<std::runtime_error>([&] {
      try_unpack_serialization_format_envelope(0x83, fmt);
    }));
  };

  "blob roundtrip preserves data_format for all three values"_test = [] {
    for(auto format : {
          serialization_format::compressed,
          serialization_format::flat,
          serialization_format::key}) {
      type_registry reg(true);
      type_description::create(read_annotation(), reg);
      reg.set_data_format(format);
      const std::string blob = serialize_type_registry_blob(reg);
      expect(blob.size() > 1_ul);
      expect(eq(static_cast<std::uint8_t>(blob[0]), pack_serialization_format_envelope(format)));

      type_registry out(false);
      deserialize_type_registry_blob(blob, out);
      expect(out.data_format() == format);
      expect(out.get_types().size() > 0_ul);
    }
  };

  "legacy blob without envelope deserializes as compressed"_test = [] {
    type_registry reg(true);
    type_description::create(read_annotation(), reg);
    std::ostringstream output_stream;
    stream_writer writer(output_stream);
    writer.register_serialize_typed_function<type_description>();
    writer.register_serialize_typed_function<read_annotation>();
    serialize(writer, reg);
    writer.write_rest();
    const std::string legacy = output_stream.str();
    expect(eq(static_cast<std::uint8_t>(legacy[0]) & 0x80, std::uint8_t{0}));

    type_registry out(false);
    deserialize_type_registry_blob(legacy, out);
    expect(out.data_format() == serialization_format::compressed);
  };

  return 0;
}
