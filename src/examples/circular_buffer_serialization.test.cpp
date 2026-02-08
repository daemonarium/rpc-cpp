#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

#include <serialization/circular_buffer_writer.hpp>
#include <serialization/circular_buffer_reader.hpp>

struct Sample {
  std::uint64_t id;
  std::string name;
};

void serialize(serialization::writer& writer, const Sample& sample) {
  writer.write_u64(sample.id);
  writer.write_string(sample.name);
}

void deserialize(serialization::reader& reader, Sample& sample) {
  reader.read_u64(sample.id);
  reader.read_string(sample.name);
}

int main() {
  struct circular_buffer* buffer = circular_buffer_create(128, NULL);
  assert(buffer);

  serialization::circular_buffer_writer writer(buffer);
  writer.register_serialization_function<Sample>();

  auto sample = std::make_shared<Sample>(Sample{42, "answer"});

  writer.write_u64(123456789);
  writer.write_i64(-123456);
  writer.write_bool(true);
  writer.write_pointer(sample, [&]() {
    writer.write_typed(sample);
  });
  writer.write_rest();

  serialization::circular_buffer_reader reader(buffer);
  reader.register_deserialize_typed_function<Sample>();

  std::uint64_t u64 = 0;
  std::int64_t i64 = 0;
  bool boolean = false;
  std::shared_ptr<Sample> received;

  reader.read_u64(u64);
  reader.read_i64(i64);
  reader.read_bool(boolean);
  reader.read_pointer(received, [&reader](std::shared_ptr<void>& data) {
    reader.read_typed(data);
  });
  reader.read_rest();

  assert(u64 == 123456789);
  assert(i64 == -123456);
  assert(boolean == true);
  assert(received);
  assert(received->id == sample->id);
  assert(received->name == sample->name);

  circular_buffer_destroy(buffer);
  std::cout << "circular buffer serialization test passed" << std::endl;
  return 0;
}

