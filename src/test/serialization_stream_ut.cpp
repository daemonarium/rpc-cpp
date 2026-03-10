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
}
