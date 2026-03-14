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

  // Returns serialized bytes for uvarint (same encoding as circular_buffer_writer).
  std::vector<std::uint8_t> uvarint_to_bytes(std::uint64_t value) {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    circular_buffer_reserve_for_writing(cb, 32);
    size_t len = circular_buffer_poke_uvarint(cb, 0, value);
    circular_buffer_commit_write(cb, len);
    std::vector<std::uint8_t> bytes(len);
    circular_buffer_peek(cb, 0, bytes.data(), len);
    circular_buffer_destroy(cb);
    return bytes;
  }

  // Returns serialized bytes for svarint (same encoding as circular_buffer_writer).
  std::vector<std::uint8_t> svarint_to_bytes(std::int64_t value) {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    circular_buffer_reserve_for_writing(cb, 32);
    size_t len = circular_buffer_poke_svarint(cb, 0, value);
    circular_buffer_commit_write(cb, len);
    std::vector<std::uint8_t> bytes(len);
    circular_buffer_peek(cb, 0, bytes.data(), len);
    circular_buffer_destroy(cb);
    return bytes;
  }

  // Returns serialized bytes for float64 (same encoding as circular_buffer_writer).
  std::vector<std::uint8_t> float64_to_bytes(double value) {
    struct circular_buffer* cb = circular_buffer_create(16, nullptr);
    circular_buffer_reserve_for_writing(cb, 16);
    circular_buffer_writer w(cb, 0);
    w.write_f64(value);
    circular_buffer_commit_write(cb, 8);
    std::vector<std::uint8_t> bytes(8);
    circular_buffer_peek(cb, 0, bytes.data(), 8);
    circular_buffer_destroy(cb);
    return bytes;
  }

  template <typename T>
  int three_way_cmp(const T& a, const T& b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
  }

  int bytes_three_way_cmp(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    size_t n = (a.size() < b.size()) ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
      if (a[i] != b[i]) return (a[i] < b[i]) ? -1 : 1;
    }
    if (a.size() < b.size()) return -1;
    if (a.size() > b.size()) return 1;
    return 0;
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

  // Sort order is preserved: binary comparison of serialized form matches numeric comparison.
  // Critical for DB keys serialized with the same format.
  "cb serialization preserves sort order uvarint svarint float64"_test = [] {
    // --- uvarint: many values across byte-length boundaries ---
    std::vector<std::uint64_t> uvals = {
      0, 1, 2, 3, 126, 127, 128, 129, 255, 256,
      0x3FFE, 0x3FFF, 0x4000, 0x4001,
      0x1FFFFE, 0x1FFFFF, 0x200000, 0x200001,
      0x10000000 - 1, 0x10000000, 0x10000001,
      100, 1000, 1000000, 1000000000ULL,
      0xFFFFFFFFULL, 0x100000000ULL, 0x100000001ULL,
      0xFFFFFFFFFFFFFFFFULL
    };
    for (size_t i = 0; i < uvals.size(); ++i) {
      for (size_t j = 0; j < uvals.size(); ++j) {
        auto a = uvals[i], b = uvals[j];
        auto ba = uvarint_to_bytes(a), bb = uvarint_to_bytes(b);
        expect(three_way_cmp(a, b) == bytes_three_way_cmp(ba, bb));
      }
    }

    // --- svarint: positive, negative, boundaries ---
    std::vector<std::int64_t> svals = {
      0, 1, -1, 2, -2, 62, 63, -63, -64, 64, 65,
      127, -127, 128, -128, 129, -129,
      0x1FFF, 0x2000, -0x2000, -0x2001,
      0x7FFFFFFF, -0x80000000LL,
      1000000, -1000000,
      0x7FFFFFFFFFFFFFFFLL, std::numeric_limits<std::int64_t>::min()
    };
    for (size_t i = 0; i < svals.size(); ++i) {
      for (size_t j = 0; j < svals.size(); ++j) {
        auto a = svals[i], b = svals[j];
        auto ba = svarint_to_bytes(a), bb = svarint_to_bytes(b);
        expect(three_way_cmp(a, b) == bytes_three_way_cmp(ba, bb));
      }
    }

    // --- float64: exclude NaN so numeric comparison is well-defined ---
    std::vector<double> fvals = {
      -std::numeric_limits<double>::infinity(),
      -1e308, -1e100, -1e10, -1000.0, -1.5, -1.0, -0.5, -1e-10, -1e-308,
      -0.0, 0.0,
      1e-308, 1e-10, 0.5, 1.0, 1.5, 1000.0, 1e10, 1e100, 1e308,
      std::numeric_limits<double>::infinity()
    };
    for (size_t i = 0; i < fvals.size(); ++i) {
      for (size_t j = 0; j < fvals.size(); ++j) {
        auto a = fvals[i], b = fvals[j];
        auto ba = float64_to_bytes(a), bb = float64_to_bytes(b);
        // Print the input values and their serialized bytes in hex for debugging
        std::ostringstream oss;        
/*         oss << "a=" << a << " b=" << b << " | ba=0x";                
        for (std::uint8_t byte : ba) oss << std::hex << std::uppercase << (int)byte;
        oss << " bb=0x";
        for (std::uint8_t byte : bb) oss << std::hex << std::uppercase << (int)byte;
        uint64_t a_uint64 = *reinterpret_cast<uint64_t*>(&a);
        uint64_t b_uint64 = *reinterpret_cast<uint64_t*>(&b);        
        oss << std::endl << "a_uint64=0x" << std::hex << std::uppercase << a_uint64 << " b_uint64=0x" << std::hex << std::uppercase << b_uint64 ; */
        std::cout << oss.str() << std::endl;

        expect(three_way_cmp(a, b) == bytes_three_way_cmp(ba, bb));
      }
    }

    // NaN: two NaNs must serialize to the same bytes (for consistency in keys).
    double nan = std::numeric_limits<double>::quiet_NaN();
    expect(float64_to_bytes(nan) == float64_to_bytes(nan));
  };
}
