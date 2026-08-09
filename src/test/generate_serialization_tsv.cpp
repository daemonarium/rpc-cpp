/**
 * Generates a TSV file (no header) for cross-checking scalar serialization with TypeScript.
 * Columns: type (u64|i64|…)  value_as_string  hex_bytes
 *
 * Usage:
 *   generate_serialization_tsv [out.tsv]           # compressed (default)
 *   generate_serialization_tsv --flat [out.tsv]    # flat LE
 */

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "serialization/stream_writer.hpp"
#include "serialization/flat_stream_writer.hpp"

using namespace serialization;

static std::string bytes_to_hex(const std::string& bytes) {
  std::ostringstream hex;
  hex << std::hex << std::setfill('0');
  for (unsigned char b : bytes) {
    hex << std::setw(2) << static_cast<unsigned>(b);
  }
  return hex.str();
}

template <typename Writer, typename Fn>
static void emit_with(std::ostream& out, const char* type, const std::string& label, Fn&& write_fn) {
  std::ostringstream buf;
  Writer w(buf);
  write_fn(w);
  out << type << "\t" << label << "\t" << bytes_to_hex(buf.str()) << "\n";
}

template <typename Writer>
static void emit_all(std::ostream& out) {
  auto u64 = [&](std::uint64_t v) {
    emit_with<Writer>(out, "u64", std::to_string(v), [&](Writer& w) { w.write_u64(v); });
  };
  auto i64 = [&](std::int64_t v) {
    emit_with<Writer>(out, "i64", std::to_string(v), [&](Writer& w) { w.write_i64(v); });
  };
  auto f64 = [&](double v, const std::string& label) {
    emit_with<Writer>(out, "f64", label, [&](Writer& w) { w.write_f64(v); });
  };
  auto u32 = [&](std::uint32_t v) {
    emit_with<Writer>(out, "u32", std::to_string(v), [&](Writer& w) { w.write_u32(v); });
  };
  auto i32 = [&](std::int32_t v) {
    emit_with<Writer>(out, "i32", std::to_string(v), [&](Writer& w) { w.write_i32(v); });
  };
  auto u16 = [&](std::uint16_t v) {
    emit_with<Writer>(out, "u16", std::to_string(v), [&](Writer& w) { w.write_u16(v); });
  };
  auto i16 = [&](std::int16_t v) {
    emit_with<Writer>(out, "i16", std::to_string(v), [&](Writer& w) { w.write_i16(v); });
  };
  auto f32 = [&](float v, const std::string& label) {
    emit_with<Writer>(out, "f32", label, [&](Writer& w) { w.write_f32(v); });
  };
  auto u8 = [&](std::uint8_t v) {
    emit_with<Writer>(out, "u8", std::to_string(static_cast<unsigned>(v)), [&](Writer& w) { w.write_u8(v); });
  };
  auto i8 = [&](std::int8_t v) {
    emit_with<Writer>(out, "i8", std::to_string(static_cast<int>(v)), [&](Writer& w) { w.write_i8(v); });
  };

  u64(0); u64(1); u64(42); u64(127); u64(128);
  u64(0x3FFF); u64(0x4000); u64(0x1FFFFF); u64(0x200000);
  u64(0xFFFFFFFF); u64(0x100000000ULL); u64(0xFFFFFFFFFFFFFFFFULL);

  i64(0); i64(1); i64(-1); i64(63); i64(-63); i64(64); i64(-64);
  i64(1000000); i64(-1000000); i64(0x1FFF); i64(-0x1FFF);
  i64(0x7FFFFFFFFFFFFFFF); i64(static_cast<std::int64_t>(0x8000000000000000ULL));

  f64(0.0, "0"); f64(1.5, "1.5"); f64(-1.5, "-1.5");
  f64(3.14159265358979, "3.14159265358979");
  f64(1e100, "1e100"); f64(-1e100, "-1e100");
  f64(-std::numeric_limits<double>::infinity(), "-inf");
  f64(std::numeric_limits<double>::infinity(), "inf");
  f64(std::numeric_limits<double>::quiet_NaN(), "nan");

  u32(0); u32(1); u32(42); u32(127); u32(128);
  u32(0x3FFF); u32(0x4000); u32(0xFFFFFFFF);

  i32(0); i32(1); i32(-1); i32(63); i32(-64);
  i32(1000000); i32(-1000000); i32(0x7FFFFFFF);
  i32(static_cast<std::int32_t>(0x80000000));

  u16(0); u16(1); u16(127); u16(128); u16(0x3FFF); u16(0xFFFF);

  i16(0); i16(1); i16(-1); i16(63); i16(-64); i16(32767); i16(-32768);

  f32(0.f, "0"); f32(1.5f, "1.5"); f32(-1.5f, "-1.5");
  f32(3.14159265f, "3.14159265");
  f32(-std::numeric_limits<float>::infinity(), "-inf");
  f32(std::numeric_limits<float>::infinity(), "inf");
  f32(std::numeric_limits<float>::quiet_NaN(), "nan");

  u8(0); u8(1); u8(127); u8(128); u8(255);
  i8(0); i8(1); i8(-1); i8(127); i8(-128);
}

int main(int argc, char* argv[]) {
  bool flat = false;
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--flat") {
      flat = true;
    } else if (arg == "--compressed") {
      flat = false;
    } else if (!path) {
      path = argv[i];
    }
  }

  std::ostream* out = &std::cout;
  std::ofstream file;
  if (path) {
    file.open(path);
    if (!file) return 1;
    out = &file;
  }

  if (flat) {
    emit_all<flat_stream_writer>(*out);
  } else {
    emit_all<stream_writer>(*out);
  }
  return 0;
}
