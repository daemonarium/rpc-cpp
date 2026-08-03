#include <boost/ut.hpp>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

#include "serialization/key_circular_buffer_writer.hpp"
#include "serialization/key_circular_buffer_reader.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"

using namespace boost::ut;
using namespace serialization;

namespace {

  struct Person {
    std::string name;
    std::int32_t age = 0;
    std::vector<std::string> tags;
  };

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

  struct circular_buffer* place_blob(const std::vector<std::uint8_t>& blob, bool wrap) {
    size_t need = blob.size() + (wrap ? 64 : 8);
    if(need < 64) need = 64;
    struct circular_buffer* cb = circular_buffer_create(need, nullptr);
    if(wrap) {
      size_t before_wrap = blob.size() / 2;
      if(before_wrap == 0) before_wrap = 1;
      if(before_wrap >= blob.size()) before_wrap = blob.size() - 1;
      size_t usable = circular_buffer_writable_bytes(cb) + 1;
      force_read_near_end(cb, usable - before_wrap);
    }
    size_t w = circular_buffer_write(cb, blob.data(), blob.size());
    if(w != blob.size()) {
      circular_buffer_destroy(cb);
      return nullptr;
    }
    return cb;
  }

  template <typename T>
  std::vector<std::uint8_t> encode_key(const T& value) {
    key_encoder enc;
    serialize(enc, value);
    return enc.finish();
  }

}

int main() {
  "key circular roundtrip contig"_test = [] {
    Person p;
    p.name = "Circ";
    p.age = 9;
    p.tags = {"a", "b"};

    struct circular_buffer* cb = circular_buffer_create(256, nullptr);
    key_circular_buffer_writer w(cb);
    serialize(w, p);
    w.finish();
    size_t len = circular_buffer_readable_bytes(cb);
    key_circular_buffer_reader r(cb, len);
    Person out{};
    deserialize(r, out);
    expect(eq(out.name, std::string{"Circ"}));
    expect(eq(out.age, 9));
    expect(eq(out.tags.size(), std::size_t{2}));
    circular_buffer_destroy(cb);
  };

  "key circular roundtrip wrap"_test = [] {
    Person p;
    p.name = "Wrap";
    p.age = 3;
    p.tags = {"long-tag-value", "x"};
    auto blob = encode_key(p);
    struct circular_buffer* cb = place_blob(blob, true);
    expect(cb != nullptr);
    expect(circular_buffer_region_contiguous(cb, 0, blob.size()) == 0);
    key_circular_buffer_reader r(cb, blob.size());
    Person out{};
    deserialize(r, out);
    expect(eq(out.name, std::string{"Wrap"}));
    expect(eq(out.age, 3));
    expect(static_cast<bool>(out.tags == p.tags));
    circular_buffer_destroy(cb);
  };

  "key circular seam smoke"_test = [] {
    std::vector<std::string> vs{"hello", "world"};
    auto blob = encode_key(vs);
    for(size_t before = 1; before < blob.size(); before += 5) {
      size_t need = blob.size() + 64;
      struct circular_buffer* cb = circular_buffer_create(need, nullptr);
      size_t usable = circular_buffer_writable_bytes(cb) + 1;
      size_t bw = before;
      if(bw >= blob.size()) bw = blob.size() - 1;
      force_read_near_end(cb, usable - bw);
      if(circular_buffer_writable_bytes(cb) < blob.size()) {
        circular_buffer_destroy(cb);
        continue;
      }
      circular_buffer_write(cb, blob.data(), blob.size());
      key_circular_buffer_reader r(cb, blob.size());
      std::vector<std::string> out;
      deserialize(r, out);
      expect(static_cast<bool>(out == vs));
      circular_buffer_destroy(cb);
    }
  };

  "key circular unsupported throw"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(64, nullptr);
    expect(throws<std::runtime_error>([&] {
      key_circular_buffer_writer w(cb);
      std::map<std::string, int> m{{"k", 1}};
      serialize(w, m);
    }));
    expect(throws<std::runtime_error>([&] {
      key_circular_buffer_writer w(cb);
      auto p = std::make_shared<Person>();
      serialize(w, p);
    }));
    circular_buffer_destroy(cb);
  };
}
