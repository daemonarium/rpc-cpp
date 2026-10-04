#include <boost/ut.hpp>

#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "serialization/circular_buffer_reader.hpp"
#include "serialization/circular_buffer_writer.hpp"
#include "serialization/flat_buffer_reader.hpp"
#include "serialization/flat_circular_buffer_writer.hpp"
#include "serialization/flat_source.hpp"
#include "serialization/flat_stream_reader.hpp"
#include "serialization/flat_stream_writer.hpp"
#include "serialization/flat_view.hpp"
#include "serialization/serialization_format.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/stream_writer.hpp"

extern "C" {
#include <io/circular_buffer.h>
}

using namespace boost::ut;
using namespace serialization;

namespace {

  struct shape {
    virtual ~shape() = default;
    std::uint64_t kind = 0;
  };

  struct circle : shape {
    std::uint64_t radius = 0;
  };

  struct rect : shape {
    std::uint64_t width = 0;
    std::uint64_t height = 0;
  };

  struct drawing {
    std::string title;
    std::shared_ptr<shape> main;
    std::vector<std::shared_ptr<shape>> extra;
    std::shared_ptr<shape> again;
  };

  template <typename W>
  void register_shapes(W& writer) {
    writer.template register_serialize_typed_function<circle>();
    writer.template register_serialize_typed_function<rect>();
  }

  template <typename R>
  void register_shapes_read(R& reader) {
    reader.template register_deserialize_typed_function<circle>();
    reader.template register_deserialize_typed_function<rect>();
  }

  drawing sample_drawing() {
    drawing d;
    d.title = "plan";
    auto c = std::make_shared<circle>();
    c->kind = 1;
    c->radius = 4;
    auto r = std::make_shared<rect>();
    r->kind = 2;
    r->width = 3;
    r->height = 5;
    d.main = c;
    d.extra.push_back(r);
    d.extra.push_back(nullptr);
    d.extra.push_back(c);
    d.again = c;
    return d;
  }

  bool same_shape(const std::shared_ptr<shape>& a, const std::shared_ptr<shape>& b) {
    if(!a || !b) return !a && !b;
    if(typeid(*a) != typeid(*b)) return false;
    if(auto ca = std::dynamic_pointer_cast<circle>(a)) {
      auto cb = std::dynamic_pointer_cast<circle>(b);
      return ca->kind == cb->kind && ca->radius == cb->radius;
    }
    auto ra = std::dynamic_pointer_cast<rect>(a);
    auto rb = std::dynamic_pointer_cast<rect>(b);
    return ra && rb && ra->kind == rb->kind && ra->width == rb->width && ra->height == rb->height;
  }

  void expect_drawing(const drawing& out, const drawing& src) {
    expect(eq(out.title, src.title));
    expect(same_shape(out.main, src.main));
    expect(eq(out.extra.size(), src.extra.size()));
    expect(out.main != nullptr);
    expect(out.again.get() == out.main.get());
    expect(out.extra[0] != nullptr);
    expect(same_shape(out.extra[0], src.extra[0]));
    expect(out.extra[1] == nullptr);
    expect(out.extra[2].get() == out.main.get());
    auto as_circle = std::dynamic_pointer_cast<circle>(out.main);
    expect(as_circle != nullptr);
    expect(eq(as_circle->radius, 4_u));
    auto as_rect = std::dynamic_pointer_cast<rect>(out.extra[0]);
    expect(as_rect != nullptr);
    expect(eq(as_rect->width, 3_u));
    expect(eq(as_rect->height, 5_u));
  }

  std::string encode_flat_drawing(const drawing& value) {
    std::ostringstream os;
    flat_stream_writer w(os);
    register_shapes(w);
    serialize(w, value);
    w.write_rest();
    return os.str();
  }

  // Shared-root framing so flat_view can walk per-frame prefixes.
  std::string encode_flat_drawing_framed(const drawing& value) {
    std::ostringstream os;
    flat_stream_writer w(os);
    register_shapes(w);
    std::size_t size_offset = w.position();
    w.write_u32(0);
    std::size_t frame_start = w.position();
    serialize(w, value);
    w.poke_u32(size_offset, static_cast<std::uint32_t>(w.position() - frame_start) + 1);
    w.write_rest();
    w.write_u32(0);
    return os.str();
  }

  drawing decode_flat_drawing(const std::string& bytes) {
    std::istringstream is(bytes);
    flat_stream_reader r(is);
    register_shapes_read(r);
    drawing out{};
    deserialize(r, out);
    r.read_rest();
    return out;
  }

  std::string encode_compressed_drawing(const drawing& value) {
    std::ostringstream os;
    stream_writer w(os);
    register_shapes(w);
    serialize(w, value);
    w.write_rest();
    return os.str();
  }

  drawing decode_compressed_drawing(const std::string& bytes) {
    std::istringstream is(bytes);
    stream_reader r(is);
    register_shapes_read(r);
    drawing out{};
    deserialize(r, out);
    r.read_rest();
    return out;
  }

  flat_view_factory_registry shape_factories() {
    flat_view_factory_registry factories;
    factories.register_type<circle>();
    factories.register_type<rect>();
    return factories;
  }

}

int main() {
  "flat wire version rejects stale peers"_test = [] {
    expect(eq(FLAT_WIRE_VERSION, 2_u));
    require_flat_wire_version(FLAT_WIRE_VERSION);
    expect(throws<std::runtime_error>([] {
      require_flat_wire_version(1);
    }));
  };

  "flat polymorphic graph materialized roundtrip"_test = [] {
    drawing src = sample_drawing();
    drawing out = decode_flat_drawing(encode_flat_drawing(src));
    expect_drawing(out, src);
  };

  "flat polymorphic graph in-place flat_view"_test = [] {
    drawing src = sample_drawing();
    std::string bytes = encode_flat_drawing_framed(src);
    auto storage = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
    auto session = std::make_shared<flat_session>(storage);
    auto factories = shape_factories();
    auto root = flat_detail::flat_root_shared_on_session<drawing>(
      session, 0, storage->size(), &factories);
    expect(root != nullptr);
    expect(eq(std::string(root->get<^^drawing::title>()), std::string{"plan"}));

    auto main = root->get<^^drawing::main>();
    expect(main.is_type<circle>());
    expect(!main.is_type<rect>());
    expect(main.get_type() == std::type_index(typeid(circle)));
    auto circle_view = main.get<circle>();
    expect(circle_view != nullptr);
    expect(eq(circle_view->get<^^shape::kind>(), 1_u));
    expect(eq(circle_view->get<^^circle::radius>(), 4_u));

    auto extra = root->get<^^drawing::extra>();
    expect(eq(extra.size(), std::size_t{3}));
    expect(extra[0].is_type<rect>());
    expect(extra[1] == nullptr);
    expect(!extra[1]);
    expect(extra[2].get<circle>().get() == circle_view.get());

    auto again = root->get<^^drawing::again>();
    expect(again.get<circle>().get() == circle_view.get());

    auto rect_view = extra[0].get<rect>();
    expect(eq(rect_view->get<^^rect::width>(), 3_u));
    expect(eq(rect_view->get<^^rect::height>(), 5_u));
  };

  "flat polymorphic base view skips derived trailer"_test = [] {
    drawing src = sample_drawing();
    std::string bytes = encode_flat_drawing_framed(src);
    auto storage = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
    auto session = std::make_shared<flat_session>(storage);
    auto factories = shape_factories();
    auto root = flat_detail::flat_root_shared_on_session<drawing>(
      session, 0, storage->size(), &factories);
    auto circle_view = root->get<^^drawing::main>().get<circle>();
    // shape has only a fixed field, so its view does not parse a trailer.
    flat_view<shape> as_base(circle_view->session(), circle_view->start(), circle_view->end());
    expect(eq(as_base.get<^^shape::kind>(), 1_u));
  };

  "flat polymorphic wrong type throws"_test = [] {
    drawing src = sample_drawing();
    std::string bytes = encode_flat_drawing_framed(src);
    auto storage = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
    auto session = std::make_shared<flat_session>(storage);
    auto factories = shape_factories();
    auto root = flat_detail::flat_root_shared_on_session<drawing>(
      session, 0, storage->size(), &factories);
    auto main = root->get<^^drawing::main>();
    expect(throws<std::runtime_error>([&] {
      main.get<rect>();
    }));
  };

  "flat polymorphic null pointer"_test = [] {
    drawing src;
    src.title = "empty";
    std::string bytes = encode_flat_drawing_framed(src);
    auto storage = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
    auto session = std::make_shared<flat_session>(storage);
    auto factories = shape_factories();
    auto root = flat_detail::flat_root_shared_on_session<drawing>(
      session, 0, storage->size(), &factories);
    auto main = root->get<^^drawing::main>();
    expect(main == nullptr);
    expect(!main);
    expect(main.get<circle>() == nullptr);
  };

  "flat polymorphic cross-format parity with compressed"_test = [] {
    drawing src = sample_drawing();
    drawing flat_out = decode_flat_drawing(encode_flat_drawing(src));
    drawing compressed_out = decode_compressed_drawing(encode_compressed_drawing(src));
    expect_drawing(flat_out, src);
    expect_drawing(compressed_out, src);
  };

  "flat polymorphic circular buffer roundtrip"_test = [] {
    drawing src = sample_drawing();
    struct circular_buffer* cb = circular_buffer_create(4096, nullptr);
    flat_circular_buffer_writer w(cb, 0);
    register_shapes(w);
    serialize(w, src);
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    circular_flat_source source(cb);
    flat_buffer_reader<circular_flat_source> r(source);
    register_shapes_read(r);
    drawing out{};
    deserialize(r, out);
    r.read_rest();
    expect_drawing(out, src);
    circular_buffer_destroy(cb);
  };
}
