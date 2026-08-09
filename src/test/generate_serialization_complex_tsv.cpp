/**
 * Generates complex Person golden TSV for TS↔C++ cross-checks.
 * Columns: kind  format  seed  hex
 *
 * kind: person_value | person_empty | person_graph | person_graph_null
 * format: compressed | flat
 */

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "test/person_fixture.hpp"

using person_fixture::Person;
using person_fixture::encode_compressed;
using person_fixture::encode_compressed_shared;
using person_fixture::encode_flat;
using person_fixture::encode_flat_shared;
using person_fixture::make_person_graph;
using person_fixture::make_person_value;

static std::string bytes_to_hex(const std::string& bytes) {
  std::ostringstream hex;
  hex << std::hex << std::setfill('0');
  for (unsigned char b : bytes) {
    hex << std::setw(2) << static_cast<unsigned>(b);
  }
  return hex.str();
}

static std::string seed_str(std::uint64_t seed) {
  std::ostringstream os;
  os << "0x" << std::hex << seed;
  return os.str();
}

static void emit(std::ostream& out, const char* kind, const char* format,
    std::uint64_t seed, const std::string& bytes) {
  out << kind << "\t" << format << "\t" << seed_str(seed) << "\t" << bytes_to_hex(bytes) << "\n";
}

static void emit_value(std::ostream& out, std::uint64_t seed) {
  Person p = make_person_value(seed);
  emit(out, "person_value", "compressed", seed, encode_compressed(p));
  emit(out, "person_value", "flat", seed, encode_flat(p));
}

static void emit_graph(std::ostream& out, std::uint64_t seed) {
  auto root = make_person_graph(seed);
  emit(out, "person_graph", "compressed", seed, encode_compressed_shared(root));
  emit(out, "person_graph", "flat", seed, encode_flat_shared(root));
}

int main(int argc, char* argv[]) {
  std::ostream* out = &std::cout;
  std::ofstream file;
  if (argc >= 2) {
    file.open(argv[1]);
    if (!file) return 1;
    out = &file;
  }

  // Empty person (kind person_empty, seed 0)
  {
    Person empty;
    emit(*out, "person_empty", "compressed", 0, encode_compressed(empty));
    emit(*out, "person_empty", "flat", 0, encode_flat(empty));
  }

  // Edge value seeds
  for (std::uint64_t seed : {
      1ULL, 2ULL, 7ULL, 11ULL, 13ULL, 42ULL, 99ULL, 128ULL, 256ULL, 1000ULL, 0xDEADBEEFULL
    }) {
    emit_value(*out, seed);
  }

  // First 16 generated value seeds
  for (int i = 0; i < 16; ++i) {
    const std::uint64_t seed = 0xC0FFEEu + static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ULL;
    emit_value(*out, seed);
  }

  // Null graph root
  {
    std::shared_ptr<Person> nil;
    emit(*out, "person_graph_null", "compressed", 0, encode_compressed_shared(nil));
    emit(*out, "person_graph_null", "flat", 0, encode_flat_shared(nil));
  }

  // First 16 generated graph seeds
  for (int i = 0; i < 16; ++i) {
    const std::uint64_t seed = 0xA11CEu + static_cast<std::uint64_t>(i) * 0xD1B54A32D192ED03ULL;
    emit_graph(*out, seed);
  }

  return 0;
}
