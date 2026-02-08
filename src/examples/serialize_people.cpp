#include "structures/person.hpp"
#include "utils/printHex.hpp"
#include "serialization/stream_writer.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/type_registry.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"
#include "serialization/debug_writer.hpp"

#include <iostream>
#include <sstream>
#include <vector>
#include <memory>

int main(int argc, char** argv) {  

  auto john = std::make_shared<Person>();
  john->name = "John";
  john->age = 30;
  john->occupation = Occupation("Engineer", 10);
  john->skills = {
    Skill("C++", 10),
    Skill("Python", 8),
    Skill("Java", 6)
  };

  auto jane = std::make_shared<Person>();
  jane->name = "Jane";
  jane->age = 25;
  jane->occupation = Occupation("Teacher", 5);
  jane->skills = {
    Skill("English", 10),
    Skill("Math", 8),
    Skill("Science", 6)
  };

  john->friends.push_back(jane);
  john->friends.push_back(nullptr);
  jane->friends.push_back(john);
  
  std::vector<std::shared_ptr<Person>> people = { john, jane };

  serialization::debug_writer dw(std::cout);
  
  print_people(people);
 
  std::cout << "Debug Serializing..." << std::endl << std::endl;
  dw.register_serialize_typed_function<Person>();
  serialization::serialize(dw, john);
  dw.write_rest();

  std::cout << "Serializing..." << std::endl << std::endl;
  std::ostringstream ss;
  serialization::stream_writer sw(ss);
  sw.register_serialize_typed_function<Person>();
  serialization::serialize(sw, john);
  sw.write_rest();

  std::string s = ss.str();
  printHex(s.data(), s.size());

  std::cout << "Deserializing..." << std::endl << std::endl;
  std::istringstream is(s);
  serialization::stream_reader sr(is);
  sr.register_deserialize_typed_function<Person>();
  std::shared_ptr<Person> p;
  serialization::deserialize(sr, p);
  sr.read_rest();

  std::cout << "Deserialized person:" << std::endl;
  print_people({p});
}