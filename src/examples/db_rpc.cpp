#include "structures/db_entity.hpp"
#include "utils/printHex.hpp"
#include "serialization/type_registry.hpp"
#include "serialization/type_description.hpp"
#include "serialization/rpc_server.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/stream_writer.hpp"

#include <iostream>
#include <sstream>
#include <vector>
#include <memory>

int main(int argc, char** argv) {  
   
  serialization::type_registry registry;
  
  using rpc_server_type = serialization::rpc_server<serialization::stream_reader, serialization::stream_writer>;
  rpc_server_type rpc_server;

  db::Db db = db::Db::get_rpc_instance();

  /* db::Entity entity1("IPI", db::Id<db::Entity>(1));
  db.put_entity(std::make_shared<db::Entity>(entity1)); */

  std::shared_ptr<serialization::type_description> dbType = 
    serialization::type_description::create(db, registry);

  //rpc_server.register_singleton_method(&db::Db::put_entity);
  rpc_server_type::register_recursive(db, rpc_server);

   std::cout << "Db type: " << *dbType << std::endl;
  std::cout << "Known types:" << std::endl;

  for (const auto& type : registry.get_types()) {
    std::cout << *type << std::endl;
  }  
  std::cout << "--------------------------------" << std::endl; 

}