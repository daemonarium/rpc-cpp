#include <boost/ut.hpp>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

#include "serialization/flat_stream_writer.hpp"
#include "serialization/flat_stream_reader.hpp"
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
  std::string encode_flat(const T& value) {
    std::ostringstream os;
    flat_stream_writer w(os);
    serialize(w, value);
    w.write_rest();
    return os.str();
  }

  template <typename T>
  std::string flat_bytes_shared(const std::shared_ptr<T>& value) {
    std::ostringstream os;
    flat_stream_writer w(os);
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

  // Deserialize a shared_ptr graph encoded by flat_bytes_shared (new wire
  // format: [encoded_size_0][root][encoded_size_i][sub-i]...[0 sentinel]).
  template <typename T>
  std::shared_ptr<T> decode_circular_shared(struct circular_buffer* cb, size_t len) {
    circular_flat_source src(cb);
    flat_buffer_reader<circular_flat_source> r(src);
    std::uint32_t root_encoded_size = 0;
    r.read_u32(root_encoded_size); // consume root prefix (or sentinel if null)
    if(root_encoded_size == 0) {
      return nullptr; // null root
    }
    auto out = std::make_shared<T>();
    r.mark_read_pointer(out); // root: back-refs (delta=-1) resolve to it
    deserialize(r, *out); // deref
    r.read_rest(); // sub-frames
    std::uint32_t sentinel = 1;
    r.read_u32(sentinel);
    expect(eq(sentinel, 0u));
    return out;
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

  // Place message so exactly `before_wrap` readable bytes sit before the physical seam.
  struct circular_buffer* place_bytes_seam(const std::string& bytes, size_t before_wrap) {
    size_t need = bytes.size() + 64;
    if(need < 64) need = 64;
    struct circular_buffer* cb = circular_buffer_create(need, nullptr);
    if(before_wrap == 0) before_wrap = 1;
    if(before_wrap >= bytes.size()) before_wrap = bytes.size() - 1;
    size_t usable = circular_buffer_writable_bytes(cb) + 1;
    size_t pad = usable - before_wrap;
    force_read_near_end(cb, pad);
    size_t w = circular_buffer_write(cb, bytes.data(), bytes.size());
    if(w != bytes.size()) {
      circular_buffer_destroy(cb);
      return nullptr;
    }
    return cb;
  }

  struct circular_buffer* place_bytes(const std::string& bytes, bool wrap) {
    if(!wrap) {
      size_t need = bytes.size() + 8;
      if(need < 64) need = 64;
      struct circular_buffer* cb = circular_buffer_create(need, nullptr);
      size_t w = circular_buffer_write(cb, bytes.data(), bytes.size());
      if(w != bytes.size()) {
        circular_buffer_destroy(cb);
        return nullptr;
      }
      return cb;
    }
    size_t before_wrap = bytes.size() / 2;
    if(before_wrap == 0) before_wrap = 1;
    if(before_wrap >= bytes.size()) before_wrap = bytes.size() - 1;
    return place_bytes_seam(bytes, before_wrap);
  }

  bool ptr_in_ring(struct circular_buffer* cb, const void* p) {
    if(!cb || !p || !cb->data) return false;
    const char* base = cb->data->data;
    // data[] length == data->size - sizeof(size_t)*3 (read/write/size header fields).
    const size_t data_len = cb->data->size >= 3 * sizeof(size_t)
      ? cb->data->size - 3 * sizeof(size_t)
      : 0;
    const char* c = static_cast<const char*>(p);
    return c >= base && c < base + data_len;
  }

  std::vector<std::uint8_t> concat_segments(struct circular_buffer_segment segs[2], int n) {
    std::vector<std::uint8_t> out;
    for(int i = 0; i < n; ++i) {
      out.insert(out.end(),
        reinterpret_cast<const std::uint8_t*>(segs[i].ptr),
        reinterpret_cast<const std::uint8_t*>(segs[i].ptr) + segs[i].len);
    }
    return out;
  }

  template <typename T>
  T roundtrip_circular_reader(const T& value, bool wrap) {
    std::string bytes = encode_flat(value);
    struct circular_buffer* cb = place_bytes(bytes, wrap);
    expect(cb != nullptr);
    circular_flat_source src(cb);
    flat_buffer_reader<circular_flat_source> r(src);
    T out{};
    deserialize(r, out);
    r.read_rest();
    circular_buffer_destroy(cb);
    return out;
  }

  void expect_person_view_fields(const flat_view<Person>& view) {
    expect(eq(std::string(view.get<^^Person::name>()), std::string{"Ada"}));
    expect(eq(view.get<^^Person::age>(), 36));
    auto skill = view.get<^^Person::skill>();
    expect(eq(std::string(skill.get<^^Skill::name>()), std::string{"math"}));
    expect(eq(skill.get<^^Skill::level>(), 10));
    auto scores = view.get<^^Person::scores>();
    expect(eq(scores.size(), std::size_t{2}));
    expect(eq(scores[0], 1));
    expect(eq(scores[1], 2));
    auto tags = view.get<^^Person::tags>();
    expect(eq(tags.size(), std::size_t{2}));
    expect(eq(std::string(tags[0]), std::string{"a"}));
    expect(eq(std::string(tags[1]), std::string{"b"}));
    expect(eq(view.get<^^Person::attrs>().find(std::string{"hp"}), 100));
  }

}

int main() {
  static_assert(flat_source_is_contiguous_v<span_flat_source>);
  static_assert(!flat_source_is_contiguous_v<circular_flat_source>);

  "circular flat_source peek"_test = [] {
    std::string bytes = encode_flat(std::int32_t{42});
    struct circular_buffer* cb = place_bytes(bytes, true);
    expect(cb != nullptr);
    circular_flat_source src(cb);
    expect(eq(src.size(), bytes.size()));
    std::vector<std::uint8_t> got(bytes.size());
    expect(eq(src.peek(0, got.data(), got.size()), got.size()));
    expect(static_cast<bool>(std::memcmp(got.data(), bytes.data(), bytes.size()) == 0));
    circular_buffer_destroy(cb);
  };

  "circular flat_source segments try_ptr null"_test = [] {
    {
      circular_flat_source empty;
      expect(eq(empty.size(), std::size_t{0}));
      std::uint8_t b = 0;
      expect(eq(empty.peek(0, &b, 1), std::size_t{0}));
      expect(empty.try_ptr(0, 1) == nullptr);
      struct circular_buffer_segment segs[2];
      expect(eq(empty.get_segments(0, 1, segs), 0));
      expect(empty.region_contiguous(0, 0) == false);
    }

    std::string bytes = encode_flat(std::string{"hello-world!!"});
    {
      struct circular_buffer* cb = place_bytes(bytes, false);
      circular_flat_source src(cb);
      expect(src.region_contiguous(0, bytes.size()));
      expect(src.try_ptr(0, bytes.size()) != nullptr);
      struct circular_buffer_segment segs[2];
      expect(eq(src.get_segments(0, bytes.size(), segs), 1));
      expect(eq(segs[0].len, bytes.size()));
      std::vector<std::uint8_t> peeked(bytes.size());
      src.peek(0, peeked.data(), peeked.size());
      expect(static_cast<bool>(concat_segments(segs, 1) == peeked));
      circular_buffer_destroy(cb);
    }
    {
      struct circular_buffer* cb = place_bytes(bytes, true);
      expect(circular_buffer_region_contiguous(cb, 0, bytes.size()) == 0);
      circular_flat_source src(cb);
      expect(!src.region_contiguous(0, bytes.size()));
      expect(src.try_ptr(0, bytes.size()) == nullptr);
      struct circular_buffer_segment segs[2];
      expect(eq(src.get_segments(0, bytes.size(), segs), 2));
      std::vector<std::uint8_t> peeked(bytes.size());
      src.peek(0, peeked.data(), peeked.size());
      expect(static_cast<bool>(concat_segments(segs, 2) == peeked));
      circular_buffer_destroy(cb);
    }
  };

  "circular reader contig and wrap scalars"_test = [] {
    expect(eq(roundtrip_circular_reader(std::int32_t{-99}, false), std::int32_t{-99}));
    expect(eq(roundtrip_circular_reader(std::int32_t{-99}, true), std::int32_t{-99}));
    expect(eq(roundtrip_circular_reader(std::string{"hello"}, true), std::string{"hello"}));
    expect(eq(roundtrip_circular_reader(std::string{""}, true), std::string{""}));
  };

  "circular reader wrap vector and map"_test = [] {
    std::vector<std::int32_t> v{1, 2, 3, 4};
    expect(static_cast<bool>(roundtrip_circular_reader(v, true) == v));
    std::vector<std::string> vs{"a", "bb", "ccc"};
    expect(static_cast<bool>(roundtrip_circular_reader(vs, true) == vs));
    std::map<std::string, std::int32_t> m{{"x", 1}, {"y", 2}};
    expect(static_cast<bool>(roundtrip_circular_reader(m, true) == m));
  };

  "circular reader seam sweep small message"_test = [] {
    std::string msg = encode_flat(std::string{"abcdefghijklmnop"});
    for(size_t seam = 0; seam < msg.size(); ++seam) {
      size_t cap = 64;
      struct circular_buffer* cb = circular_buffer_create(cap, nullptr);
      size_t pad = (cap > msg.size() + 2) ? (cap - msg.size() - 2 + (seam % 7)) : 8;
      force_read_near_end(cb, pad % (cap / 2 + 1));
      if(circular_buffer_writable_bytes(cb) < msg.size()) {
        circular_buffer_destroy(cb);
        continue;
      }
      circular_buffer_write(cb, msg.data(), msg.size());
      circular_flat_source src(cb);
      flat_buffer_reader<circular_flat_source> r(src);
      std::string out;
      deserialize(r, out);
      expect(eq(out, std::string{"abcdefghijklmnop"}));
      circular_buffer_destroy(cb);
    }
  };

  "circular flat_root_circular contig and wrap"_test = [] {
    Person p;
    p.name = "Ada";
    p.age = 36;
    p.skill = Skill{"math", 10};
    p.scores = {1, 2};
    p.tags = {"a", "b"};
    p.attrs = {{"hp", 100}};
    std::string bytes = encode_flat(p);

    {
      struct circular_buffer* cb = place_bytes(bytes, false);
      expect(circular_buffer_region_contiguous(cb, 0, bytes.size()) != 0);
      auto view = flat_root_circular<Person>(cb, bytes.size());
      expect(view.session()->storage() == nullptr);
      expect(view.session()->is_circular());
      expect_person_view_fields(view);
      flat_string name = view.get<^^Person::name>();
      expect(!name.owns());
      expect(ptr_in_ring(cb, name.data()));
      const char* ring = nullptr;
      expect(eq(circular_buffer_try_contiguous_ptr(cb, 0, bytes.size(), &ring), bytes.size()));
      expect(name.data() >= ring && name.data() < ring + bytes.size());
      circular_buffer_destroy(cb);
    }
    {
      struct circular_buffer* cb = place_bytes(bytes, true);
      expect(circular_buffer_region_contiguous(cb, 0, bytes.size()) == 0);
      auto view = flat_root_circular<Person>(cb, bytes.size());
      // No full-message materialize: circular session, no owned vector storage.
      expect(view.session()->storage() == nullptr);
      expect(view.session()->is_circular());
      expect_person_view_fields(view);
      flat_string name = view.get<"name">();
      expect(eq(std::string(name), std::string{"Ada"}));
      if(name.owns()) {
        expect(!ptr_in_ring(cb, name.data()));
      } else {
        expect(ptr_in_ring(cb, name.data()));
      }
      circular_buffer_destroy(cb);
    }
  };

  "circular flat_root name field crosses seam owns"_test = [] {
    Person p;
    p.name = "ABCDEFGHIJKLMNOP";
    p.age = 1;
    std::string bytes = encode_flat(p);
    // Place seam inside the name payload (after u32 len prefix of the name field).
    // Person layout starts with string name: find a before_wrap that splits name bytes.
    bool saw_own = false;
    for(size_t before = 1; before < bytes.size(); ++before) {
      struct circular_buffer* cb = place_bytes_seam(bytes, before);
      expect(cb != nullptr);
      auto view = flat_root_circular<Person>(cb, bytes.size());
      expect(view.session()->storage() == nullptr);
      flat_string name = view.get<^^Person::name>();
      expect(eq(std::string(name), std::string{"ABCDEFGHIJKLMNOP"}));
      if(name.owns()) {
        saw_own = true;
        expect(!ptr_in_ring(cb, name.data()));
      }
      circular_buffer_destroy(cb);
    }
    expect(saw_own);
  };

  "circular flat_root_circular map find"_test = [] {
    Person p;
    p.name = "M";
    p.attrs = {{"aa", 2}, {"b", 1}};
    std::string bytes = encode_flat(p);
    for(bool wrap : {false, true}) {
      struct circular_buffer* cb = place_bytes(bytes, wrap);
      auto view = flat_root_circular<Person>(cb, bytes.size());
      auto attrs = view.get<^^Person::attrs>();
      expect(eq(attrs.find(std::string{"b"}), 1));
      expect(eq(attrs.find(std::string{"aa"}), 2));
      circular_buffer_destroy(cb);
    }
  };

  "circular shared_ptr reader graph"_test = [] {
    auto a = std::make_shared<Person>();
    a->name = "A";
    a->age = 1;
    auto b = std::make_shared<Person>();
    b->name = "B";
    b->age = 2;
    a->friends.push_back(b);
    a->friends.push_back(nullptr);
    b->friends.push_back(a);

    std::string bytes = flat_bytes_shared(a);
    for(bool wrap : {false, true}) {
      struct circular_buffer* cb = place_bytes(bytes, wrap);
      expect(cb != nullptr);
      auto out = decode_circular_shared<Person>(cb, bytes.size());
      expect(out != nullptr);
      expect(eq(out->name, std::string{"A"}));
      expect(eq(out->friends.size(), std::size_t{2}));
      expect(out->friends[0] != nullptr);
      expect(eq(out->friends[0]->name, std::string{"B"}));
      expect(out->friends[1] == nullptr);
      expect(out->friends[0]->friends.size() == 1_u);
      expect(out->friends[0]->friends[0].get() == out.get());
      circular_buffer_destroy(cb);
    }
  };

  "circular flat_root_shared_circular graph"_test = [] {
    auto a = std::make_shared<Person>();
    a->name = "Root";
    a->age = 10;
    auto b = std::make_shared<Person>();
    b->name = "Friend";
    b->age = 11;
    a->friends.push_back(b);
    a->friends.push_back(a);

    std::string bytes = flat_bytes_shared(a);
    for(bool wrap : {false, true}) {
      struct circular_buffer* cb = place_bytes(bytes, wrap);
      expect(eq(circular_buffer_region_contiguous(cb, 0, bytes.size()) != 0, !wrap));
      auto root = flat_root_shared_circular<Person>(cb, bytes.size());
      expect(root != nullptr);
      expect(root->session()->storage() == nullptr);
      expect(root->session()->is_circular());
      expect(eq(std::string(root->get<^^Person::name>()), std::string{"Root"}));
      expect(eq(root->get<^^Person::age>(), 10));
      auto friends = root->get<^^Person::friends>();
      expect(eq(friends.size(), std::size_t{2}));
      expect(friends[0] != nullptr);
      expect(friends[1] != nullptr);
      expect(eq(std::string(friends[0]->get<^^Person::name>()), std::string{"Friend"}));
      expect(friends[1].get() == root.get());
      if(!wrap) {
        flat_string name = root->get<^^Person::name>();
        expect(!name.owns());
        expect(ptr_in_ring(cb, name.data()));
      }
      circular_buffer_destroy(cb);
    }
  };

  "circular flat_root_shared null and acyclic"_test = [] {
    {
      std::shared_ptr<Person> nil;
      std::string bytes = flat_bytes_shared(nil);
      struct circular_buffer* cb = place_bytes(bytes, false);
      auto root = flat_root_shared_circular<Person>(cb, bytes.size());
      expect(root == nullptr);
      circular_buffer_destroy(cb);
    }
    {
      auto a = std::make_shared<Person>();
      a->name = "A";
      auto b = std::make_shared<Person>();
      b->name = "B";
      a->friends.push_back(b);
      std::string bytes = flat_bytes_shared(a);
      for(bool wrap : {false, true}) {
        struct circular_buffer* cb = place_bytes(bytes, wrap);
        auto root = flat_root_shared_circular<Person>(cb, bytes.size());
        expect(root != nullptr);
        expect(eq(std::string(root->get<^^Person::name>()), std::string{"A"}));
        auto friends = root->get<^^Person::friends>();
        expect(eq(friends.size(), std::size_t{1}));
        expect(friends[0] != nullptr);
        expect(eq(std::string(friends[0]->get<^^Person::name>()), std::string{"B"}));
        expect(friends[0]->get<^^Person::friends>().size() == 0_u);
        circular_buffer_destroy(cb);
      }
    }
  };

  "circular shared_ptr reader wrap seam sweep"_test = [] {
    auto a = std::make_shared<Person>();
    a->name = "A";
    auto b = std::make_shared<Person>();
    b->name = "B";
    a->friends.push_back(b);
    b->friends.push_back(a);
    std::string msg = flat_bytes_shared(a);
    for(size_t pad = 1; pad < 40; ++pad) {
      size_t cap = msg.size() + 48;
      struct circular_buffer* cb = circular_buffer_create(cap, nullptr);
      force_read_near_end(cb, pad % (circular_buffer_writable_bytes(cb) / 2 + 1));
      if(circular_buffer_writable_bytes(cb) < msg.size()) {
        circular_buffer_destroy(cb);
        continue;
      }
      circular_buffer_write(cb, msg.data(), msg.size());
      auto out = decode_circular_shared<Person>(cb, msg.size());
      expect(out != nullptr);
      expect(eq(out->name, std::string{"A"}));
      expect(out->friends[0]->friends[0].get() == out.get());
      circular_buffer_destroy(cb);
    }
  };

  "circular flat_root_shared_circular seam sweep"_test = [] {
    auto a = std::make_shared<Person>();
    a->name = "A";
    auto b = std::make_shared<Person>();
    b->name = "B";
    a->friends.push_back(b);
    b->friends.push_back(a);
    std::string msg = flat_bytes_shared(a);
    for(size_t before = 1; before < msg.size(); before += 3) {
      struct circular_buffer* cb = place_bytes_seam(msg, before);
      expect(cb != nullptr);
      expect(circular_buffer_region_contiguous(cb, 0, msg.size()) == 0);
      auto root = flat_root_shared_circular<Person>(cb, msg.size());
      expect(root != nullptr);
      expect(eq(std::string(root->get<^^Person::name>()), std::string{"A"}));
      auto friends = root->get<^^Person::friends>();
      expect(eq(friends.size(), std::size_t{1}));
      expect(friends[0] != nullptr);
      expect(eq(std::string(friends[0]->get<^^Person::name>()), std::string{"B"}));
      auto back = friends[0]->get<^^Person::friends>();
      expect(eq(back.size(), std::size_t{1}));
      expect(back[0].get() == root.get());
      circular_buffer_destroy(cb);
    }
  };

  "circular deserialize parity"_test = [] {
    Person p;
    p.name = "X";
    p.age = 1;
    p.skill = Skill{"s", 2};
    p.scores = {3};
    p.tags = {"t"};
    std::string bytes = encode_flat(p);
    auto span_src = span_flat_source(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    auto measure = [](auto& src, std::size_t len) {
      flat_buffer_reader<std::decay_t<decltype(src)>> reader(src, 0, len);
      reader.set_skip_pointers(true);
      Person tmp{};
      deserialize(reader, tmp);
      return reader.cursor();
    };
    std::size_t a = measure(span_src, bytes.size());

    {
      struct circular_buffer* cb = place_bytes(bytes, false);
      circular_flat_source csrc(cb);
      std::size_t b = measure(csrc, bytes.size());
      expect(eq(a, b));
      expect(eq(a, bytes.size()));
      circular_buffer_destroy(cb);
    }
    {
      struct circular_buffer* cb = place_bytes(bytes, true);
      expect(circular_buffer_region_contiguous(cb, 0, bytes.size()) == 0);
      circular_flat_source csrc(cb);
      std::size_t b = measure(csrc, bytes.size());
      expect(eq(a, b));
      auto mat = flat_peek_range(cb, 0, bytes.size());
      auto mat_src = span_flat_source(std::span<const std::uint8_t>(mat.data(), mat.size()));
      expect(eq(a, measure(mat_src, mat.size())));
      circular_buffer_destroy(cb);
    }
  };

  "circular factory error paths"_test = [] {
    Person p;
    p.name = "e";
    std::string bytes = encode_flat(p);
    struct circular_buffer* cb = place_bytes(bytes, false);
    expect(throws<std::runtime_error>([] {
      flat_root_circular<Person>(nullptr, 4);
    }));
    expect(throws<std::runtime_error>([&] {
      flat_root_circular<Person>(cb, bytes.size() + 1);
    }));
    expect(throws<std::runtime_error>([] {
      flat_root_shared_circular<Person>(nullptr, 4);
    }));
    expect(throws<std::runtime_error>([&] {
      flat_root_shared_circular<Person>(cb, bytes.size() + 1);
    }));
    circular_buffer_destroy(cb);
  };

  "circular lifetime smoke"_test = [] {
    {
      std::string bytes = encode_flat(std::string{"ok"});
      struct circular_buffer* cb = place_bytes(bytes, false);
      {
        circular_flat_source src(cb);
        flat_buffer_reader<circular_flat_source> r(src);
        std::string out;
        deserialize(r, out);
        expect(eq(out, std::string{"ok"}));
      }
      circular_buffer_skip_read(cb, bytes.size());
      expect(eq(circular_buffer_readable_bytes(cb), std::size_t{0}));
      circular_buffer_destroy(cb);
    }
    {
      Person p;
      p.name = "Z";
      p.age = 1;
      std::string bytes = encode_flat(p);
      struct circular_buffer* cb = place_bytes(bytes, false);
      {
        auto view = flat_root_circular<Person>(cb, bytes.size());
        expect(eq(std::string(view.get<^^Person::name>()), std::string{"Z"}));
      }
      circular_buffer_skip_read(cb, bytes.size());
      expect(eq(circular_buffer_readable_bytes(cb), std::size_t{0}));
      circular_buffer_destroy(cb);
    }
  };
}
