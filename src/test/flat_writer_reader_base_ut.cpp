#include <boost/ut.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "serialization/flat_stream_writer.hpp"
#include "serialization/flat_stream_reader.hpp"
#include "serialization/flat_circular_buffer_writer.hpp"
#include "serialization/flat_buffer_reader.hpp"
#include "serialization/flat_source.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"
#include "serialization/rpc.hpp"

extern "C" {
#include <io/circular_buffer.h>
}

using namespace boost::ut;
using namespace serialization;

namespace {

  struct Skill {
    std::string name;
    std::int32_t level = 0;
  };

  struct Person {
    std::string name;
    std::int32_t age = 0;
    Skill skill{};
    std::vector<std::int32_t> scores;
    std::vector<std::string> tags;
    std::map<std::string, std::int32_t> attrs;
  };

  struct [[=serialization::message()]] Ping {
    std::int32_t value = 0;
  };

  // Write the same payload through flat_stream_writer, return bytes.
  template <typename Fn>
  std::string write_stream(Fn&& fn) {
    std::ostringstream os;
    flat_stream_writer w(os);
    fn(w);
    w.write_rest();
    return os.str();
  }

  // Write the same payload through flat_circular_buffer_writer, return bytes.
  template <typename Fn>
  std::vector<std::uint8_t> write_circular(Fn&& fn) {
    struct circular_buffer* cb = circular_buffer_create(4096, nullptr);
    {
      flat_circular_buffer_writer w(cb);
      fn(w);
      w.write_rest();
      circular_buffer_commit_write(cb, w.write_offset_);
    }
    std::vector<std::uint8_t> out(circular_buffer_readable_bytes(cb));
    if(!out.empty()) {
      circular_buffer_peek(cb, 0, out.data(), out.size());
    }
    circular_buffer_destroy(cb);
    return out;
  }

  // Compare std::string (stream bytes) with vector<uint8_t> (circular bytes).
  bool bytes_equal(const std::string& stream_bytes, const std::vector<std::uint8_t>& circular_bytes) {
    if(stream_bytes.size() != circular_bytes.size()) {
      return false;
    }
    for(std::size_t i = 0; i < stream_bytes.size(); i++) {
      if(static_cast<unsigned char>(stream_bytes[i]) != circular_bytes[i]) {
        return false;
      }
    }
    return true;
  }

  Person sample_person() {
    Person p;
    p.name = "Ada";
    p.age = 36;
    p.skill.name = "Rust";
    p.skill.level = 9;
    p.scores = {10, 20, 30};
    p.tags = {"dev", "systems"};
    p.attrs = {{"x", 1}, {"y", 2}};
    return p;
  }

  void expect_person_eq(const Person& out, const Person& p) {
    expect(out.name == p.name);
    expect(out.age == p.age);
    expect(out.skill.name == p.skill.name);
    expect(out.skill.level == p.skill.level);
    expect(out.scores == p.scores);
    expect(out.tags == p.tags);
    expect(out.attrs.size() == p.attrs.size());
    for(const auto& [k, v] : p.attrs) {
      auto it = out.attrs.find(k);
      expect(it != out.attrs.end());
      expect(it->second == v);
    }
  }

} // namespace

int main() {
  // Test 1: scalar parity — both writers produce identical bytes for scalars.
  "flat writer scalar parity"_test = [] {
    auto fn = [](auto& w) {
      w.write_u64(0xDEADBEEFCAFELL);
      w.write_i64(-42);
      w.write_u32(0x12345678);
      w.write_i32(-7);
      w.write_u16(0xABCD);
      w.write_i16(-1);
      w.write_u8(0xFF);
      w.write_i8(-128);
      w.write_f64(3.14159265358979);
      w.write_f32(2.71828f);
      w.write_bool(true);
      w.write_string("hello");
      w.write_binary({1, 2, 3, 4, 5});
      w.write_length(42);
    };
    auto s = write_stream(fn);
    auto c = write_circular(fn);
    expect(bytes_equal(s, c));
  };
  "flat writer frame parity"_test = [] {
    auto fn = [](auto& w) {
      w.begin_frame();
      w.write_string("field1");
      w.save_offset();
      w.write_u32(100);
      w.save_offset();
      w.write_string("field2");
      w.end_frame();
    };
    auto s = write_stream(fn);
    auto c = write_circular(fn);
    expect(bytes_equal(s, c));
  };

  // Test 3: map parity — both writers produce identical SoA layout.
  "flat writer map parity"_test = [] {
    std::map<std::string, std::int32_t> m = {{"alpha", 1}, {"beta", 2}, {"gamma", 3}};
    auto fn = [&m](auto& w) {
      w.write_map(m,
        [](auto& w, const std::string& k) { w.write_string(k); },
        [](auto& w, const std::int32_t& v) { w.write_i32(v); });
    };
    auto s = write_stream(fn);
    auto c = write_circular(fn);
    expect(bytes_equal(s, c));
  };

  // Test 4: typed parity — both writers produce identical typed dispatch.
  "flat writer typed parity"_test = [] {
    auto fn = [](auto& w) {
      w.template register_serialize_typed_function<Person>();
      auto p = std::make_shared<Person>(sample_person());
      w.template write_typed<Person>(p);
    };
    auto s = write_stream(fn);
    auto c = write_circular(fn);
    expect(bytes_equal(s, c));
  };

  // Test 5: exception parity — both writers produce identical exception bytes.
  "flat writer exception parity"_test = [] {
    auto fn = [](auto& w) {
      w.template register_serialize_exception_function<unknown_exception>();
      w.write_exception(unknown_exception("parity-boom"));
    };
    auto s = write_stream(fn);
    auto c = write_circular(fn);
    expect(bytes_equal(s, c));
  };

  // Test 6: message parity — both writers produce identical message bytes.
  "flat writer message parity"_test = [] {
    auto fn = [](auto& w) {
      w.template register_serialize_message_function<Ping>();
      auto msg = std::make_shared<Ping>();
      msg->value = 777;
      w.write_message(msg);
    };
    auto s = write_stream(fn);
    auto c = write_circular(fn);
    expect(bytes_equal(s, c));
  };

  // Test 7: cross reader — stream write, buffer read; buffer write, stream read.
  "flat cross reader roundtrip"_test = [] {
    Person p = sample_person();
    // Stream write -> buffer reader
    std::string bytes = write_stream([&p](auto& w) { serialize(w, p); });
    std::span<const std::uint8_t> byte_span(
      reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    flat_buffer_reader<span_flat_source> br{span_flat_source(byte_span)};
    Person out1{};
    deserialize(br, out1);
    br.read_rest();
    expect_person_eq(out1, p);

    // Circular write -> stream reader
    auto cbytes = write_circular([&p](auto& w) { serialize(w, p); });
    std::string sbytes(cbytes.begin(), cbytes.end());
    std::istringstream is(sbytes);
    flat_stream_reader sr(is);
    Person out2{};
    deserialize(sr, out2);
    sr.read_rest();
    expect_person_eq(out2, p);
  };

  // Test 8: cross typed/exception/message reader — stream write, buffer read.
  "flat cross typed/exception/message reader"_test = [] {
    // Register typed + exception + message, write through stream writer.
    std::string bytes = write_stream([](auto& w) {
      w.template register_serialize_typed_function<Person>();
      auto p = std::make_shared<Person>(sample_person());
      w.template write_typed<Person>(p);

      w.template register_serialize_exception_function<unknown_exception>();
      w.write_exception(unknown_exception("cross-boom"));

      w.template register_serialize_message_function<Ping>();
      auto msg = std::make_shared<Ping>();
      msg->value = 999;
      w.write_message(msg);
    });

    // Read through buffer reader.
    std::span<const std::uint8_t> byte_span(
      reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    flat_buffer_reader<span_flat_source> br{span_flat_source(byte_span)};

    br.register_deserialize_typed_function<Person>();
    std::shared_ptr<Person> p_out;
    br.read_typed<Person>(p_out);
    expect(p_out != nullptr);
    expect(p_out->name == "Ada");
    expect(p_out->age == 36);

    br.register_deserialize_exception_function<unknown_exception>();
    auto eptr = br.read_exception();
    try {
      std::rethrow_exception(eptr);
    } catch(const unknown_exception& e) {
      expect(std::string(e.what()) == "cross-boom");
    }

    br.register_deserialize_message_function<Ping>();
    std::shared_ptr<Ping> msg_out;
    br.read_message<Ping>(msg_out);
    expect(msg_out != nullptr);
    expect(msg_out->value == 999);
  };
}
