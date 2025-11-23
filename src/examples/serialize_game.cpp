#include "structures/game_object.hpp"
#include "utils/printHex.hpp"
#include "serialization/stream_writer.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/type_registry.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"

#include <iostream>
#include <sstream>
#include <vector>
#include <memory>

int main(int argc, char** argv) {  
  
  std::ostringstream ss2;
  serialization::stream_writer sw2(ss2);

  std::vector<std::shared_ptr<GameObject>> gameObjects = {
    std::make_shared<Player>(),
    std::make_shared<Enemy>()
  };

  sw2.register_serialization_function<Player>();
  sw2.register_serialization_function<Enemy>();

  serialization::serialize(sw2, gameObjects);
  sw2.write_rest(); 

  std::string s2 = ss2.str();
  printHex(s2.data(), s2.size());

  std::istringstream is2(s2);
  serialization::stream_reader sr2(is2);
  sr2.register_deserialization_function<Player>();
  sr2.register_deserialization_function<Enemy>();
  std::vector<std::shared_ptr<GameObject>> gameObjects2;
  serialization::deserialize(sr2, gameObjects2);
  sr2.read_rest();

  std::cout << "Deserialized game objects:" << std::endl;
  for (const auto& gameObject : gameObjects2) {
    std::shared_ptr<Player> player = std::dynamic_pointer_cast<Player>(gameObject);
    std::shared_ptr<Enemy> enemy = std::dynamic_pointer_cast<Enemy>(gameObject);
    if(player) {
      std::cout << "  Player" << std::endl;
      std::cout << "    Name: " << player->name << std::endl;
    } else if(enemy) {
      std::cout << "  Enemy" << std::endl;
      std::cout << "    Level: " << enemy->level << std::endl;
    }
  }
}
