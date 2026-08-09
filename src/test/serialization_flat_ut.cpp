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

  enum class Color : std::uint64_t {
    Red = 1,
    Green = 2,
    Blue = 3,
  };

  struct Skill {
    std::string name;
    std::int32_t level = 0;
  };

  struct OnlyScalars {
    std::int32_t a = 0;
    std::int32_t b = 0;
    bool flag = false;
  };

  struct WithEnum {
    Color color = Color::Red;
    std::string label;
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

  struct WithMaps {
    std::map<std::string, std::int32_t> si;
    std::map<std::int32_t, std::string> is;
    std::map<std::string, Skill> skills;
  };

  template <typename T>
  std::string encode_flat(const T& value) {
    std::ostringstream os;
    flat_stream_writer w(os);
    serialize(w, value);
    w.write_rest();
    return os.str();
  }

  template <typename T>
  T flat_roundtrip(const T& value) {
    std::string bytes = encode_flat(value);
    std::istringstream is(bytes);
    flat_stream_reader r(is);
    T out{};
    deserialize(r, out);
    r.read_rest();
    return out;
  }

  template <typename T>
  T flat_buffer_roundtrip(const T& value) {
    std::string bytes = encode_flat(value);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    flat_buffer_reader<span_flat_source> r{span_flat_source(buf)};
    T out{};
    deserialize(r, out);
    r.read_rest();
    return out;
  }

  template <typename T>
  std::size_t measure_root_frame(const std::shared_ptr<std::vector<std::uint8_t>>& storage) {
    std::string chunk(reinterpret_cast<const char*>(storage->data()), storage->size());
    std::istringstream iss(chunk);
    flat_stream_reader sr(iss);
    T tmp{};
    deserialize(sr, tmp);
    std::streamoff told = iss.tellg();
    return told < 0 ? storage->size() : static_cast<std::size_t>(told);
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
    auto bytes = encode_flat(v);
    expect(eq(bytes.size(), std::size_t{4 + 5 * 4}));
    expect(flat_roundtrip(v) == v);
  };

  "flat empty vector"_test = [] {
    std::vector<std::int32_t> empty_fixed;
    expect(static_cast<bool>(flat_roundtrip(empty_fixed) == empty_fixed));
    expect(eq(encode_flat(empty_fixed).size(), std::size_t{4}));

    std::vector<std::string> empty_var;
    expect(static_cast<bool>(flat_roundtrip(empty_var) == empty_var));

    auto storage = std::make_shared<std::vector<std::uint8_t>>();
    {
      std::string s = encode_flat(empty_fixed);
      storage->assign(s.begin(), s.end());
    }
    auto session = std::make_shared<flat_session>(storage);
    flat_vector_view<std::int32_t> view(session, 0, storage->size());
    expect(eq(view.size(), std::size_t{0}));
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

  "flat udt scalars only no trailer"_test = [] {
    OnlyScalars o{7, -3, true};
    auto bytes = encode_flat(o);
    expect(eq(bytes.size(), std::size_t{4 + 4 + 1}));
    auto out = flat_roundtrip(o);
    expect(eq(out.a, 7));
    expect(eq(out.b, -3));
    expect(eq(out.flag, true));

    auto storage = std::make_shared<std::vector<std::uint8_t>>();
    storage->assign(bytes.begin(), bytes.end());
    auto session = std::make_shared<flat_session>(storage);
    flat_view<OnlyScalars> view(session, 0, storage->size());
    expect(eq(view.get<^^OnlyScalars::a>(), 7));
    expect(eq(view.get<"b">(), -3));
  };

  "flat enum and empty string field"_test = [] {
    WithEnum w{Color::Blue, ""};
    auto out = flat_roundtrip(w);
    expect(out.color == Color::Blue);
    expect(eq(out.label, std::string{""}));

    auto storage = std::make_shared<std::vector<std::uint8_t>>();
    {
      std::string s = encode_flat(w);
      storage->assign(s.begin(), s.end());
    }
    auto session = std::make_shared<flat_session>(storage);
    flat_view<WithEnum> view(session, 0, measure_root_frame<WithEnum>(storage));
    expect(view.get<^^WithEnum::color>() == Color::Blue);
    expect(eq(std::string(view.get<"label">()), std::string{""}));
  };

  "flat map empty"_test = [] {
    std::map<std::string, std::int32_t> empty;
    auto bytes = encode_flat(empty);
    expect(eq(bytes.size(), std::size_t{4}));
    expect(static_cast<bool>(flat_roundtrip(empty) == empty));
  };

  "flat map types"_test = [] {
    std::map<std::string, std::int32_t> si{{"x", 1}, {"y", 2}};
    expect(static_cast<bool>(flat_roundtrip(si) == si));

    std::map<std::int32_t, std::string> is{{1, "a"}, {2, "bb"}};
    expect(static_cast<bool>(flat_roundtrip(is) == is));

    std::map<std::string, std::string> ss{{"k", "v"}, {"aa", "bb"}};
    expect(static_cast<bool>(flat_roundtrip(ss) == ss));

    std::map<std::int32_t, std::int32_t> ii{{3, 4}, {1, 2}};
    expect(static_cast<bool>(flat_roundtrip(ii) == ii));

    std::map<std::string, Skill> skills{{"c++", Skill{"c++", 9}}, {"go", Skill{"go", 3}}};
    auto skills_out = flat_roundtrip(skills);
    expect(eq(skills_out.size(), std::size_t{2}));
    expect(eq(skills_out["c++"].level, 9));
    expect(eq(skills_out["go"].name, std::string{"go"}));
  };

  "flat map in udt"_test = [] {
    WithMaps m;
    m.si = {{"a", 1}, {"b", 2}};
    m.is = {{10, "ten"}};
    m.skills = {{"math", Skill{"math", 5}}};
    auto out = flat_roundtrip(m);
    expect(static_cast<bool>(out.si == m.si));
    expect(static_cast<bool>(out.is == m.is));
    expect(eq(out.skills["math"].level, 5));
    expect(static_cast<bool>(flat_buffer_roundtrip(m).si == m.si));
  };

  "flat map memcmp order find"_test = [] {
    // length-prefix memcmp order: "b" (len=1) before "aa" (len=2)
    std::map<std::string, std::int32_t> m{{"aa", 2}, {"b", 1}};
    expect(static_cast<bool>(flat_roundtrip(m) == m));

    auto storage = std::make_shared<std::vector<std::uint8_t>>();
    {
      std::string s = encode_flat(m);
      storage->assign(s.begin(), s.end());
    }
    auto session = std::make_shared<flat_session>(storage);
    flat_map_view<std::string, std::int32_t> view(session, 0, storage->size());
    expect(eq(view.size(), std::size_t{2}));
    expect(eq(view.find(std::string{"b"}), 1));
    expect(eq(view.find(std::string{"aa"}), 2));
    expect(eq(view["b"], 1));
    expect(!view.contains(std::string{"zzz"}));
    bool threw = false;
    try {
      (void)view.find(std::string{"zzz"});
    } catch(...) {
      threw = true;
    }
    expect(threw);
  };

  "flat_map_view in udt get"_test = [] {
    Person p;
    p.name = "Eve";
    p.age = 42;
    p.attrs = {{"hp", 100}, {"mp", 50}};

    auto storage = std::make_shared<std::vector<std::uint8_t>>();
    {
      std::string s = encode_flat(p);
      storage->assign(s.begin(), s.end());
    }
    auto session = std::make_shared<flat_session>(storage);
    flat_view<Person> view(session, 0, measure_root_frame<Person>(storage));
    auto attrs = view.get<^^Person::attrs>();
    expect(eq(attrs.size(), std::size_t{2}));
    expect(eq(attrs.find(std::string{"hp"}), 100));
    expect(eq(attrs["mp"], 50));
    expect(!attrs.contains(std::string{"xp"}));
  };

  "flat map int key string value view"_test = [] {
    std::map<std::int32_t, std::string> m{{2, "two"}, {1, "one"}};
    auto storage = std::make_shared<std::vector<std::uint8_t>>();
    {
      std::string s = encode_flat(m);
      storage->assign(s.begin(), s.end());
    }
    auto session = std::make_shared<flat_session>(storage);
    flat_map_view<std::int32_t, std::string> view(session, 0, storage->size());
    expect(eq(std::string(view.find(1)), std::string{"one"}));
    expect(eq(std::string(view[2]), std::string{"two"}));
  };

  "flat map W_k smoke"_test = [] {
    std::map<std::string, std::int32_t> m;
    for(int i = 0; i < 40; ++i) {
      m[std::string(20, static_cast<char>('a' + (i % 26))) + std::to_string(i)] = i;
    }
    expect(static_cast<bool>(flat_roundtrip(m) == m));
    auto bytes = encode_flat(m);
    expect(bytes.size() > 256u);
  };

  "flat map nested with vector"_test = [] {
    std::map<std::string, std::vector<std::int32_t>> m{
      {"a", {1, 2}},
      {"b", {3}}
    };
    auto out = flat_roundtrip(m);
    expect(static_cast<bool>(out["a"] == m["a"]));
    expect(static_cast<bool>(out["b"] == m["b"]));

    std::vector<std::map<std::string, std::int32_t>> v{
      {{"x", 1}},
      {{"y", 2}, {"z", 3}}
    };
    expect(static_cast<bool>(flat_roundtrip(v) == v));
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

  "flat shared_ptr self-ref deserialize"_test = [] {
    auto a = std::make_shared<Person>();
    a->name = "Self";
    a->age = 7;
    a->friends.push_back(a);
    a->friends.push_back(a);
    a->friends.push_back(nullptr);

    std::ostringstream os;
    flat_stream_writer w(os);
    w.register_serialize_typed_function<Person>();
    serialize(w, a);
    w.write_rest();
    std::string bytes = os.str();

    std::istringstream is(bytes);
    flat_stream_reader r(is);
    r.register_deserialize_typed_function<Person>();
    std::shared_ptr<Person> out;
    deserialize(r, out);
    r.read_rest();

    expect(out != nullptr);
    expect(eq(out->name, std::string{"Self"}));
    expect(eq(out->friends.size(), std::size_t{3}));
    expect(out->friends[0].get() == out.get());
    expect(out->friends[1].get() == out.get());
    expect(out->friends[2] == nullptr);

    std::ostringstream os2;
    flat_stream_writer w2(os2);
    w2.register_serialize_typed_function<Person>();
    serialize(w2, out);
    w2.write_rest();
    expect(eq(os2.str(), bytes));
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
      std::string s = encode_flat(p);
      storage->assign(s.begin(), s.end());
    }

    auto session = std::make_shared<flat_session>(storage);
    flat_view<Person> view(session, 0, measure_root_frame<Person>(storage));

    expect(eq(std::string(view.get<^^Person::name>()), std::string{"Eve"}));
    expect(eq(view.get<^^Person::age>(), 42));
    expect(eq(std::string(view.get<"name">()), std::string{"Eve"}));
    {
      flat_string name = view.get<^^Person::name>();
      expect(!name.owns());
      expect(eq(std::string(name), std::string{"Eve"}));
    }

    auto skill = view.get<^^Person::skill>();
    expect(eq(std::string(skill.get<^^Skill::name>()), std::string{"go"}));
    expect(!skill.get<^^Skill::name>().owns());
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
