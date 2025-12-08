#include "serialization/type_registry.hpp"
#include "serialization/stream_reader.hpp"

#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {  
  std::fstream file(argv[1], std::ios::in | std::ios::binary);
  if(!file.is_open()) {
    std::cerr << "Failed to open registry file" << std::endl;
    return 1;
  }
  file.seekg(0, std::ios::end);
  size_t size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<char> buffer(size);
  file.read(buffer.data(), size);
  std::istringstream input_stream(std::string(buffer.data(), size), std::ios::binary);
  std::cout << "File size: " << size << std::endl;
  serialization::stream_reader reader(input_stream);
  reader.register_deserialization_function<serialization::type_description>();
  serialization::type_registry registry;
  deserialize(reader, registry);
  std::cout << "Read rest: " << input_stream.tellg() << std::endl;
  reader.read_rest();
  std::cout << "Type registry:" << std::endl;
  for (const auto& type : registry.get_types()) {
    std::cout << *type << std::endl;
  }

  return 0;
}