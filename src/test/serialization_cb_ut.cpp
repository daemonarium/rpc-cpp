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
    size_t len = circular_buffer_poke_uvarint64(cb, 0, value);
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
    size_t len = circular_buffer_poke_svarint64(cb, 0, value);
    circular_buffer_commit_write(cb, len);
    std::vector<std::uint8_t> bytes(len);
    circular_buffer_peek(cb, 0, bytes.data(), len);
    circular_buffer_destroy(cb);
    return bytes;
  }

  // Returns serialized bytes for uvarint32 (same encoding as uvarint64 for values in 32-bit range).
  std::vector<std::uint8_t> uvarint32_to_bytes(std::uint32_t value) {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    circular_buffer_reserve_for_writing(cb, 32);
    size_t len = circular_buffer_poke_uvarint32(cb, 0, value);
    circular_buffer_commit_write(cb, len);
    std::vector<std::uint8_t> bytes(len);
    circular_buffer_peek(cb, 0, bytes.data(), len);
    circular_buffer_destroy(cb);
    return bytes;
  }

  // Returns serialized bytes for svarint32 (same encoding as svarint64 for values in 32-bit range).
  std::vector<std::uint8_t> svarint32_to_bytes(std::int32_t value) {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    circular_buffer_reserve_for_writing(cb, 32);
    size_t len = circular_buffer_poke_svarint32(cb, 0, value);
    circular_buffer_commit_write(cb, len);
    std::vector<std::uint8_t> bytes(len);
    circular_buffer_peek(cb, 0, bytes.data(), len);
    circular_buffer_destroy(cb);
    return bytes;
  }

  // Returns serialized bytes for uvarint16 (same encoding as uvarint64 for values in 16-bit range).
  std::vector<std::uint8_t> uvarint16_to_bytes(std::uint16_t value) {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    circular_buffer_reserve_for_writing(cb, 32);
    size_t len = circular_buffer_poke_uvarint16(cb, 0, value);
    circular_buffer_commit_write(cb, len);
    std::vector<std::uint8_t> bytes(len);
    circular_buffer_peek(cb, 0, bytes.data(), len);
    circular_buffer_destroy(cb);
    return bytes;
  }

  // Returns serialized bytes for svarint16 (same encoding as svarint64 for values in 16-bit range).
  std::vector<std::uint8_t> svarint16_to_bytes(std::int16_t value) {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    circular_buffer_reserve_for_writing(cb, 32);
    size_t len = circular_buffer_poke_svarint16(cb, 0, value);
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

  // Returns serialized bytes for float32 (same encoding as circular_buffer_writer).
  std::vector<std::uint8_t> float32_to_bytes(float value) {
    struct circular_buffer* cb = circular_buffer_create(16, nullptr);
    circular_buffer_reserve_for_writing(cb, 16);
    circular_buffer_writer w(cb, 0);
    w.write_f32(value);
    circular_buffer_commit_write(cb, 4);
    std::vector<std::uint8_t> bytes(4);
    circular_buffer_peek(cb, 0, bytes.data(), 4);
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

  "cb serialize uint8 roundtrip"_test = [] {
    auto u8_rt = [](std::uint8_t v) {
      struct circular_buffer* cb = circular_buffer_create(64, nullptr);
      circular_buffer_writer w(cb, 0);
      w.write_u8(v);
      circular_buffer_commit_write(cb, w.write_offset_);
      circular_buffer_reader r(cb, 0);
      std::uint8_t result;
      r.read_u8(result);
      circular_buffer_destroy(cb);
      return result;
    };
    expect(u8_rt(0) == 0_u);
    expect(u8_rt(1) == 1_u);
    expect(u8_rt(127) == 127_u);
    expect(u8_rt(128) == 128_u);
    expect(u8_rt(255) == 255_u);
  };

  "cb serialize int8 roundtrip"_test = [] {
    auto i8_rt = [](std::int8_t v) {
      struct circular_buffer* cb = circular_buffer_create(64, nullptr);
      circular_buffer_writer w(cb, 0);
      w.write_i8(v);
      circular_buffer_commit_write(cb, w.write_offset_);
      circular_buffer_reader r(cb, 0);
      std::int8_t result;
      r.read_i8(result);
      circular_buffer_destroy(cb);
      return result;
    };
    expect(i8_rt(0) == 0_i);
    expect(i8_rt(1) == 1_i);
    expect(i8_rt(-1) == -1_i);
    expect(i8_rt(127) == 127_i);
    expect(i8_rt(-128) == -128_i);
  };

  "cb serialize float roundtrip"_test = [] {
    auto float_rt = [](float v) {
      struct circular_buffer* cb = circular_buffer_create(64, nullptr);
      circular_buffer_writer w(cb, 0);
      w.write_f32(v);
      circular_buffer_commit_write(cb, w.write_offset_);
      circular_buffer_reader r(cb, 0);
      float result;
      r.read_f32(result);
      circular_buffer_destroy(cb);
      return result;
    };
    expect(float_rt(0.0f) == 0.0f);
    expect(float_rt(1.5f) == 1.5f);
    expect(float_rt(-1.5f) == -1.5f);
    expect(float_rt(3.14159265f) == 3.14159265f);
    expect(float_rt(-std::numeric_limits<float>::infinity()) == -std::numeric_limits<float>::infinity());
    expect(float_rt(std::numeric_limits<float>::infinity()) == std::numeric_limits<float>::infinity());
    expect(std::isnan(float_rt(std::numeric_limits<float>::quiet_NaN())));
  };

  "cb serialization preserves sort order float32"_test = [] {
    std::vector<float> fvals = {
      -std::numeric_limits<float>::infinity(),
      -1e30f, -1e10f, -1000.0f, -1.5f, -1.0f, -0.5f, -1e-10f, -1e-30f,
      -0.0f, 0.0f,
      1e-30f, 1e-10f, 0.5f, 1.0f, 1.5f, 1000.0f, 1e10f, 1e30f,
      std::numeric_limits<float>::infinity()
    };
    for (size_t i = 0; i < fvals.size(); ++i) {
      for (size_t j = 0; j < fvals.size(); ++j) {
        auto a = fvals[i], b = fvals[j];
        auto ba = float32_to_bytes(a), bb = float32_to_bytes(b);
        expect(three_way_cmp(a, b) == bytes_three_way_cmp(ba, bb));
      }
    }
    float nan = std::numeric_limits<float>::quiet_NaN();
    expect(float32_to_bytes(nan) == float32_to_bytes(nan));
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

  "cb uvarint32 roundtrip"_test = [] {
    std::vector<std::uint32_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0x1FFFFF, 0x200000,
        0xFFFFFF, 0x10000000 - 1, 0x10000000, 0x7FFFFFFF, 0xFFFFFFFF};
    for (std::uint32_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t written = circular_buffer_write_uvarint32(cb, v);
      expect(written > 0_u);
      std::uint32_t out;
      size_t read = circular_buffer_read_uvarint32(cb, &out);
      expect(read == written);
      expect(out == v);
      circular_buffer_destroy(cb);
    }
  };

  "cb uvarint32 byte lengths"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    circular_buffer_reserve_for_writing(cb, 32);
    expect(circular_buffer_write_uvarint32(cb, 0) == 1_u);
    expect(circular_buffer_write_uvarint32(cb, 0xFFFFFFFF) == 5_u);
    std::uint32_t a, b;
    size_t n1 = circular_buffer_read_uvarint32(cb, &a);
    size_t n2 = circular_buffer_read_uvarint32(cb, &b);
    expect(n1 == 1_u && a == 0_u);
    expect(n2 == 5_u && b == 0xFFFFFFFF);
    circular_buffer_destroy(cb);
  };

  "cb svarint32 roundtrip"_test = [] {
    const std::int32_t min32 = std::numeric_limits<std::int32_t>::min();
    std::vector<std::int32_t> vals = {0, 1, -1, 63, -63, 64, -64, 127, -127, 128, -128,
        0x1FFF, -0x2000, 0x7FFFFFFF, min32};
    for (std::int32_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t written = circular_buffer_write_svarint32(cb, v);      
      expect(written > 0_u);
      std::int32_t out;
      size_t read = circular_buffer_read_svarint32(cb, &out);
      expect(read == written);
      expect(out == v);
      circular_buffer_destroy(cb);
    }
  };

  "cb write uvarint32 read as uvarint64 compatible"_test = [] {
    std::vector<std::uint32_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0x1FFFFF, 0xFFFFFFFF};
    for (std::uint32_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t w = circular_buffer_write_uvarint32(cb, v);
      expect(w > 0_u);
      std::uint64_t out64;
      size_t r = circular_buffer_read_uvarint64(cb, &out64);
      expect(r == w);
      expect(out64 == (std::uint64_t)v);
      circular_buffer_destroy(cb);
    }
  };

  "cb write svarint32 read as svarint64 compatible"_test = [] {
    std::vector<std::int32_t> vals = {0, 1, -1, 63, -63, 0x7FFFFFFF, std::numeric_limits<std::int32_t>::min()};
    for (std::int32_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t w = circular_buffer_write_svarint32(cb, v);
      expect(w > 0_u);
      std::int64_t out64;
      size_t r = circular_buffer_read_svarint64(cb, &out64);
      expect(r == w);
      expect(out64 == (std::int64_t)v);
      circular_buffer_destroy(cb);
    }
  };

  "cb write uvarint64 in 32bit range read as uvarint32"_test = [] {
    std::vector<std::uint64_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0xFFFFFFFF};
    for (std::uint64_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t w = circular_buffer_write_uvarint64(cb, v);
      expect(w > 0_u);
      std::uint32_t out32;
      size_t r = circular_buffer_read_uvarint32(cb, &out32);
      expect(r == w);
      expect(out32 == (std::uint32_t)v);
      circular_buffer_destroy(cb);
    }
  };

  "cb write svarint64 in 32bit range read as svarint32"_test = [] {
    std::vector<std::int64_t> vals = {0, 1, -1, 63, -63, 0x7FFFFFFF, (std::int64_t)std::numeric_limits<std::int32_t>::min()};
    for (std::int64_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t w = circular_buffer_write_svarint64(cb, v);
      expect(w > 0_u);
      std::int32_t out32;
      size_t r = circular_buffer_read_svarint32(cb, &out32);
      expect(r == w);
      expect(out32 == (std::int32_t)v);
      circular_buffer_destroy(cb);
    }
  };

  "cb uvarint32 and uvarint64 same bytes for 32bit range"_test = [] {
    std::vector<std::uint32_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0xFFFFFFFF};
    for (std::uint32_t v : vals) {
      auto bytes32 = uvarint32_to_bytes(v);
      auto bytes64 = uvarint_to_bytes((std::uint64_t)v);
      expect(bytes32 == bytes64);
    }
  };

  "cb svarint32 and svarint64 same bytes for 32bit range"_test = [] {
    std::vector<std::int32_t> vals = {0, 1, -1, 63, -63, 0x7FFFFFFF, std::numeric_limits<std::int32_t>::min()};
    for (std::int32_t v : vals) {
      auto bytes32 = svarint32_to_bytes(v);
      auto bytes64 = svarint_to_bytes((std::int64_t)v);
      expect(bytes32 == bytes64);
    }
  };

  "cb uvarint16 roundtrip"_test = [] {
    std::vector<std::uint16_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0x7FFF, 0xFFFF};
    for (std::uint16_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t written = circular_buffer_write_uvarint16(cb, v);
      expect(written > 0_u);
      std::uint16_t out;
      size_t read = circular_buffer_read_uvarint16(cb, &out);
      expect(read == written);
      expect(out == v);
      circular_buffer_destroy(cb);
    }
  };

  "cb uvarint16 byte lengths"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    circular_buffer_reserve_for_writing(cb, 32);
    expect(circular_buffer_write_uvarint16(cb, 0) == 1_u);
    expect(circular_buffer_write_uvarint16(cb, 0xFFFF) == 3_u);
    std::uint16_t a, b;
    size_t n1 = circular_buffer_read_uvarint16(cb, &a);
    size_t n2 = circular_buffer_read_uvarint16(cb, &b);
    expect(n1 == 1_u && a == 0_u);
    expect(n2 == 3_u && b == 0xFFFF);
    circular_buffer_destroy(cb);
  };

  "cb svarint16 roundtrip"_test = [] {
    const std::int16_t min16 = std::numeric_limits<std::int16_t>::min();
    std::vector<std::int16_t> vals = {0, 1, -1, 63, -63, 64, -64, 127, -127, 128, -128,
        0x1FFF, -0x2000, 0x7FFF, min16};
    for (std::int16_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t written = circular_buffer_write_svarint16(cb, v);
      expect(written > 0_u);
      std::int16_t out;
      size_t read = circular_buffer_read_svarint16(cb, &out);
      expect(read == written);
      expect(out == v);
      circular_buffer_destroy(cb);
    }
  };

  "cb write uvarint16 read as uvarint64 compatible"_test = [] {
    std::vector<std::uint16_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0xFFFF};
    for (std::uint16_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t w = circular_buffer_write_uvarint16(cb, v);
      expect(w > 0_u);
      std::uint64_t out64;
      size_t r = circular_buffer_read_uvarint64(cb, &out64);
      expect(r == w);
      expect(out64 == (std::uint64_t)v);
      circular_buffer_destroy(cb);
    }
  };

  "cb write svarint16 read as svarint64 compatible"_test = [] {
    std::vector<std::int16_t> vals = {0, 1, -1, 63, -63, 0x7FFF, std::numeric_limits<std::int16_t>::min()};
    for (std::int16_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t w = circular_buffer_write_svarint16(cb, v);
      expect(w > 0_u);
      std::int64_t out64;
      size_t r = circular_buffer_read_svarint64(cb, &out64);
      expect(r == w);
      expect(out64 == (std::int64_t)v);
      circular_buffer_destroy(cb);
    }
  };

  "cb write uvarint64 in 16bit range read as uvarint16"_test = [] {
    std::vector<std::uint64_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0xFFFF};
    for (std::uint64_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t w = circular_buffer_write_uvarint64(cb, v);
      expect(w > 0_u);
      std::uint16_t out16;
      size_t r = circular_buffer_read_uvarint16(cb, &out16);
      expect(r == w);
      expect(out16 == (std::uint16_t)v);
      circular_buffer_destroy(cb);
    }
  };

  "cb write svarint64 in 16bit range read as svarint16"_test = [] {
    std::vector<std::int64_t> vals = {0, 1, -1, 63, -63, 0x7FFF, (std::int64_t)std::numeric_limits<std::int16_t>::min()};
    for (std::int64_t v : vals) {
      struct circular_buffer* cb = circular_buffer_create(32, nullptr);
      circular_buffer_reserve_for_writing(cb, 32);
      size_t w = circular_buffer_write_svarint64(cb, v);
      expect(w > 0_u);
      std::int16_t out16;
      size_t r = circular_buffer_read_svarint16(cb, &out16);
      expect(r == w);
      expect(out16 == (std::int16_t)v);
      circular_buffer_destroy(cb);
    }
  };

  "cb uvarint16 and uvarint64 same bytes for 16bit range"_test = [] {
    std::vector<std::uint16_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0xFFFF};
    for (std::uint16_t v : vals) {
      auto bytes16 = uvarint16_to_bytes(v);
      auto bytes64 = uvarint_to_bytes((std::uint64_t)v);
      expect(bytes16 == bytes64);
    }
  };

  "cb svarint16 and svarint64 same bytes for 16bit range"_test = [] {
    std::vector<std::int16_t> vals = {0, 1, -1, 63, -63, 0x7FFF, std::numeric_limits<std::int16_t>::min()};
    for (std::int16_t v : vals) {
      auto bytes16 = svarint16_to_bytes(v);
      auto bytes64 = svarint_to_bytes((std::int64_t)v);
      expect(bytes16 == bytes64);
    }
  };
}
