#include "serialization/type_registry.hpp"
#include "serialization/type_registry_blob.hpp"

#include <iostream>
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
  std::cout << "File size: " << size << std::endl;

  serialization::type_registry registry(false);
  serialization::deserialize_type_registry_blob(
    std::string_view(buffer.data(), buffer.size()),
    registry
  );
  std::cout << "Data format: " << static_cast<unsigned>(registry.data_format()) << std::endl;
  std::cout << "Type registry:" << std::endl;
  for (const auto& type : registry.get_types()) {
    std::cout << *type << std::endl;
  }

  return 0;
}
