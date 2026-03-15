/**
 * Generates a TSV file (no header) for cross-checking serialization with TypeScript.
 * Columns: type (u64|i64|f64), value_as_string, hex_bytes
 * Run and redirect to a file, or pass output path as first argument.
 */

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <limits>
#include <fstream>
#include <iomanip>

#include "serialization/stream_writer.hpp"

using namespace serialization;

static std::string bytes_to_hex(const std::string& bytes) {
  std::ostringstream hex;
  hex << std::hex << std::setfill('0');
  for (unsigned char b : bytes) {
    hex << std::setw(2) << static_cast<unsigned>(b);
  }
  return hex.str();
}

static void emit_u64(std::ostream& out, std::uint64_t value) {
  std::ostringstream buf;
  stream_writer w(buf);
  w.write_u64(value);
  std::string raw = buf.str();
  out << "u64\t" << value << "\t" << bytes_to_hex(raw) << "\n";
}

static void emit_i64(std::ostream& out, std::int64_t value) {
  std::ostringstream buf;
  stream_writer w(buf);
  w.write_i64(value);
  std::string raw = buf.str();
  out << "i64\t" << value << "\t" << bytes_to_hex(raw) << "\n";
}

static void emit_f64(std::ostream& out, double value, const std::string& value_label) {
  std::ostringstream buf;
  stream_writer w(buf);
  w.write_f64(value);
  std::string raw = buf.str();
  out << "f64\t" << value_label << "\t" << bytes_to_hex(raw) << "\n";
}

static void emit_u32(std::ostream& out, std::uint32_t value) {
  std::ostringstream buf;
  stream_writer w(buf);
  w.write_u32(value);
  std::string raw = buf.str();
  out << "u32\t" << value << "\t" << bytes_to_hex(raw) << "\n";
}

static void emit_i32(std::ostream& out, std::int32_t value) {
  std::ostringstream buf;
  stream_writer w(buf);
  w.write_i32(value);
  std::string raw = buf.str();
  out << "i32\t" << value << "\t" << bytes_to_hex(raw) << "\n";
}

static void emit_u16(std::ostream& out, std::uint16_t value) {
  std::ostringstream buf;
  stream_writer w(buf);
  w.write_u16(value);
  std::string raw = buf.str();
  out << "u16\t" << value << "\t" << bytes_to_hex(raw) << "\n";
}

static void emit_i16(std::ostream& out, std::int16_t value) {
  std::ostringstream buf;
  stream_writer w(buf);
  w.write_i16(value);
  std::string raw = buf.str();
  out << "i16\t" << value << "\t" << bytes_to_hex(raw) << "\n";
}

static void emit_f32(std::ostream& out, float value, const std::string& value_label) {
  std::ostringstream buf;
  stream_writer w(buf);
  w.write_f32(value);
  std::string raw = buf.str();
  out << "f32\t" << value_label << "\t" << bytes_to_hex(raw) << "\n";
}

static void emit_u8(std::ostream& out, std::uint8_t value) {
  std::ostringstream buf;
  stream_writer w(buf);
  w.write_u8(value);
  std::string raw = buf.str();
  out << "u8\t" << static_cast<unsigned>(value) << "\t" << bytes_to_hex(raw) << "\n";
}

static void emit_i8(std::ostream& out, std::int8_t value) {
  std::ostringstream buf;
  stream_writer w(buf);
  w.write_i8(value);
  std::string raw = buf.str();
  out << "i8\t" << static_cast<int>(value) << "\t" << bytes_to_hex(raw) << "\n";
}

int main(int argc, char* argv[]) {
  std::ostream* out = &std::cout;
  std::ofstream file;
  if (argc >= 2) {
    file.open(argv[1]);
    if (!file) return 1;
    out = &file;
  }

  // u64 / uvarint
  emit_u64(*out, 0);
  emit_u64(*out, 1);
  emit_u64(*out, 42);
  emit_u64(*out, 127);
  emit_u64(*out, 128);
  emit_u64(*out, 0x3FFF);
  emit_u64(*out, 0x4000);
  emit_u64(*out, 0x1FFFFF);
  emit_u64(*out, 0x200000);
  emit_u64(*out, 0xFFFFFFFF);
  emit_u64(*out, 0x100000000ULL);
  emit_u64(*out, 0xFFFFFFFFFFFFFFFFULL);

  // i64 / svarint
  emit_i64(*out, 0);
  emit_i64(*out, 1);
  emit_i64(*out, -1);
  emit_i64(*out, 63);
  emit_i64(*out, -63);
  emit_i64(*out, 64);
  emit_i64(*out, -64);
  emit_i64(*out, 1000000);
  emit_i64(*out, -1000000);
  emit_i64(*out, 0x1FFF);
  emit_i64(*out, -0x1FFF);
  emit_i64(*out, 0x7FFFFFFFFFFFFFFF);
  emit_i64(*out, -0x8000000000000000);

  // f64
  emit_f64(*out, 0.0, "0");
  emit_f64(*out, 1.5, "1.5");
  emit_f64(*out, -1.5, "-1.5");
  emit_f64(*out, 3.14159265358979, "3.14159265358979");
  emit_f64(*out, 1e100, "1e100");
  emit_f64(*out, -1e100, "-1e100");
  emit_f64(*out, -std::numeric_limits<double>::infinity(), "-inf");
  emit_f64(*out, std::numeric_limits<double>::infinity(), "inf");
  emit_f64(*out, std::numeric_limits<double>::quiet_NaN(), "nan");

  // u32 / uvarint32
  emit_u32(*out, 0);
  emit_u32(*out, 1);
  emit_u32(*out, 42);
  emit_u32(*out, 127);
  emit_u32(*out, 128);
  emit_u32(*out, 0x3FFF);
  emit_u32(*out, 0x4000);
  emit_u32(*out, 0xFFFFFFFF);

  // i32 / svarint32
  emit_i32(*out, 0);
  emit_i32(*out, 1);
  emit_i32(*out, -1);
  emit_i32(*out, 63);
  emit_i32(*out, -64);
  emit_i32(*out, 1000000);
  emit_i32(*out, -1000000);
  emit_i32(*out, 0x7FFFFFFF);
  emit_i32(*out, static_cast<std::int32_t>(0x80000000));

  // u16 / uvarint16
  emit_u16(*out, 0);
  emit_u16(*out, 1);
  emit_u16(*out, 127);
  emit_u16(*out, 128);
  emit_u16(*out, 0x3FFF);
  emit_u16(*out, 0xFFFF);

  // i16 / svarint16
  emit_i16(*out, 0);
  emit_i16(*out, 1);
  emit_i16(*out, -1);
  emit_i16(*out, 63);
  emit_i16(*out, -64);
  emit_i16(*out, 32767);
  emit_i16(*out, -32768);

  // f32
  emit_f32(*out, 0.f, "0");
  emit_f32(*out, 1.5f, "1.5");
  emit_f32(*out, -1.5f, "-1.5");
  emit_f32(*out, 3.14159265f, "3.14159265");
  emit_f32(*out, -std::numeric_limits<float>::infinity(), "-inf");
  emit_f32(*out, std::numeric_limits<float>::infinity(), "inf");
  emit_f32(*out, std::numeric_limits<float>::quiet_NaN(), "nan");

  // u8
  emit_u8(*out, 0);
  emit_u8(*out, 1);
  emit_u8(*out, 127);
  emit_u8(*out, 128);
  emit_u8(*out, 255);

  // i8
  emit_i8(*out, 0);
  emit_i8(*out, 1);
  emit_i8(*out, -1);
  emit_i8(*out, 127);
  emit_i8(*out, -128);

  return 0;
}
