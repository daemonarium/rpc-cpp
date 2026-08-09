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
    // friends compared separately (shared graph)
  }

  Skill make_skill(Rng& rng, std::uint32_t salt) {
    Skill s;
    s.name = rng.next_bool() && (salt % 5 == 0) ? std::string{} : ("sk_" + rng.next_string(1, 10));
    s.level = rng.next_i32(-5, 100);
    return s;
  }

  // Value-only person (no friend graph) — full operator== roundtrip.
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
  std::string encode_flat(const T& value) {
    std::ostringstream os;
    flat_stream_writer w(os);
    serialize(w, value);
    w.write_rest();
    return os.str();
  }

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

  std::shared_ptr<Person> make_person_graph(std::uint64_t seed) {
    Rng rng(seed);
    const int n = rng.next_i32(2, 6);
    std::vector<std::shared_ptr<Person>> nodes;
    nodes.reserve(static_cast<std::size_t>(n));
    for(int i = 0; i < n; ++i) {
      auto node = std::make_shared<Person>();
      node->name = "g" + std::to_string(i) + "_" + rng.next_string(0, 8);
      node->age = rng.next_i32(0, 80);
      // Keep maps/vectors empty on graph nodes — stress topology + identity.
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

  std::string encode_flat_shared(const std::shared_ptr<Person>& value) {
    std::ostringstream os;
    flat_stream_writer w(os);
    if(!value) {
      w.write_u32(0); // sentinel only — null root
      return os.str();
    }
    // Pre-register the root so self-references use negative back-reference
    // deltas instead of creating duplicate sub-frames.
    w.mark_written_pointer(value);
    // Root frame with per-frame size prefix (encoded = real_size + 1).
    std::size_t size_offset = w.position();
    w.write_u32(0); // placeholder for encoded_size_0
    std::size_t frame_start = w.position();
    serialize(w, *value); // deref, no root delta
    std::uint32_t real_size = static_cast<std::uint32_t>(w.position() - frame_start);
    w.poke_u32(size_offset, real_size + 1);
    // Sub-frames: each wrapped with its own per-frame prefix by write_rest.
    w.write_rest();
    w.write_u32(0); // sentinel terminates the frame list
    return os.str();
  }

  // Deserialize a shared_ptr graph encoded by encode_flat_shared (new wire
  // format: [encoded_size_0][root][encoded_size_i][sub-i]...[0 sentinel]).
  std::shared_ptr<Person> decode_flat_shared(const std::string& bytes) {
    std::istringstream is(bytes);
    flat_stream_reader r(is);
    std::uint32_t root_encoded_size = 0;
    r.read_u32(root_encoded_size); // consume root prefix (or sentinel if null)
    if(root_encoded_size == 0) {
      return nullptr; // null root
    }
    auto out = std::make_shared<Person>();
    r.mark_read_pointer(out); // root: back-refs (delta=-1) resolve to it
    deserialize(r, *out); // deref
    r.read_rest(); // sub-frames (each prefix consumed inside)
    std::uint32_t sentinel = 1;
    r.read_u32(sentinel);
    expect(eq(sentinel, 0u));
    return out;
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
        // Canonical check: re-encode must match (topology + values preserved).
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
        // Pre-register root for back-references.
        cw.mark_written_pointer(root);
        // Root frame with per-frame size prefix (new wire format).
        std::size_t size_offset = cw.write_offset_;
        cw.write_u32(0); // placeholder for encoded_size_0
        std::size_t frame_start = cw.write_offset_;
        serialize(cw, *root); // deref, no root delta
        std::uint32_t real_size = static_cast<std::uint32_t>(cw.write_offset_ - frame_start);
        std::uint32_t encoded_size = real_size + 1;
        circular_buffer_poke(cb, size_offset, &encoded_size, sizeof(encoded_size));
        cw.write_rest(); // sub-frames with per-frame prefixes
        cw.write_u32(0); // sentinel
        circular_buffer_commit_write(cb, cw.write_offset_);

        circular_flat_source src(cb);
        flat_buffer_reader<circular_flat_source> r(src);
        std::uint32_t root_encoded_size = 0;
        r.read_u32(root_encoded_size); // consume root prefix
        auto out = std::make_shared<Person>();
        r.mark_read_pointer(out); // root: back-refs (delta=-1) resolve to it
        deserialize(r, *out); // deref
        r.read_rest(); // sub-frames
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