#include "structures/contract.hpp"
#include "structures/person.hpp"
#include "utils/printHex.hpp"
#include "serialization/type_registry.hpp"
#include "serialization/type_description.hpp"


#include <iostream>
#include <sstream>
#include <vector>
#include <memory>

int main(int argc, char** argv) {  
  
  serialization::type_registry registry;

  std::shared_ptr<serialization::type_description> contractType = 
    serialization::type_description::create(Contract(), registry);
  std::cout << "Contract type: " << *contractType << std::endl; 
  std::cout << "Known types:" << std::endl;
  for (const auto& type : registry.get_types()) {
    std::cout << *type << std::endl;
  }  
  std::cout << "--------------------------------" << std::endl;


}