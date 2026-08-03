#include <boost/ut.hpp>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "serialization/key_stream_writer.hpp"
#include "serialization/key_stream_reader.hpp"
#include "serialization/key_codec.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"
#include "serialization/stream_writer.hpp"

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
  };

  template <typename T>
  std::vector<std::uint8_t> key_bytes(const T& value) {
    key_encoder enc;
    serialize(enc, value);
    return enc.finish();
  }

  template <typename T>
  T key_roundtrip(const T& value) {
    auto blob = key_bytes(value);
    key_decoder dec(blob);
    T out{};
    deserialize(dec, out);
    return out;
  }

  template <typename T>
  T key_stream_roundtrip(const T& value) {
    std::ostringstream os;
    key_stream_writer w(os);
    serialize(w, value);
    w.finish();
    std::istringstream is(os.str());
    key_stream_reader r(is);
    T out{};
    deserialize(r, out);
    return out;
  }

  int memcmp_order(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    int c = std::memcmp(a.data(), b.data(), n);
    if(c != 0) return c;
    if(a.size() < b.size()) return -1;
    if(a.size() > b.size()) return 1;
    return 0;
  }

}

int main() {
  "key roundtrip scalars"_test = [] {
    expect(eq(key_roundtrip(std::uint64_t{42}), std::uint64_t{42}));
    expect(eq(key_roundtrip(std::int64_t{-7}), std::int64_t{-7}));
    expect(eq(key_roundtrip(std::uint32_t{1000}), std::uint32_t{1000}));
    expect(eq(key_roundtrip(std::int32_t{-1000}), std::int32_t{-1000}));
    expect(eq(key_roundtrip(std::uint16_t{40000}), std::uint16_t{40000}));
    expect(eq(key_roundtrip(std::int16_t{-20000}), std::int16_t{-20000}));
    expect(eq(key_roundtrip(std::uint8_t{255}), std::uint8_t{255}));
    expect(eq(key_roundtrip(std::int8_t{-128}), std::int8_t{-128}));
    expect(eq(key_roundtrip(true), true));
    expect(eq(key_roundtrip(false), false));
    expect(eq(key_roundtrip(3.5f), 3.5f));
    expect(eq(key_roundtrip(2.25), 2.25));
  };

  "key roundtrip string binary"_test = [] {
    expect(eq(key_roundtrip(std::string{}), std::string{}));
    expect(eq(key_roundtrip(std::string{"hello"}), std::string{"hello"}));
    std::vector<std::uint8_t> bin{1, 2, 3, 4};
    expect(static_cast<bool>(key_roundtrip(bin) == bin));
  };

  "key roundtrip vector and udt"_test = [] {
    std::vector<std::int32_t> v{1, 2, 3};
    expect(static_cast<bool>(key_roundtrip(v) == v));
    std::vector<std::string> vs{"a", "bb", ""};
    expect(static_cast<bool>(key_roundtrip(vs) == vs));

    Person p;
    p.name = "Ada";
    p.age = 36;
    p.skill = Skill{"math", 10};
    p.scores = {1, 2};
    p.tags = {"x", "yz"};
    auto out = key_roundtrip(p);
    expect(eq(out.name, p.name));
    expect(eq(out.age, p.age));
    expect(eq(out.skill.name, p.skill.name));
    expect(eq(out.skill.level, p.skill.level));
    expect(static_cast<bool>(out.scores == p.scores));
    expect(static_cast<bool>(out.tags == p.tags));
  };

  "key unique_ptr roundtrip"_test = [] {
    auto p = std::make_unique<std::string>("owned");
    key_encoder enc;
    serialize(enc, p);
    auto blob = enc.finish();
    key_decoder dec(blob);
    std::unique_ptr<std::string> out = std::make_unique<std::string>();
    deserialize(dec, out);
    expect(eq(*out, std::string{"owned"}));
  };

  "key stream roundtrip"_test = [] {
    expect(eq(key_stream_roundtrip(std::string{"stream"}), std::string{"stream"}));
    Person p;
    p.name = "Bob";
    p.age = 1;
    auto out = key_stream_roundtrip(p);
    expect(eq(out.name, std::string{"Bob"}));
    expect(eq(out.age, 1));
  };

  "key sort length does not disturb data order"_test = [] {
    // With length-prefix-before-string, short "b" can sort before longer "aa" wrongly vs data-first.
    // Key format: data is string bytes only; lengths in meta at end.
    struct Rec {
      std::string a;
      std::string b;
    };
    Rec left{"aa", "z"};
    Rec right{"b", "aaaaaaaa"};
    auto bl = key_bytes(left);
    auto br = key_bytes(right);
    // data starts with 'a' vs 'b' → left < right in memcmp
    expect(memcmp_order(bl, br) < 0);

    // Compressed length-prefix would compare len(2) vs len(1) first → opposite for first field alone
    std::ostringstream cos_l;
    stream_writer cw_l(cos_l);
    serialize(cw_l, left.a);
    std::ostringstream cos_r;
    stream_writer cw_r(cos_r);
    serialize(cw_r, right.a);
    auto cl = cos_l.str();
    auto cr = cos_r.str();
    int compressed_cmp = std::memcmp(cl.data(), cr.data(),
      cl.size() < cr.size() ? cl.size() : cr.size());
    if(compressed_cmp == 0) {
      compressed_cmp = cl.size() < cr.size() ? -1 : (cl.size() > cr.size() ? 1 : 0);
    }
    // Document that compressed first-string order differs from key data-first for this pair
    expect(compressed_cmp > 0); // "b"(len1) before "aa"(len2) in compressed
    expect(memcmp_order(key_bytes(left.a), key_bytes(right.a)) < 0); // data-first: aa < b
  };

  "key sort tie on equal data different lengths"_test = [] {
    std::vector<std::string> a{"a", "bc"};
    std::vector<std::string> b{"ab", "c"};
    auto ba = key_bytes(a);
    auto bb = key_bytes(b);
    // Same concatenated data "abc", different meta → blobs differ
    expect(static_cast<bool>(ba != bb));
    // Deterministic order
    int o = memcmp_order(ba, bb);
    expect(o != 0);
    expect(eq(memcmp_order(ba, bb), o));
  };

  "key unsupported map and shared_ptr throw"_test = [] {
    expect(throws<std::runtime_error>([] {
      std::map<std::string, std::int32_t> m{{"a", 1}};
      key_encoder enc;
      serialize(enc, m);
    }));
    expect(throws<std::runtime_error>([] {
      auto p = std::make_shared<Person>();
      key_encoder enc;
      serialize(enc, p);
    }));
  };

  "key empty meta still has trailing size"_test = [] {
    auto blob = key_bytes(std::int32_t{5});
    expect(blob.size() >= 1);
    expect(eq(blob.back(), std::uint8_t{0}));
  };

  "key meta overflow throws"_test = [] {
    expect(throws<std::runtime_error>([] {
      key_encoder enc;
      for(int i = 0; i < 256; ++i) {
        enc.write_length(0);
      }
      enc.finish();
    }));
  };
}
