#include <boost/ut.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

#include "serialization/stream_writer.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/circular_buffer_writer.hpp"
#include "serialization/circular_buffer_reader.hpp"
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
using person_fixture::encode_compressed;
using person_fixture::encode_compressed_shared;

namespace {

  template <typename T>
  T stream_roundtrip(const T& value) {
    std::string bytes = encode_compressed(value);
    std::istringstream is(bytes);
    stream_reader r(is);
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
    std::string expected = encode_compressed(value);
    size_t need = expected.size() + 256;
    if(need < 128) need = 128;
    struct circular_buffer* cb = circular_buffer_create(need, nullptr);
    if(wrap && expected.size() > 1) {
      size_t before = expected.size() / 2;
      if(before == 0) before = 1;
      force_near_seam(cb, before);
    }
    circular_buffer_writer w(cb, 0);
    serialize(w, value);
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    circular_buffer_reader r(cb, 0);
    T out{};
    deserialize(r, out);
    r.read_rest();
    circular_buffer_destroy(cb);
    return out;
  }

  void verify_person_value_all_paths(const Person& p) {
    expect(static_cast<bool>(stream_roundtrip(p) == p));
    expect(static_cast<bool>(circular_roundtrip(p, false) == p));
    expect(static_cast<bool>(circular_roundtrip(p, true) == p));
  }

  std::shared_ptr<Person> stream_roundtrip_shared(const std::shared_ptr<Person>& value) {
    std::string bytes = encode_compressed_shared(value);
    std::istringstream is(bytes);
    stream_reader r(is);
    r.register_deserialize_typed_function<Person>();
    std::shared_ptr<Person> out;
    deserialize(r, out);
    r.read_rest();
    return out;
  }

  std::shared_ptr<Person> circular_roundtrip_shared(const std::shared_ptr<Person>& value, bool wrap) {
    std::string expected = encode_compressed_shared(value);
    size_t need = expected.size() + 256;
    if(need < 128) need = 128;
    struct circular_buffer* cb = circular_buffer_create(need, nullptr);
    if(wrap && expected.size() > 1) {
      force_near_seam(cb, expected.size() / 3);
    }
    circular_buffer_writer w(cb, 0);
    w.register_serialize_typed_function<Person>();
    serialize(w, value);
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    circular_buffer_reader r(cb, 0);
    r.register_deserialize_typed_function<Person>();
    std::shared_ptr<Person> out;
    deserialize(r, out);
    r.read_rest();
    circular_buffer_destroy(cb);
    return out;
  }

}

int main() {
  "compressed generated person values: serialize/deserialize"_test = [] {
    constexpr int N = 80;
    for(int i = 0; i < N; ++i) {
      const std::uint64_t seed = 0xC0FFEEu + static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ULL;
      verify_person_value_all_paths(make_person_value(seed));
    }
  };

  "compressed generated person edge seeds + empty"_test = [] {
    for(std::uint64_t seed : {
        1ULL, 2ULL, 7ULL, 11ULL, 13ULL, 42ULL, 99ULL, 128ULL, 256ULL, 1000ULL, 0xDEADBEEFULL
      }) {
      verify_person_value_all_paths(make_person_value(seed));
    }
    Person empty;
    verify_person_value_all_paths(empty);
  };

  "compressed shared_ptr self-ref deserialize"_test = [] {
    auto a = std::make_shared<Person>();
    a->name = "Self";
    a->age = 7;
    a->friends.push_back(a);
    a->friends.push_back(a);
    a->friends.push_back(nullptr);

    std::string bytes = encode_compressed_shared(a);

    {
      auto out = stream_roundtrip_shared(a);
      expect(out != nullptr);
      expect(eq(out->name, std::string{"Self"}));
      expect(eq(out->friends.size(), std::size_t{3}));
      expect(out->friends[0].get() == out.get());
      expect(out->friends[1].get() == out.get());
      expect(out->friends[2] == nullptr);
      expect(eq(encode_compressed_shared(out), bytes));
    }

    {
      auto out = circular_roundtrip_shared(a, true);
      expect(out != nullptr);
      expect(out->friends[0].get() == out.get());
      expect(out->friends[1].get() == out.get());
      expect(out->friends[2] == nullptr);
      expect(eq(encode_compressed_shared(out), bytes));
    }
  };

  "compressed shared_ptr null root"_test = [] {
    std::shared_ptr<Person> nil;
    std::string bytes = encode_compressed_shared(nil);

    {
      auto out = stream_roundtrip_shared(nil);
      expect(out == nullptr);
      expect(eq(encode_compressed_shared(out), bytes));
    }
    for(bool wrap : {false, true}) {
      auto out = circular_roundtrip_shared(nil, wrap);
      expect(out == nullptr);
      expect(eq(encode_compressed_shared(out), bytes));
    }
  };

  "compressed shared_ptr graph (a<->b cycle)"_test = [] {
    auto a = std::make_shared<Person>();
    a->name = "A";
    a->age = 1;
    auto b = std::make_shared<Person>();
    b->name = "B";
    b->age = 2;
    a->friends.push_back(b);
    a->friends.push_back(nullptr);
    b->friends.push_back(a);

    std::string bytes = encode_compressed_shared(a);

    {
      auto out = stream_roundtrip_shared(a);
      expect(out != nullptr);
      expect(eq(out->name, std::string{"A"}));
      expect(eq(out->friends.size(), std::size_t{2}));
      expect(out->friends[0] != nullptr);
      expect(eq(out->friends[0]->name, std::string{"B"}));
      expect(out->friends[1] == nullptr);
      expect(out->friends[0]->friends.size() == 1_u);
      expect(out->friends[0]->friends[0].get() == out.get());
      expect(eq(encode_compressed_shared(out), bytes));
    }

    for(bool wrap : {false, true}) {
      auto out = circular_roundtrip_shared(a, wrap);
      expect(out != nullptr);
      expect(eq(out->name, std::string{"A"}));
      expect(eq(out->friends.size(), std::size_t{2}));
      expect(out->friends[0] != nullptr);
      expect(eq(out->friends[0]->name, std::string{"B"}));
      expect(out->friends[1] == nullptr);
      expect(out->friends[0]->friends.size() == 1_u);
      expect(out->friends[0]->friends[0].get() == out.get());
      expect(eq(encode_compressed_shared(out), bytes));
    }
  };

  "compressed generated person graphs: serialize/deserialize"_test = [] {
    constexpr int N = 32;
    for(int i = 0; i < N; ++i) {
      const std::uint64_t seed = 0xA11CEu + static_cast<std::uint64_t>(i) * 0xD1B54A32D192ED03ULL;
      auto root = make_person_graph(seed);
      std::string bytes = encode_compressed_shared(root);

      {
        auto out = stream_roundtrip_shared(root);
        expect(eq(encode_compressed_shared(out), bytes));
        std::map<const Person*, const Person*> seen;
        expect(static_cast<bool>(person_graph_equal(root, out, seen)));
      }

      {
        auto out = circular_roundtrip_shared(root, false);
        expect(eq(encode_compressed_shared(out), bytes));
        std::map<const Person*, const Person*> seen;
        expect(static_cast<bool>(person_graph_equal(root, out, seen)));
      }

      {
        auto out = circular_roundtrip_shared(root, true);
        expect(eq(encode_compressed_shared(out), bytes));
        std::map<const Person*, const Person*> seen;
        expect(static_cast<bool>(person_graph_equal(root, out, seen)));
      }
    }
  };
}
