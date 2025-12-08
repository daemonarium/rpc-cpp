#include "serialization/type_registry.hpp"
#include "serialization/stream_writer.hpp"
#include "utils/printHex.hpp"
#include "serialization/serialize.hpp"

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


  std::ostringstream output_stream;
  serialization::stream_writer writer(output_stream);
  writer.register_serialization_function<serialization::type_description>();
  serialize(writer, registry);
  writer.write_rest();  
  std::cout << "Serialized registry:" << std::endl;
  printHex(output_stream.str().data(), output_stream.str().size());
  return 0;
}