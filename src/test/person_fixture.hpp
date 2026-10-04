#pragma once

/**
 * Shared Person/Skill fixture for generated UTs and cross-language TSV dumps.
 * Rng + make_* must stay bit-identical with ts-rpc/tests/personFixture.ts.
 */

#include <cstdint>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "serialization/stream_writer.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/flat_stream_writer.hpp"
#include "serialization/flat_stream_reader.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"

namespace person_fixture {

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

inline bool operator==(const Skill& a, const Skill& b) {
  return a.name == b.name && a.level == b.level;
}

inline bool operator==(const Person& a, const Person& b) {
  return a.name == b.name && a.age == b.age && a.skill == b.skill
    && a.scores == b.scores && a.tags == b.tags && a.attrs == b.attrs
    && a.skills == b.skills && a.by_id == b.by_id;
}

inline Skill make_skill(Rng& rng, std::uint32_t salt) {
  Skill s;
  s.name = rng.next_bool() && (salt % 5 == 0) ? std::string{} : ("sk_" + rng.next_string(1, 10));
  s.level = rng.next_i32(-5, 100);
  return s;
}

inline Person make_person_value(std::uint64_t seed) {
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
    // Sequence RNG calls — map[key]=val argument order is otherwise unspecified.
    std::string key = "a" + std::to_string(i) + "_" + rng.next_string(1, 5);
    std::int32_t val = rng.next_i32(-50, 50);
    p.attrs[key] = val;
  }
  const int n_skills = rng.next_i32(0, 4);
  for(int i = 0; i < n_skills; ++i) {
    std::string key = "s" + rng.next_string(1, 6);
    Skill sk = make_skill(rng, static_cast<std::uint32_t>(seed) + static_cast<std::uint32_t>(i));
    p.skills[key] = sk;
  }
  const int n_ids = rng.next_i32(0, 5);
  for(int i = 0; i < n_ids; ++i) {
    std::int32_t key = rng.next_i32(-20, 20);
    std::string val = rng.next_string(0, 10);
    p.by_id[key] = val;
  }
  return p;
}

inline std::shared_ptr<Person> make_person_graph(std::uint64_t seed) {
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
  nodes[0]->friends.push_back(nodes[0]);
  return nodes[0];
}

inline bool person_ptr_fields_equal(const Person& a, const Person& b) {
  return a.name == b.name && a.age == b.age && a.skill == b.skill
    && a.scores == b.scores && a.tags == b.tags && a.attrs == b.attrs
    && a.skills == b.skills && a.by_id == b.by_id;
}

inline bool person_graph_equal(const std::shared_ptr<Person>& a, const std::shared_ptr<Person>& b,
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

template <typename T>
inline std::string encode_compressed(const T& value) {
  std::ostringstream os;
  serialization::stream_writer w(os);
  serialization::serialize(w, value);
  w.write_rest();
  return os.str();
}

inline std::string encode_compressed_shared(const std::shared_ptr<Person>& value) {
  std::ostringstream os;
  serialization::stream_writer w(os);
  w.register_serialize_typed_function<Person>();
  serialization::serialize(w, value);
  w.write_rest();
  return os.str();
}

template <typename T>
inline std::string encode_flat(const T& value) {
  std::ostringstream os;
  serialization::flat_stream_writer w(os);
  serialization::serialize(w, value);
  w.write_rest();
  return os.str();
}

inline std::string encode_flat_shared(const std::shared_ptr<Person>& value) {
  std::ostringstream os;
  serialization::flat_stream_writer w(os);
  w.register_serialize_typed_function<Person>();
  if(!value) {
    w.write_u32(0);
    return os.str();
  }
  w.mark_written_pointer(value);
  std::size_t size_offset = w.position();
  w.write_u32(0);
  std::size_t frame_start = w.position();
  serialization::serialize(w, *value);
  std::uint32_t real_size = static_cast<std::uint32_t>(w.position() - frame_start);
  w.poke_u32(size_offset, real_size + 1);
  w.write_rest();
  w.write_u32(0);
  return os.str();
}

inline std::shared_ptr<Person> decode_flat_shared(const std::string& bytes) {
  std::istringstream is(bytes);
  serialization::flat_stream_reader r(is);
  r.register_deserialize_typed_function<Person>();
  std::uint32_t root_encoded_size = 0;
  r.read_u32(root_encoded_size);
  if(root_encoded_size == 0) {
    return nullptr;
  }
  auto out = std::make_shared<Person>();
  r.mark_read_pointer(out);
  serialization::deserialize(r, *out);
  r.read_rest();
  std::uint32_t sentinel = 1;
  r.read_u32(sentinel);
  if(sentinel != 0) {
    throw std::runtime_error("flat shared decode: missing sentinel");
  }
  return out;
}

} // namespace person_fixture
