#include "serialization/type_registry.hpp"
#include "serialization/type_registry_blob.hpp"
#include "serialization/type_description.hpp"
#include "utils/printHex.hpp"

#include <iostream>

int main(int argc, char** argv) {  
  serialization::type_registry registry;
  serialization::type_description::create(registry, registry); 
  serialization::type_description::create(serialization::type_description(), registry);
  serialization::type_description::create(serialization::read_annotation(), registry); 
  
  std::cout << "Type registry:" << std::endl;
  for (const auto& type : registry.get_types()) {
    std::cout << *type << std::endl;
  }

  const std::string bytes = serialization::serialize_type_registry_blob(registry);
  std::cout << "Serialized registry:" << std::endl;
  printHex(bytes.data(), bytes.size());
  return 0;
}
