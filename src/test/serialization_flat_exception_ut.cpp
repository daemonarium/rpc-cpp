#include <boost/ut.hpp>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

#include "serialization/flat_stream_writer.hpp"
#include "serialization/flat_stream_reader.hpp"
#include "serialization/flat_buffer_reader.hpp"
#include "serialization/flat_circular_buffer_writer.hpp"
#include "serialization/flat_source.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"
#include "serialization/rpc.hpp"
#include "serialization/annotations.hpp"

using namespace boost::ut;
using namespace serialization;

namespace {

  struct [[=serialization::message()]] Ping {
    std::uint64_t id = 0;
    std::string text;
  };

  bool operator==(const Ping& a, const Ping& b) {
    return a.id == b.id && a.text == b.text;
  }

  void expect_exception_what(std::exception_ptr eptr, const std::string& expected) {
    expect(eptr != nullptr);
    try {
      std::rethrow_exception(eptr);
    } catch(const unknown_exception& e) {
      expect(eq(std::string(e.what()), expected));
    } catch(...) {
      expect(false) << "unexpected exception type";
    }
  }

}

int main() {
  "flat exception roundtrip stream"_test = [] {
    std::ostringstream os;
    flat_stream_writer w(os);
    w.register_serialize_exception_function<unknown_exception>();
    w.write_exception(unknown_exception("boom-stream"));
    w.write_rest();

    std::istringstream is(os.str());
    flat_stream_reader r(is);
    r.register_deserialize_exception_function<unknown_exception>();
    expect_exception_what(r.read_exception(), "boom-stream");
  };

  "flat exception roundtrip buffer"_test = [] {
    std::ostringstream os;
    flat_stream_writer w(os);
    w.register_serialize_exception_function<unknown_exception>();
    w.write_exception(unknown_exception("boom-buffer"));
    w.write_rest();
    std::string bytes = os.str();

    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    flat_buffer_reader<span_flat_source> r{span_flat_source(buf)};
    r.register_deserialize_exception_function<unknown_exception>();
    expect_exception_what(r.read_exception(), "boom-buffer");
  };

  "flat exception roundtrip circular"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(256, nullptr);
    flat_circular_buffer_writer w(cb, 0);
    w.register_serialize_exception_function<unknown_exception>();
    w.write_exception(unknown_exception("boom-circular"));
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    circular_flat_source src(cb);
    flat_buffer_reader<circular_flat_source> r(src);
    r.register_deserialize_exception_function<unknown_exception>();
    expect_exception_what(r.read_exception(), "boom-circular");
    circular_buffer_destroy(cb);
  };

  "flat message roundtrip stream"_test = [] {
    auto msg = std::make_shared<Ping>();
    msg->id = 42;
    msg->text = "hello";

    std::ostringstream os;
    flat_stream_writer w(os);
    w.register_serialize_message_function<Ping>();
    w.write_message(msg);
    w.write_rest();

    std::istringstream is(os.str());
    flat_stream_reader r(is);
    r.register_deserialize_message_function<Ping>();
    std::shared_ptr<Ping> out;
    r.read_message(out);
    r.read_rest();

    expect(out != nullptr);
    expect(eq(out->id, std::uint64_t{42}));
    expect(eq(out->text, std::string{"hello"}));
  };

  "flat message roundtrip circular"_test = [] {
    auto msg = std::make_shared<Ping>();
    msg->id = 7;
    msg->text = "ring";

    struct circular_buffer* cb = circular_buffer_create(256, nullptr);
    flat_circular_buffer_writer w(cb, 0);
    w.register_serialize_message_function<Ping>();
    w.write_message(msg);
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    circular_flat_source src(cb);
    flat_buffer_reader<circular_flat_source> r(src);
    r.register_deserialize_message_function<Ping>();
    std::shared_ptr<Ping> out;
    r.read_message(out);
    r.read_rest();

    expect(out != nullptr);
    expect(eq(out->id, std::uint64_t{7}));
    expect(eq(out->text, std::string{"ring"}));
    circular_buffer_destroy(cb);
  };

  "flat channel-like message dispatch"_test = [] {
    Ping msg{99, "dispatch"};

    std::ostringstream os;
    flat_stream_writer w(os);
    w.register_serialize_message_function<Ping>();
    // Simulate socket_message_channel: type index + serialize(header)
    w.write_u64(0);
    serialize(w, msg);
    w.write_rest();

    std::istringstream is(os.str());
    flat_stream_reader r(is);
    std::uint64_t index = 0;
    r.read_u64(index);
    expect(eq(index, std::uint64_t{0}));
    Ping out{};
    deserialize(r, out);
    r.read_rest();
    expect(static_cast<bool>(out == msg));
  };
}
