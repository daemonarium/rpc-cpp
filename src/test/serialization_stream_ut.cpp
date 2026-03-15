#include <boost/ut.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <limits>

#include "serialization/stream_writer.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"

using namespace serialization;

namespace {
  template <typename T>
  T roundtrip(const T& value) {
    std::ostringstream os;
    stream_writer w(os);
    serialize(w, value);
    w.write_rest();

    std::istringstream is(os.str());
    stream_reader r(is);
    T result{};
    deserialize(r, result);
    r.read_rest();
    return result;
  }

  // Returns serialized bytes for uvarint (same encoding as stream_writer).
  std::vector<std::uint8_t> uvarint_to_bytes(std::uint64_t value) {
    std::ostringstream os;
    stream_writer w(os);
    w.write_u64(value);
    std::string s = os.str();
    return std::vector<std::uint8_t>(s.begin(), s.end());
  }

  // Returns serialized bytes for svarint (same encoding as stream_writer).
  std::vector<std::uint8_t> svarint_to_bytes(std::int64_t value) {
    std::ostringstream os;
    stream_writer w(os);
    w.write_i64(value);
    std::string s = os.str();
    return std::vector<std::uint8_t>(s.begin(), s.end());
  }

  std::vector<std::uint8_t> uvarint32_to_bytes(std::uint32_t value) {
    std::ostringstream os;
    stream_writer w(os);
    w.write_u32(value);
    std::string s = os.str();
    return std::vector<std::uint8_t>(s.begin(), s.end());
  }

  std::vector<std::uint8_t> svarint32_to_bytes(std::int32_t value) {
    std::ostringstream os;
    stream_writer w(os);
    w.write_i32(value);
    std::string s = os.str();
    return std::vector<std::uint8_t>(s.begin(), s.end());
  }

  std::vector<std::uint8_t> uvarint16_to_bytes(std::uint16_t value) {
    std::ostringstream os;
    stream_writer w(os);
    w.write_u16(value);
    std::string s = os.str();
    return std::vector<std::uint8_t>(s.begin(), s.end());
  }

  std::vector<std::uint8_t> svarint16_to_bytes(std::int16_t value) {
    std::ostringstream os;
    stream_writer w(os);
    w.write_i16(value);
    std::string s = os.str();
    return std::vector<std::uint8_t>(s.begin(), s.end());
  }

  // Returns serialized bytes for float64 (same encoding as stream_writer).
  std::vector<std::uint8_t> float64_to_bytes(double value) {
    std::ostringstream os;
    stream_writer w(os);
    w.write_f64(value);
    std::string s = os.str();
    return std::vector<std::uint8_t>(s.begin(), s.end());
  }

  // Returns serialized bytes for float32 (same encoding as stream_writer).
  std::vector<std::uint8_t> float32_to_bytes(float value) {
    std::ostringstream os;
    stream_writer w(os);
    w.write_f32(value);
    std::string s = os.str();
    return std::vector<std::uint8_t>(s.begin(), s.end());
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

  "stream serialize uint64 small values"_test = [] {
    expect(roundtrip<std::uint64_t>(0) == 0_u);
    expect(roundtrip<std::uint64_t>(1) == 1_u);
    expect(roundtrip<std::uint64_t>(42) == 42_u);
    expect(roundtrip<std::uint64_t>(127) == 127_u);
  };

  "stream serialize uint64 varint boundaries"_test = [] {
    expect(roundtrip<std::uint64_t>(128) == std::uint64_t(128));      // 2-byte boundary
    expect(roundtrip<std::uint64_t>(0x3FFF) == std::uint64_t(0x3FFF));
    expect(roundtrip<std::uint64_t>(0x4000) == std::uint64_t(0x4000)); // 3-byte boundary
    expect(roundtrip<std::uint64_t>(0x1FFFFF) == std::uint64_t(0x1FFFFF));
    expect(roundtrip<std::uint64_t>(0x200000) == std::uint64_t(0x200000)); // 4-byte boundary
  };

  "stream serialize uint64 large values"_test = [] {
    expect(roundtrip<std::uint64_t>(0xFFFFFFFF) == std::uint64_t(0xFFFFFFFF));
    expect(roundtrip<std::uint64_t>(0x100000000ULL) == std::uint64_t(0x100000000ULL));
    expect(roundtrip<std::uint64_t>(0xFFFFFFFFFFFFFFFFULL) == std::uint64_t(0xFFFFFFFFFFFFFFFFULL));
  };

  "stream serialize uint32"_test = [] {
    expect(roundtrip<std::uint32_t>(0) == 0_u);
    expect(roundtrip<std::uint32_t>(12345) == 12345_u);
    expect(roundtrip<std::uint32_t>(0xFFFFFFFF) == std::uint32_t(0xFFFFFFFF));
  };

  "stream serialize int64 roundtrip"_test = [] {
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

  "stream serialize int32"_test = [] {
    expect(roundtrip<std::int32_t>(0) == 0_i);
    expect(roundtrip<std::int32_t>(-42) == -42_i);
    expect(roundtrip<std::int32_t>(42) == 42_i);
  };

  // Note: serialize(w, double) is ambiguous (SignedInteger and FloatingPoint both match double).
  // Test doubles using write_f64/read_f64 directly.
  "stream serialize double roundtrip"_test = [] {
    auto double_rt = [](double v) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_f64(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      double result;
      r.read_f64(result);
      return result;
    };

    expect(double_rt(0.0) == 0.0_d);
    expect(double_rt(1.5) == 1.5_d);
    expect(double_rt(-1.5) == -1.5_d);
    expect(double_rt(3.14159265358979) == 3.14159265358979_d);
    expect(double_rt(double(1e100)) == double(1e100));
    expect(double_rt(double(-1e100)) == double(-1e100));
    expect(double_rt(-std::numeric_limits<double>::infinity()) == -std::numeric_limits<double>::infinity());
    expect(double_rt(std::numeric_limits<double>::infinity()) == std::numeric_limits<double>::infinity());
    expect(std::isnan(double_rt(std::numeric_limits<double>::quiet_NaN())));
  };

  "stream serialize uint8 roundtrip"_test = [] {
    auto u8_rt = [](std::uint8_t v) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_u8(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      std::uint8_t result;
      r.read_u8(result);
      return result;
    };
    expect(u8_rt(0) == 0_u);
    expect(u8_rt(1) == 1_u);
    expect(u8_rt(127) == 127_u);
    expect(u8_rt(128) == 128_u);
    expect(u8_rt(255) == 255_u);
  };

  "stream serialize int8 roundtrip"_test = [] {
    auto i8_rt = [](std::int8_t v) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_i8(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      std::int8_t result;
      r.read_i8(result);
      return result;
    };
    expect(i8_rt(0) == 0_i);
    expect(i8_rt(1) == 1_i);
    expect(i8_rt(-1) == -1_i);
    expect(i8_rt(127) == 127_i);
    expect(i8_rt(-128) == -128_i);
  };

  "stream serialize float roundtrip"_test = [] {
    auto float_rt = [](float v) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_f32(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      float result;
      r.read_f32(result);
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

  "stream serialization preserves sort order float32"_test = [] {
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

  "stream serialize bool roundtrip"_test = [] {
    expect(roundtrip<bool>(true) == true);
    expect(roundtrip<bool>(false) == false);
  };

  "stream serialize string roundtrip"_test = [] {
    expect(roundtrip<std::string>("") == std::string(""));
    expect(roundtrip<std::string>("hello") == std::string("hello"));

    // Long string (>128 chars to test varint length prefix)
    std::string long_str(200, 'x');
    expect(roundtrip<std::string>(long_str) == long_str);

    // UTF-8
    expect(roundtrip<std::string>("zażółć gęślą jaźń") == std::string("zażółć gęślą jaźń"));
  };

  "stream serialize binary roundtrip"_test = [] {
    std::vector<std::uint8_t> empty;
    expect(roundtrip<std::vector<std::uint8_t>>(empty) == empty);

    std::vector<std::uint8_t> data(4096);
    for(size_t i = 0; i < data.size(); ++i) {
      data[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    expect(roundtrip<std::vector<std::uint8_t>>(data) == data);
  };

  "stream serialize vector<uint64_t> roundtrip"_test = [] {
    std::vector<std::uint64_t> empty;
    expect(roundtrip<std::vector<std::uint64_t>>(empty) == empty);

    std::vector<std::uint64_t> v = {1, 2, 3, 100, 0xFFFFFFFFFFFFFFFFULL};
    expect(roundtrip<std::vector<std::uint64_t>>(v) == v);
  };

  "stream serialize vector<string> roundtrip"_test = [] {
    std::vector<std::string> v = {"hello", "world", "", "test123"};
    expect(roundtrip<std::vector<std::string>>(v) == v);
  };

  "stream serialize nested vector roundtrip"_test = [] {
    std::vector<std::vector<std::int32_t>> v = {{1, 2, 3}, {}, {-1, 0, 1}};
    expect(roundtrip<std::vector<std::vector<std::int32_t>>>(v) == v);
  };

  "stream serialize map<string,uint64_t> roundtrip"_test = [] {
    std::map<std::string, std::uint64_t> empty;
    expect(roundtrip<std::map<std::string, std::uint64_t>>(empty) == empty);

    std::map<std::string, std::uint64_t> m = {{"a", 1}, {"bb", 200}, {"ccc", 0}};
    expect(roundtrip<std::map<std::string, std::uint64_t>>(m) == m);
  };

  "stream serialize multiple values in sequence"_test = [] {
    std::ostringstream os;
    stream_writer w(os);
    serialize(w, std::uint64_t(42));
    serialize(w, std::string("hello"));
    serialize(w, true);
    serialize(w, std::int64_t(-7));
    w.write_rest();

    std::istringstream is(os.str());
    stream_reader r(is);

    std::uint64_t u; deserialize(r, u);
    std::string s; deserialize(r, s);
    bool b; deserialize(r, b);
    std::int64_t i; deserialize(r, i);
    r.read_rest();

    expect(u == 42_u);
    expect(s == std::string("hello"));
    expect(b == true);
    expect(i == -7_i);
  };

  // Sort order is preserved: binary comparison of serialized form matches numeric comparison.
  // Same encoding as circular buffer; critical for DB keys.
  "stream serialization preserves sort order uvarint svarint float64"_test = [] {
    // --- uvarint ---
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

    // --- svarint ---
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

    // --- float64 (exclude NaN for pairwise comparison) ---
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
        expect(three_way_cmp(a, b) == bytes_three_way_cmp(ba, bb));
      }
    }

    // NaN: two NaNs must serialize to the same bytes.
    double nan = std::numeric_limits<double>::quiet_NaN();
    expect(float64_to_bytes(nan) == float64_to_bytes(nan));
  };

  "stream uvarint32 roundtrip"_test = [] {
    std::vector<std::uint32_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0x1FFFFF, 0x200000,
        0xFFFFFF, 0x10000000 - 1, 0x10000000, 0x7FFFFFFF, 0xFFFFFFFF};
    for (std::uint32_t v : vals) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_u32(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      std::uint32_t out;
      r.read_u32(out);
      expect(out == v);
    }
  };

  "stream svarint32 roundtrip"_test = [] {
    const std::int32_t min32 = std::numeric_limits<std::int32_t>::min();
    std::vector<std::int32_t> vals = {0, 1, -1, 63, -63, 64, -64, 127, -127, 128, -128,
        0x1FFF, -0x2000, 0x7FFFFFFF, min32};
    for (std::int32_t v : vals) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_i32(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      std::int32_t out;
      r.read_i32(out);
      expect(out == v);
    }
  };

  "stream write uvarint32 read as uvarint64 compatible"_test = [] {
    std::vector<std::uint32_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0x1FFFFF, 0xFFFFFFFF};
    for (std::uint32_t v : vals) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_u32(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      std::uint64_t out64;
      r.read_u64(out64);
      expect(out64 == static_cast<std::uint64_t>(v));
    }
  };

  "stream write svarint32 read as svarint64 compatible"_test = [] {
    std::vector<std::int32_t> vals = {0, 1, -1, 63, -63, 0x7FFFFFFF, std::numeric_limits<std::int32_t>::min()};
    for (std::int32_t v : vals) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_i32(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      std::int64_t out64;
      r.read_i64(out64);
      expect(out64 == static_cast<std::int64_t>(v));
    }
  };

  "stream uvarint32 and uvarint64 same bytes for 32bit range"_test = [] {
    std::vector<std::uint32_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0xFFFFFFFF};
    for (std::uint32_t v : vals) {
      auto bytes32 = uvarint32_to_bytes(v);
      auto bytes64 = uvarint_to_bytes(static_cast<std::uint64_t>(v));
      expect(bytes32 == bytes64);
    }
  };

  "stream svarint32 and svarint64 same bytes for 32bit range"_test = [] {
    std::vector<std::int32_t> vals = {0, 1, -1, 63, -63, 0x7FFFFFFF, std::numeric_limits<std::int32_t>::min()};
    for (std::int32_t v : vals) {
      auto bytes32 = svarint32_to_bytes(v);
      auto bytes64 = svarint_to_bytes(static_cast<std::int64_t>(v));
      expect(bytes32 == bytes64);
    }
  };

  "stream uvarint16 roundtrip"_test = [] {
    std::vector<std::uint16_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0x7FFF, 0xFFFF};
    for (std::uint16_t v : vals) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_u16(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      std::uint16_t out;
      r.read_u16(out);
      expect(out == v);
    }
  };

  "stream svarint16 roundtrip"_test = [] {
    const std::int16_t min16 = std::numeric_limits<std::int16_t>::min();
    std::vector<std::int16_t> vals = {0, 1, -1, 63, -63, 64, -64, 127, -127, 128, -128,
        0x1FFF, -0x2000, 0x7FFF, min16};
    for (std::int16_t v : vals) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_i16(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      std::int16_t out;
      r.read_i16(out);
      expect(out == v);
    }
  };

  "stream write uvarint16 read as uvarint64 compatible"_test = [] {
    std::vector<std::uint16_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0xFFFF};
    for (std::uint16_t v : vals) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_u16(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      std::uint64_t out64;
      r.read_u64(out64);
      expect(out64 == static_cast<std::uint64_t>(v));
    }
  };

  "stream write svarint16 read as svarint64 compatible"_test = [] {
    std::vector<std::int16_t> vals = {0, 1, -1, 63, -63, 0x7FFF, std::numeric_limits<std::int16_t>::min()};
    for (std::int16_t v : vals) {
      std::ostringstream os;
      stream_writer w(os);
      w.write_i16(v);
      std::istringstream is(os.str());
      stream_reader r(is);
      std::int64_t out64;
      r.read_i64(out64);
      expect(out64 == static_cast<std::int64_t>(v));
    }
  };

  "stream uvarint16 and uvarint64 same bytes for 16bit range"_test = [] {
    std::vector<std::uint16_t> vals = {0, 1, 127, 128, 0x3FFF, 0x4000, 0xFFFF};
    for (std::uint16_t v : vals) {
      auto bytes16 = uvarint16_to_bytes(v);
      auto bytes64 = uvarint_to_bytes(static_cast<std::uint64_t>(v));
      expect(bytes16 == bytes64);
    }
  };

  "stream svarint16 and svarint64 same bytes for 16bit range"_test = [] {
    std::vector<std::int16_t> vals = {0, 1, -1, 63, -63, 0x7FFF, std::numeric_limits<std::int16_t>::min()};
    for (std::int16_t v : vals) {
      auto bytes16 = svarint16_to_bytes(v);
      auto bytes64 = svarint_to_bytes(static_cast<std::int64_t>(v));
      expect(bytes16 == bytes64);
    }
  };
}
