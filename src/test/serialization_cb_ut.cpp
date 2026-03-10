#include <boost/ut.hpp>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <limits>

extern "C" {
#include <io/circular_buffer.h>
}

#include "serialization/circular_buffer_writer.hpp"
#include "serialization/circular_buffer_reader.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"

using namespace serialization;

namespace {
  template <typename T>
  T roundtrip(const T& value, size_t buf_size = 8192) {
    struct circular_buffer* cb = circular_buffer_create(buf_size, nullptr);

    circular_buffer_writer w(cb, 0);
    serialize(w, value);
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    circular_buffer_reader r(cb, 0);
    T result{};
    deserialize(r, result);
    r.read_rest();

    circular_buffer_destroy(cb);
    return result;
  }
}

int main() {
  using namespace boost::ut;

  "cb serialize uint64 small values"_test = [] {
    expect(roundtrip<std::uint64_t>(0) == 0_u);
    expect(roundtrip<std::uint64_t>(1) == 1_u);
    expect(roundtrip<std::uint64_t>(42) == 42_u);
    expect(roundtrip<std::uint64_t>(127) == 127_u);
  };

  "cb serialize uint64 varint boundaries"_test = [] {
    expect(roundtrip<std::uint64_t>(128) == std::uint64_t(128));
    expect(roundtrip<std::uint64_t>(0x3FFF) == std::uint64_t(0x3FFF));
    expect(roundtrip<std::uint64_t>(0x4000) == std::uint64_t(0x4000));
    expect(roundtrip<std::uint64_t>(0x1FFFFF) == std::uint64_t(0x1FFFFF));
    expect(roundtrip<std::uint64_t>(0x200000) == std::uint64_t(0x200000));
  };

  "cb serialize uint64 large values"_test = [] {
    expect(roundtrip<std::uint64_t>(0xFFFFFFFF) == std::uint64_t(0xFFFFFFFF));
    expect(roundtrip<std::uint64_t>(0x100000000ULL) == std::uint64_t(0x100000000ULL));
    expect(roundtrip<std::uint64_t>(0xFFFFFFFFFFFFFFFFULL) == std::uint64_t(0xFFFFFFFFFFFFFFFFULL));
  };

  "cb serialize int64 roundtrip"_test = [] {
    expect(roundtrip<std::int64_t>(0) == 0_i);
    expect(roundtrip<std::int64_t>(1) == 1_i);
    expect(roundtrip<std::int64_t>(-1) == -1_i);
    expect(roundtrip<std::int64_t>(63) == 63_i);
    expect(roundtrip<std::int64_t>(-63) == -63_i);
    expect(roundtrip<std::int64_t>(64) == 64_i);
    expect(roundtrip<std::int64_t>(-64) == -64_i);
    expect(roundtrip<std::int64_t>(1000000) == 1000000_i);
    expect(roundtrip<std::int64_t>(-1000000) == -1000000_i);
  };

  // serialize(w, double) is ambiguous — test doubles via write_f64/read_f64 directly.
  "cb serialize double roundtrip"_test = [] {
    auto double_rt = [](double v) {
      struct circular_buffer* cb = circular_buffer_create(64, nullptr);
      circular_buffer_writer w(cb, 0);
      w.write_f64(v);
      circular_buffer_commit_write(cb, w.write_offset_);
      circular_buffer_reader r(cb, 0);
      double result;
      r.read_f64(result);
      circular_buffer_destroy(cb);
      return result;
    };

    expect(double_rt(0.0) == 0.0_d);
    expect(double_rt(1.5) == 1.5_d);
    expect(double_rt(-1.5) == -1.5_d);
    expect(double_rt(3.14159265358979) == 3.14159265358979_d);
    expect(double_rt(-std::numeric_limits<double>::infinity()) == -std::numeric_limits<double>::infinity());
    expect(double_rt(std::numeric_limits<double>::infinity()) == std::numeric_limits<double>::infinity());
    expect(std::isnan(double_rt(std::numeric_limits<double>::quiet_NaN())));
  };

  "cb serialize bool roundtrip"_test = [] {
    expect(roundtrip<bool>(true) == true);
    expect(roundtrip<bool>(false) == false);
  };

  "cb serialize string roundtrip"_test = [] {
    expect(roundtrip<std::string>("") == std::string(""));
    expect(roundtrip<std::string>("hello") == std::string("hello"));

    std::string long_str(200, 'x');
    expect(roundtrip<std::string>(long_str) == long_str);
  };

  "cb serialize binary roundtrip"_test = [] {
    std::vector<std::uint8_t> empty;
    expect(roundtrip<std::vector<std::uint8_t>>(empty) == empty);

    std::vector<std::uint8_t> data(4096);
    for(size_t i = 0; i < data.size(); ++i) {
      data[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    expect(roundtrip<std::vector<std::uint8_t>>(data) == data);
  };

  "cb serialize vector<uint64_t> roundtrip"_test = [] {
    std::vector<std::uint64_t> empty;
    expect(roundtrip<std::vector<std::uint64_t>>(empty) == empty);

    std::vector<std::uint64_t> v = {1, 2, 3, 100, 0xFFFFFFFFFFFFFFFFULL};
    expect(roundtrip<std::vector<std::uint64_t>>(v) == v);
  };

  "cb serialize nested vector roundtrip"_test = [] {
    std::vector<std::vector<std::int32_t>> v = {{1, 2, 3}, {}, {-1, 0, 1}};
    expect(roundtrip<std::vector<std::vector<std::int32_t>>>(v) == v);
  };

  "cb serialize map<string,uint64_t> roundtrip"_test = [] {
    std::map<std::string, std::uint64_t> empty;
    expect(roundtrip<std::map<std::string, std::uint64_t>>(empty) == empty);

    std::map<std::string, std::uint64_t> m = {{"a", 1}, {"bb", 200}, {"ccc", 0}};
    expect(roundtrip<std::map<std::string, std::uint64_t>>(m) == m);
  };

  "cb serialize multiple values in sequence"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(8192, nullptr);

    circular_buffer_writer w(cb, 0);
    serialize(w, std::uint64_t(42));
    serialize(w, std::string("hello"));
    serialize(w, true);
    serialize(w, std::int64_t(-7));
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    circular_buffer_reader r(cb, 0);
    std::uint64_t u; deserialize(r, u);
    std::string s; deserialize(r, s);
    bool b; deserialize(r, b);
    std::int64_t i; deserialize(r, i);
    r.read_rest();

    expect(u == 42_u);
    expect(s == std::string("hello"));
    expect(b == true);
    expect(i == -7_i);

    circular_buffer_destroy(cb);
  };

  "cb serialize with small buffer"_test = [] {
    // Use a small buffer to stress the circular buffer with large data
    std::vector<std::uint64_t> v;
    for(uint64_t i = 0; i < 100; ++i) v.push_back(i * 1000);

    // Buffer of 128 bytes, data is much larger — tests reserve/grow behavior
    auto result = roundtrip(v, 128);
    expect(result == v);
  };
}
