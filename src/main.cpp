
#include <iostream>
#include <meta>
#include <simdjson/json_builder/json_builder.h>
#include <simdjson/json_builder/string_builder.h>

#include "serialization/serialize.hpp"
#include "serialization/debug_writer.hpp"
#include "serialization/stream_writer.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/deserialize.hpp"
#include "serialization/type_description.hpp"
#include "serialization/type_registry.hpp"

#include <iomanip>
#include <memory>
#include <typeindex>

#include "examples/structures/contract.hpp"
#include "serialization/meta_expand.hpp"

#include "utils/printClass.hpp"
#include "utils/printHex.hpp"


int main(int argc, char** argv) {  


/*   [:serialization::expand(std::meta::annotations_of(^^Person::name)):] >> [&]<auto a> {
    std::cout << "Annotation: " << std::meta::display_string_of(std::meta::type_of(a)) << std::endl;
  }; */


/*   printClass<Contract>();

  Contract ipi("IPI", "ipi", 8, 1000000);
  std::string name = callMethodByName<Contract, std::string>(ipi, "name");
  std::cout << "Name: " << name << std::endl;
  std::cout << "Name: " << ipi.name() << std::endl; */
 


  

  /* registry.register_type(TypeDescription::create(Person(), registry));
  std::shared_ptr<TypeDescription> personType 
    = TypeDescription::create(Person(), registry);
  std::cout << "Person type: " << *personType << std::endl; 
  std::cout << "Known types:" << std::endl;
  for (const auto& type : registry.get_types()) {
    std::cout << *type << std::endl;
  }
  */

/* 
  std::ostringstream ss;
  serialization::stream_writer sw(ss);
  serialization::serialize(sw, p);
  std::cout << ss.str() << std::endl;

  std::string s = ss.str();
  printHex(s.data(), s.size());

  std::istringstream is(s);
  serialization::stream_reader sr(is);
  Person p2;
  serialization::deserialize(sr, p2);
  std::cout << "Deserialized person:" << std::endl;
  std::cout << "Name: " << p2.name << ", Age: " << p2.age << std::endl;
  std::cout << "Occupation: " << p2.occupation.name << ", Years: " << p2.occupation.years << std::endl;
  std::cout << "Skills: " << std::endl;
  for (const auto& skill : p2.skills) {
    std::cout << "  " << skill.name << ", Level: " << skill.level << std::endl;
  }

  std::cout << "Method parameters:" << std::endl;
  printMethod<^^add>(); */

  return 0;
}
