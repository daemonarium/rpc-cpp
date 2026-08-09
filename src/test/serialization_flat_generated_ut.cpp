#include <boost/ut.hpp>

#include <cstdint>
#include <cstring>
#include <map>
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
#include "serialization/flat_view.hpp"
#include "serialization/flat_source.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"
#include "test/person_fixture.hpp"

using namespace boost::ut;
using namespace serialization;
using person_fixture::Person;
using person_fixture::Skill;
using person_fixture::make_person_value;
using person_fixture::make_person_graph;
using person_fixture::person_graph_equal;
using person_fixture::encode_flat;
using person_fixture::encode_flat_shared;
using person_fixture::decode_flat_shared;

namespace {

  template <typename T>
  T stream_roundtrip(const T& value) {
    std::string bytes = encode_flat(value);
    std::istringstream is(bytes);
    flat_stream_reader r(is);
    T out{};
    deserialize(r, out);
    r.read_rest();
    return out;
  }

  template <typename T>
  T buffer_roundtrip(const T& value) {
    std::string bytes = encode_flat(value);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    flat_buffer_reader<span_flat_source> r{span_flat_source(buf)};
    T out{};
    deserialize(r, out);
    r.read_rest();
    return out;
  }

  void force_near_seam(struct circular_buffer* cb, size_t before_wrap) {
    size_t usable = circular_buffer_writable_bytes(cb) + 1;
    if(before_wrap == 0) before_wrap = 1;
    if(before_wrap >= usable) before_wrap = usable / 2;
    if(before_wrap == 0) before_wrap = 1;
    size_t pad = usable - before_wrap;
    std::vector<std::uint8_t> junk(pad, 0xAB);
    size_t written = 0;
    while(written < junk.size()) {
      size_t n = circular_buffer_write(cb, junk.data() + written, junk.size() - written);
      if(n == 0) break;
      written += n;
    }
    circular_buffer_skip_read(cb, written);
  }

  template <typename T>
  T circular_roundtrip(const T& value, bool wrap) {
    std::string expected = encode_flat(value);
    size_t need = expected.size() + 128;
    if(need < 64) need = 64;
    struct circular_buffer* cb = circular_buffer_create(need, nullptr);
    if(wrap && expected.size() > 1) {
      size_t before = expected.size() / 2;
      if(before == 0) before = 1;
      force_near_seam(cb, before);
    }
    flat_circular_buffer_writer w(cb, 0);
    serialize(w, value);
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    circular_flat_source src(cb);
    flat_buffer_reader<circular_flat_source> r(src);
    T out{};
    deserialize(r, out);
    r.read_rest();
    circular_buffer_destroy(cb);
    return out;
  }

  void expect_skill_view(const flat_view<Skill>& view, const Skill& expected) {
    expect(eq(std::string(view.get<^^Skill::name>()), expected.name));
    expect(eq(view.get<^^Skill::level>(), expected.level));
  }

  void expect_person_view(const flat_view<Person>& view, const Person& expected) {
    expect(eq(std::string(view.get<^^Person::name>()), expected.name));
    expect(eq(view.get<^^Person::age>(), expected.age));
    expect_skill_view(view.get<^^Person::skill>(), expected.skill);

    auto scores = view.get<^^Person::scores>();
    expect(eq(scores.size(), expected.scores.size()));
    for(std::size_t i = 0; i < expected.scores.size(); ++i) {
      expect(eq(scores[i], expected.scores[i]));
    }

    auto tags = view.get<^^Person::tags>();
    expect(eq(tags.size(), expected.tags.size()));
    for(std::size_t i = 0; i < expected.tags.size(); ++i) {
      expect(eq(std::string(tags[i]), expected.tags[i]));
    }

    auto attrs = view.get<^^Person::attrs>();
    expect(eq(attrs.size(), expected.attrs.size()));
    for(const auto& [k, v] : expected.attrs) {
      expect(eq(attrs.find(k), v));
    }

    auto skills = view.get<^^Person::skills>();
    expect(eq(skills.size(), expected.skills.size()));
    for(const auto& [k, v] : expected.skills) {
      expect_skill_view(skills.find(k), v);
    }

    auto by_id = view.get<^^Person::by_id>();
    expect(eq(by_id.size(), expected.by_id.size()));
    for(const auto& [k, v] : expected.by_id) {
      expect(eq(std::string(by_id.find(k)), v));
    }

    auto friends = view.get<^^Person::friends>();
    expect(eq(friends.size(), expected.friends.size()));
  }

  void verify_person_value_all_paths(const Person& p) {
    expect(static_cast<bool>(stream_roundtrip(p) == p));
    expect(static_cast<bool>(buffer_roundtrip(p) == p));
    expect(static_cast<bool>(circular_roundtrip(p, false) == p));
    expect(static_cast<bool>(circular_roundtrip(p, true) == p));

    std::string bytes = encode_flat(p);
    auto storage = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
    flat_view<Person> view(std::make_shared<flat_session>(storage), 0, storage->size());
    expect_person_view(view, p);

    size_t need = bytes.size() + 64;
    if(need < 64) need = 64;
    struct circular_buffer* cb = circular_buffer_create(need, nullptr);
    flat_circular_buffer_writer w(cb, 0);
    serialize(w, p);
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);
    auto cview = flat_root_circular<Person>(cb, circular_buffer_readable_bytes(cb));
    expect_person_view(cview, p);
    circular_buffer_destroy(cb);
  }

  void expect_person_graph_view(const std::shared_ptr<flat_view<Person>>& view,
      const std::shared_ptr<Person>& expected,
      std::map<const void*, const Person*>& seen) {
    if(!expected) {
      expect(view == nullptr);
      return;
    }
    expect(view != nullptr);
    auto it = seen.find(view.get());
    if(it != seen.end()) {
      expect(it->second == expected.get());
      return;
    }
    seen[view.get()] = expected.get();
    expect_person_view(*view, *expected);
    auto friends = view->get<^^Person::friends>();
    expect(eq(friends.size(), expected->friends.size()));
    for(std::size_t i = 0; i < expected->friends.size(); ++i) {
      expect_person_graph_view(friends[i], expected->friends[i], seen);
    }
  }

}

int main() {
  "generated person values: serialize/deserialize + flat_view"_test = [] {
    constexpr int N = 80;
    for(int i = 0; i < N; ++i) {
      const std::uint64_t seed = 0xC0FFEEu + static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ULL;
      verify_person_value_all_paths(make_person_value(seed));
    }
  };

  "generated person edge seeds + empty"_test = [] {
    for(std::uint64_t seed : {
        1ULL, 2ULL, 7ULL, 11ULL, 13ULL, 42ULL, 99ULL, 128ULL, 256ULL, 1000ULL, 0xDEADBEEFULL
      }) {
      verify_person_value_all_paths(make_person_value(seed));
    }
    Person empty;
    verify_person_value_all_paths(empty);
  };

  "generated person graphs: serialize/deserialize + flat_view"_test = [] {
    constexpr int N = 32;
    for(int i = 0; i < N; ++i) {
      const std::uint64_t seed = 0xA11CEu + static_cast<std::uint64_t>(i) * 0xD1B54A32D192ED03ULL;
      auto root = make_person_graph(seed);
      std::string bytes = encode_flat_shared(root);

      {
        auto out = decode_flat_shared(bytes);
        expect(eq(encode_flat_shared(out), bytes));
        std::map<const Person*, const Person*> seen;
        expect(static_cast<bool>(person_graph_equal(root, out, seen)));
      }

      {
        auto storage = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
        auto view = flat_root_shared<Person>(storage);
        std::map<const void*, const Person*> seen;
        expect_person_graph_view(view, root, seen);
      }

      {
        size_t need = bytes.size() + 128;
        struct circular_buffer* cb = circular_buffer_create(need, nullptr);
        if(bytes.size() > 1) {
          force_near_seam(cb, bytes.size() / 3);
        }
        flat_circular_buffer_writer cw(cb, 0);
        cw.mark_written_pointer(root);
        std::size_t size_offset = cw.write_offset_;
        cw.write_u32(0);
        std::size_t frame_start = cw.write_offset_;
        serialize(cw, *root);
        std::uint32_t real_size = static_cast<std::uint32_t>(cw.write_offset_ - frame_start);
        std::uint32_t encoded_size = real_size + 1;
        circular_buffer_poke(cb, size_offset, &encoded_size, sizeof(encoded_size));
        cw.write_rest();
        cw.write_u32(0);
        circular_buffer_commit_write(cb, cw.write_offset_);

        circular_flat_source src(cb);
        flat_buffer_reader<circular_flat_source> r(src);
        std::uint32_t root_encoded_size = 0;
        r.read_u32(root_encoded_size);
        auto out = std::make_shared<Person>();
        r.mark_read_pointer(out);
        deserialize(r, *out);
        r.read_rest();
        std::uint32_t sentinel = 1;
        r.read_u32(sentinel);
        expect(eq(sentinel, 0u));
        expect(eq(encode_flat_shared(out), bytes));
        std::map<const Person*, const Person*> seen;
        expect(static_cast<bool>(person_graph_equal(root, out, seen)));

        auto cview = flat_root_shared_circular<Person>(cb, circular_buffer_readable_bytes(cb));
        std::map<const void*, const Person*> seen_v;
        expect_person_graph_view(cview, root, seen_v);
        circular_buffer_destroy(cb);
      }
    }
  };
}
