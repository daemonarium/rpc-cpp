#include <boost/ut.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "serialization/flat_stream_writer.hpp"
#include "serialization/flat_stream_reader.hpp"
#include "serialization/flat_buffer_reader.hpp"
#include "serialization/flat_view.hpp"
#include "serialization/flat_encoding.hpp"
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
  };

  template <typename T>
  std::string flat_bytes(const T& value) {
    std::ostringstream os;
    flat_stream_writer w(os);
    serialize(w, value);
    w.write_rest();
    return os.str();
  }

  template <typename T>
  T flat_roundtrip(const T& value) {
    std::string bytes = flat_bytes(value);
    std::istringstream is(bytes);
    flat_stream_reader r(is);
    T out{};
    deserialize(r, out);
    r.read_rest();
    return out;
  }

  template <typename T>
  T flat_buffer_roundtrip(const T& value) {
    std::string bytes = flat_bytes(value);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    flat_buffer_reader<span_flat_source> r{span_flat_source(buf)};
    T out{};
    deserialize(r, out);
    r.read_rest();
    return out;
  }

}

int main() {
  "flat le scalars"_test = [] {
    expect(eq(flat_roundtrip(std::uint8_t{0x7Fu}), std::uint8_t{0x7Fu}));
    expect(eq(flat_roundtrip(std::int8_t{-5}), std::int8_t{-5}));
    expect(eq(flat_roundtrip(std::uint16_t{0xABCD}), std::uint16_t{0xABCD}));
    expect(eq(flat_roundtrip(std::int32_t{-123456}), std::int32_t{-123456}));
    expect(eq(flat_roundtrip(std::uint64_t{0x1122334455667788ULL}), std::uint64_t{0x1122334455667788ULL}));
    expect(eq(flat_roundtrip(true), true));
    expect(eq(flat_roundtrip(false), false));
    expect(eq(flat_roundtrip(3.5f), 3.5f));
    expect(eq(flat_roundtrip(1.25), 1.25));
  };

  "flat string binary"_test = [] {
    expect(eq(flat_roundtrip(std::string{"hello"}), std::string{"hello"}));
    expect(eq(flat_roundtrip(std::string{""}), std::string{""}));
    std::vector<std::uint8_t> bin{1, 2, 3, 4};
    expect(flat_roundtrip(bin) == bin);
  };

  "flat vector fixed no trailer"_test = [] {
    std::vector<std::int32_t> v{1, 2, 3, 4, 5};
    auto bytes = flat_bytes(v);
    // length u32 + 5*i32, K=0 => no trailer
    expect(eq(bytes.size(), std::size_t{4 + 5 * 4}));
    expect(flat_roundtrip(v) == v);
  };

  "flat udt nested"_test = [] {
    Person p;
    p.name = "Ada";
    p.age = 36;
    p.skill = Skill{"math", 10};
    p.scores = {1, 2, 3};
    p.tags = {"a", "bb", "ccc"};
    auto out = flat_roundtrip(p);
    expect(eq(out.name, std::string{"Ada"}));
    expect(eq(out.age, 36));
    expect(eq(out.skill.name, std::string{"math"}));
    expect(eq(out.skill.level, 10));
    expect(out.scores == p.scores);
    expect(out.tags == p.tags);
  };

  "flat map"_test = [] {
    std::map<std::string, std::int32_t> m{{"x", 1}, {"y", 2}};
    expect(flat_roundtrip(m) == m);
  };

  "flat buffer reader"_test = [] {
    Person p;
    p.name = "Bob";
    p.age = 20;
    p.skill = Skill{"c++", 7};
    auto out = flat_buffer_roundtrip(p);
    expect(eq(out.name, std::string{"Bob"}));
    expect(eq(out.age, 20));
    expect(eq(out.skill.level, 7));
  };

  "flat shared_ptr graph"_test = [] {
    auto a = std::make_shared<Person>();
    a->name = "A";
    a->age = 1;
    auto b = std::make_shared<Person>();
    b->name = "B";
    b->age = 2;
    a->friends.push_back(b);
    a->friends.push_back(nullptr);
    b->friends.push_back(a);

    std::ostringstream os;
    flat_stream_writer w(os);
    w.register_serialize_typed_function<Person>();
    serialize(w, a);
    w.write_rest();

    std::istringstream is(os.str());
    flat_stream_reader r(is);
    r.register_deserialize_typed_function<Person>();
    std::shared_ptr<Person> out;
    deserialize(r, out);
    r.read_rest();

    expect(out != nullptr);
    expect(eq(out->name, std::string{"A"}));
    expect(eq(out->friends.size(), std::size_t{2}));
    expect(out->friends[0] != nullptr);
    expect(eq(out->friends[0]->name, std::string{"B"}));
    expect(out->friends[1] == nullptr);
    expect(out->friends[0]->friends.size() == 1_u);
    expect(out->friends[0]->friends[0].get() == out.get());
  };

  "flat_view getters"_test = [] {
    Person p;
    p.name = "Eve";
    p.age = 42;
    p.skill = Skill{"go", 3};
    p.scores = {9, 8};
    p.tags = {"x", "yz"};

    auto storage = std::make_shared<std::vector<std::uint8_t>>();
    {
      std::string s = flat_bytes(p);
      storage->assign(s.begin(), s.end());
    }

    // Measure root frame (no rest for this value without pending pointers)
    std::string chunk(reinterpret_cast<const char*>(storage->data()), storage->size());
    std::istringstream iss(chunk);
    flat_stream_reader sr(iss);
    Person tmp{};
    deserialize(sr, tmp);
    std::streamoff told = iss.tellg();
    std::size_t frame_end = told < 0 ? storage->size() : static_cast<std::size_t>(told);

    auto session = std::make_shared<flat_session>(storage);
    flat_view<Person> view(session, 0, frame_end);

    expect(eq(std::string(view.get<^^Person::name>()), std::string{"Eve"}));
    expect(eq(view.get<^^Person::age>(), 42));
    expect(eq(std::string(view.get<"name">()), std::string{"Eve"}));

    auto skill = view.get<^^Person::skill>();
    expect(eq(std::string(skill.get<^^Skill::name>()), std::string{"go"}));
    expect(eq(skill.get<^^Skill::level>(), 3));

    auto scores = view.get<^^Person::scores>();
    expect(eq(scores.size(), std::size_t{2}));
    expect(eq(scores[0], 9));
    expect(eq(scores[1], 8));

    auto tags = view.get<^^Person::tags>();
    expect(eq(tags.size(), std::size_t{2}));
    expect(eq(std::string(tags[0]), std::string{"x"}));
    expect(eq(std::string(tags[1]), std::string{"yz"}));
  };

  "flat_view shared_ptr"_test = [] {
    auto a = std::make_shared<Person>();
    a->name = "Root";
    a->age = 10;
    auto b = std::make_shared<Person>();
    b->name = "Friend";
    b->age = 11;
    a->friends.push_back(b);
    a->friends.push_back(a);

    auto storage = std::make_shared<std::vector<std::uint8_t>>();
    {
      std::ostringstream os;
      flat_stream_writer w(os);
      w.register_serialize_typed_function<Person>();
      serialize(w, a);
      w.write_rest();
      std::string s = os.str();
      storage->assign(s.begin(), s.end());
    }

    auto root = flat_root_shared<Person>(storage);
    expect(root != nullptr);
    expect(eq(std::string(root->get<^^Person::name>()), std::string{"Root"}));
    expect(eq(root->get<^^Person::age>(), 10));

    auto friends = root->get<^^Person::friends>();
    expect(eq(friends.size(), std::size_t{2}));
    auto f0 = friends[0];
    auto f1 = friends[1];
    expect(f0 != nullptr);
    expect(f1 != nullptr);
    expect(eq(std::string(f0->get<^^Person::name>()), std::string{"Friend"}));
    expect(f1.get() == root.get());
  };

  "flat encoding helpers"_test = [] {
    expect(eq(flat_offset_width(10), std::size_t{1}));
    expect(eq(flat_offset_width(300), std::size_t{2}));
    expect(eq(flat_offset_width(70000), std::size_t{4}));
    expect(eq(flat_trailer_size(0, 4), std::size_t{0}));
    expect(eq(flat_trailer_size(3, 2), std::size_t{2 + 3 * 2}));
  };
}
