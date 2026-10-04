#include <boost/ut.hpp>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

#include "serialization/flat_circular_buffer_writer.hpp"
#include "serialization/flat_stream_writer.hpp"
#include "serialization/flat_buffer_reader.hpp"
#include "serialization/flat_view.hpp"
#include "serialization/flat_source.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"

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
    std::vector<std::shared_ptr<Person>> friends;
    std::map<std::string, std::int32_t> attrs;
  };

  template <typename T>
  std::string encode_flat_stream(const T& value) {
    std::ostringstream os;
    flat_stream_writer w(os);
    serialize(w, value);
    w.write_rest();
    return os.str();
  }

  template <typename T>
  std::string encode_flat_stream_shared(const std::shared_ptr<T>& value) {
    std::ostringstream os;
    flat_stream_writer w(os);
    w.template register_serialize_typed_function<T>();
    if(!value) {
      w.write_u32(0); // sentinel only — null root
      return os.str();
    }
    w.mark_written_pointer(value); // root: back-refs use negative deltas
    std::size_t size_offset = w.position();
    w.write_u32(0); // encoded_size_0 placeholder
    std::size_t frame_start = w.position();
    serialize(w, *value); // deref, no root delta
    std::uint32_t real_size = static_cast<std::uint32_t>(w.position() - frame_start);
    w.poke_u32(size_offset, real_size + 1);
    w.write_rest(); // sub-frames with per-frame prefixes
    w.write_u32(0); // sentinel
    return os.str();
  }

  void force_read_near_end(struct circular_buffer* cb, size_t pad) {
    std::vector<std::uint8_t> junk(pad, 0xCD);
    size_t written = 0;
    while(written < pad) {
      size_t n = circular_buffer_write(cb, junk.data() + written, pad - written);
      if(n == 0) break;
      written += n;
    }
    circular_buffer_skip_read(cb, written);
  }

  template <typename T>
  size_t write_flat_circular(struct circular_buffer* cb, const T& value, size_t write_offset = 0) {
    flat_circular_buffer_writer w(cb, write_offset);
    serialize(w, value);
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);
    return w.write_offset_;
  }

  template <typename T>
  size_t write_flat_circular_shared(struct circular_buffer* cb, const std::shared_ptr<T>& value,
      size_t write_offset = 0) {
    flat_circular_buffer_writer w(cb, write_offset);
    w.template register_serialize_typed_function<T>();
    if(!value) {
      w.write_u32(0); // sentinel only — null root
      circular_buffer_commit_write(cb, w.write_offset_);
      return w.write_offset_;
    }
    w.mark_written_pointer(value); // root: back-refs use negative deltas
    std::size_t size_offset = w.write_offset_;
    w.write_u32(0); // encoded_size_0 placeholder
    std::size_t frame_start = w.write_offset_;
    serialize(w, *value); // deref, no root delta
    std::uint32_t real_size = static_cast<std::uint32_t>(w.write_offset_ - frame_start);
    std::uint32_t encoded_size = real_size + 1;
    circular_buffer_poke(cb, size_offset, &encoded_size, sizeof(encoded_size));
    w.write_rest(); // sub-frames with per-frame prefixes
    w.write_u32(0); // sentinel
    circular_buffer_commit_write(cb, w.write_offset_);
    return w.write_offset_;
  }

  std::vector<std::uint8_t> peek_all(struct circular_buffer* cb) {
    size_t n = circular_buffer_readable_bytes(cb);
    std::vector<std::uint8_t> out(n);
    if(n > 0) {
      expect(eq(circular_buffer_peek(cb, 0, out.data(), n), n));
    }
    return out;
  }

  Person sample_person() {
    Person p;
    p.name = "Ada";
    p.age = 36;
    p.skill = Skill{"math", 10};
    p.scores = {1, 2, 3};
    p.tags = {"a", "bb"};
    p.attrs = {{"hp", 100}, {"mp", 50}};
    return p;
  }

}

int main() {
  "flat circular writer scalars string"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(256, nullptr);
    expect(cb != nullptr);
    write_flat_circular(cb, std::string{"hello"});
    circular_flat_source src(cb);
    flat_buffer_reader<circular_flat_source> r(src);
    std::string out;
    deserialize(r, out);
    r.read_rest();
    expect(eq(out, std::string{"hello"}));
    circular_buffer_destroy(cb);
  };

  "flat circular writer udt map vector"_test = [] {
    Person p = sample_person();
    struct circular_buffer* cb = circular_buffer_create(2048, nullptr);
    write_flat_circular(cb, p);

    circular_flat_source src(cb);
    flat_buffer_reader<circular_flat_source> r(src);
    Person out{};
    deserialize(r, out);
    r.read_rest();
    expect(eq(out.name, p.name));
    expect(eq(out.age, p.age));
    expect(eq(out.skill.name, p.skill.name));
    expect(eq(out.skill.level, p.skill.level));
    expect(static_cast<bool>(out.scores == p.scores));
    expect(static_cast<bool>(out.tags == p.tags));
    expect(static_cast<bool>(out.attrs == p.attrs));

    auto view = flat_root_circular<Person>(cb, circular_buffer_readable_bytes(cb));
    expect(eq(std::string(view.get<^^Person::name>()), p.name));
    expect(eq(view.get<^^Person::attrs>().find(std::string{"mp"}), 50));
    circular_buffer_destroy(cb);
  };

  "flat circular writer byte identical to stream"_test = [] {
    Person p = sample_person();
    std::string stream_bytes = encode_flat_stream(p);

    struct circular_buffer* cb = circular_buffer_create(stream_bytes.size() + 64, nullptr);
    write_flat_circular(cb, p);
    auto ring = peek_all(cb);
    expect(eq(ring.size(), stream_bytes.size()));
    expect(static_cast<bool>(std::memcmp(ring.data(), stream_bytes.data(), ring.size()) == 0));
    circular_buffer_destroy(cb);
  };

  "flat circular writer wrap on write"_test = [] {
    Person p = sample_person();
    std::string expected = encode_flat_stream(p);

    size_t need = expected.size() + 128;
    struct circular_buffer* cb = circular_buffer_create(need, nullptr);
    size_t usable = circular_buffer_writable_bytes(cb) + 1;
    size_t before_wrap = expected.size() / 3;
    if(before_wrap == 0) before_wrap = 1;
    force_read_near_end(cb, usable - before_wrap);

    write_flat_circular(cb, p);
    expect(circular_buffer_region_contiguous(cb, 0, expected.size()) == 0);

    auto ring = peek_all(cb);
    expect(eq(ring.size(), expected.size()));
    expect(static_cast<bool>(std::memcmp(ring.data(), expected.data(), ring.size()) == 0));

    auto view = flat_root_circular<Person>(cb, ring.size());
    expect(view.session()->storage() == nullptr);
    expect(view.session()->is_circular());
    expect(eq(std::string(view.get<"name">()), p.name));
    expect(eq(view.get<^^Person::attrs>().find(std::string{"hp"}), 100));
    circular_buffer_destroy(cb);
  };

  "flat circular writer shared_ptr graph"_test = [] {
    auto a = std::make_shared<Person>();
    a->name = "Root";
    a->age = 10;
    auto b = std::make_shared<Person>();
    b->name = "Friend";
    b->age = 11;
    a->friends.push_back(b);
    a->friends.push_back(a);

    std::string expected = encode_flat_stream_shared(a);

    struct circular_buffer* cb = circular_buffer_create(expected.size() + 64, nullptr);
    write_flat_circular_shared(cb, a);
    auto ring = peek_all(cb);
    expect(eq(ring.size(), expected.size()));
    expect(static_cast<bool>(std::memcmp(ring.data(), expected.data(), ring.size()) == 0));

    auto root = flat_root_shared_circular<Person>(cb, ring.size());
    expect(root != nullptr);
    expect(eq(std::string(root->get<^^Person::name>()), std::string{"Root"}));
    auto friends = root->get<^^Person::friends>();
    expect(eq(friends.size(), std::size_t{2}));
    expect(eq(std::string(friends[0].get<Person>()->get<^^Person::name>()), std::string{"Friend"}));
    expect(friends[1].get<Person>().get() == root.get());
    circular_buffer_destroy(cb);
  };

  "flat circular writer shared_ptr wrap on write"_test = [] {
    auto a = std::make_shared<Person>();
    a->name = "A";
    auto b = std::make_shared<Person>();
    b->name = "B";
    a->friends.push_back(b);
    b->friends.push_back(a);
    std::string expected = encode_flat_stream_shared(a);

    size_t need = expected.size() + 128;
    struct circular_buffer* cb = circular_buffer_create(need, nullptr);
    size_t usable = circular_buffer_writable_bytes(cb) + 1;
    force_read_near_end(cb, usable - (expected.size() / 2));

    write_flat_circular_shared(cb, a);
    expect(circular_buffer_region_contiguous(cb, 0, expected.size()) == 0);

    auto root = flat_root_shared_circular<Person>(cb, expected.size());
    expect(root != nullptr);
    expect(root->session()->storage() == nullptr);
    expect(eq(std::string(root->get<^^Person::name>()), std::string{"A"}));
    expect(eq(std::string(root->get<^^Person::friends>()[0].get<Person>()->get<^^Person::name>()),
      std::string{"B"}));
    circular_buffer_destroy(cb);
  };

  "flat circular writer short space throws"_test = [] {
    // Fixed-size ring: grow callback refuses to enlarge past initial capacity.
    constexpr size_t cap = 8;
    auto no_grow = [](size_t) -> size_t {
      return cap + 12 + 1; // HEADER_SIZE(12) + slack(1), same as create(cap)
    };
    struct circular_buffer* cb = circular_buffer_create(cap, +no_grow);
    expect(throws<std::runtime_error>([&] {
      flat_circular_buffer_writer w(cb, 0);
      serialize(w, std::string(64, 'x'));
      w.write_rest();
    }));
    circular_buffer_destroy(cb);
  };

  "flat circular writer null buffer throws"_test = [] {
    expect(throws<std::runtime_error>([] {
      flat_circular_buffer_writer w(nullptr, 0);
    }));
  };
}
