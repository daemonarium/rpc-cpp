#include <boost/ut.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <sstream>
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

using namespace boost::ut;
using namespace serialization;

namespace {

  class Rng {
    public:
      explicit Rng(std::uint64_t seed) : s_(seed ? seed : 0x9e3779b97f4a7c15ULL) {}

      std::uint64_t next_u64() {
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return s_ * 0x2545F4914F6CDD1DULL;
      }

      std::uint32_t next_u32() { return static_cast<std::uint32_t>(next_u64() >> 32); }

      std::int32_t next_i32(std::int32_t lo, std::int32_t hi) {
        if(hi <= lo) return lo;
        return lo + static_cast<std::int32_t>(next_u32() % static_cast<std::uint32_t>(hi - lo + 1));
      }

      bool next_bool() { return (next_u32() & 1u) != 0; }

      std::string next_string(std::size_t min_len, std::size_t max_len) {
        std::size_t n = static_cast<std::size_t>(next_i32(
          static_cast<std::int32_t>(min_len), static_cast<std::int32_t>(max_len)));
        static constexpr char alphabet[] =
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
        std::string out;
        out.reserve(n);
        for(std::size_t i = 0; i < n; ++i) {
          out.push_back(alphabet[next_u32() % (sizeof(alphabet) - 1)]);
        }
        return out;
      }

    private:
      std::uint64_t s_;
  };

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
    std::map<std::string, Skill> skills;
    std::map<std::int32_t, std::string> by_id;
  };

  bool operator==(const Skill& a, const Skill& b) {
    return a.name == b.name && a.level == b.level;
  }

  bool operator==(const Person& a, const Person& b) {
    return a.name == b.name && a.age == b.age && a.skill == b.skill
      && a.scores == b.scores && a.tags == b.tags && a.attrs == b.attrs
      && a.skills == b.skills && a.by_id == b.by_id;
  }

  Skill make_skill(Rng& rng, std::uint32_t salt) {
    Skill s;
    s.name = rng.next_bool() && (salt % 5 == 0) ? std::string{} : ("sk_" + rng.next_string(1, 10));
    s.level = rng.next_i32(-5, 100);
    return s;
  }

  Person make_person_value(std::uint64_t seed) {
    Rng rng(seed);
    Person p;
    p.name = (seed % 11 == 0) ? std::string{} : ("p_" + rng.next_string(0, 16));
    p.age = rng.next_i32(-1, 120);
    p.skill = make_skill(rng, static_cast<std::uint32_t>(seed));
    const int n_scores = rng.next_i32(0, 8);
    for(int i = 0; i < n_scores; ++i) {
      p.scores.push_back(rng.next_i32(-100, 100));
    }
    const int n_tags = rng.next_i32(0, 6);
    for(int i = 0; i < n_tags; ++i) {
      p.tags.push_back(rng.next_string(0, 12));
    }
    const int n_attrs = rng.next_i32(0, 6);
    for(int i = 0; i < n_attrs; ++i) {
      p.attrs["a" + std::to_string(i) + "_" + rng.next_string(1, 5)] = rng.next_i32(-50, 50);
    }
    const int n_skills = rng.next_i32(0, 4);
    for(int i = 0; i < n_skills; ++i) {
      p.skills["s" + rng.next_string(1, 6)] = make_skill(rng, static_cast<std::uint32_t>(seed) + static_cast<std::uint32_t>(i));
    }
    const int n_ids = rng.next_i32(0, 5);
    for(int i = 0; i < n_ids; ++i) {
      p.by_id[rng.next_i32(-20, 20)] = rng.next_string(0, 10);
    }
    return p;
  }

  template <typename T>
  std::string encode_compressed(const T& value) {
    std::ostringstream os;
    stream_writer w(os);
    serialize(w, value);
    w.write_rest();
    return os.str();
  }

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

  std::shared_ptr<Person> make_person_graph(std::uint64_t seed) {
    Rng rng(seed);
    const int n = rng.next_i32(2, 6);
    std::vector<std::shared_ptr<Person>> nodes;
    nodes.reserve(static_cast<std::size_t>(n));
    for(int i = 0; i < n; ++i) {
      auto node = std::make_shared<Person>();
      node->name = "g" + std::to_string(i) + "_" + rng.next_string(0, 8);
      node->age = rng.next_i32(0, 80);
      nodes.push_back(node);
    }
    for(int i = 0; i < n; ++i) {
      const int edges = rng.next_i32(1, 3);
      for(int e = 0; e < edges; ++e) {
        nodes[static_cast<std::size_t>(i)]->friends.push_back(
          nodes[static_cast<std::size_t>(rng.next_i32(0, n - 1))]);
      }
      if(rng.next_bool()) {
        nodes[static_cast<std::size_t>(i)]->friends.push_back(nullptr);
      }
    }
    // Always include a self-ref — exercises the read_rest assign fix.
    nodes[0]->friends.push_back(nodes[0]);
    return nodes[0];
  }

  bool person_ptr_fields_equal(const Person& a, const Person& b) {
    return a.name == b.name && a.age == b.age && a.skill == b.skill
      && a.scores == b.scores && a.tags == b.tags && a.attrs == b.attrs
      && a.skills == b.skills && a.by_id == b.by_id;
  }

  bool person_graph_equal(const std::shared_ptr<Person>& a, const std::shared_ptr<Person>& b,
      std::map<const Person*, const Person*>& seen) {
    if(!a || !b) return a.get() == b.get();
    auto it = seen.find(a.get());
    if(it != seen.end()) return it->second == b.get();
    seen[a.get()] = b.get();
    if(!person_ptr_fields_equal(*a, *b)) return false;
    if(a->friends.size() != b->friends.size()) return false;
    for(std::size_t i = 0; i < a->friends.size(); ++i) {
      if(!person_graph_equal(a->friends[i], b->friends[i], seen)) return false;
    }
    return true;
  }

  std::string encode_compressed_shared(const std::shared_ptr<Person>& value) {
    std::ostringstream os;
    stream_writer w(os);
    w.register_serialize_typed_function<Person>();
    serialize(w, value);
    w.write_rest();
    return os.str();
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
