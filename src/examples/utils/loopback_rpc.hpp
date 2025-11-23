#pragma once

#include "utils/printHex.hpp"
#include "serialization/type_registry.hpp"
#include "serialization/type_description.hpp"
#include "serialization/rpc_server.hpp"
#include "serialization/rpc_client.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/stream_writer.hpp"

#include <iostream>
#include <sstream>
#include <vector>
#include <memory>

namespace serialization {

  
class loopback_rpc {
  public:

  using rpc_server_type = serialization::rpc_server<serialization::stream_reader, serialization::stream_writer>;
  using rpc_client_type = serialization::rpc_client<serialization::stream_reader, serialization::stream_writer>;

  serialization::type_registry registry;

  std::ostringstream output_stream;
  serialization::stream_writer writer;
  std::istringstream input_stream;  
  serialization::stream_reader reader;  

  rpc_server_type rpc_server;
  std::shared_ptr<rpc_client_type> rpc_client;

  bool print_messages = false;

  loopback_rpc(bool print_messagesp = false) :       
      writer(output_stream), reader(input_stream),
      rpc_server(), 
      rpc_client(std::make_shared<rpc_client_type>(writer, [this]() { this->handle_call(); })),
      registry(),
      print_messages(print_messagesp) {

  }

  void handle_call() {
    std::string call = output_stream.str();
    if(print_messages) std::cout << "Sending call: " << std::endl;
    printHex(call.data(), call.size());
    input_stream.str(call);
    input_stream.seekg(0);
    output_stream.str("");
    output_stream.seekp(0);
    if(print_messages) std::cout << "Handling call..." << std::endl;
    rpc_server.handle_call(reader, writer);
    std::string response = output_stream.str();
    if(print_messages) std::cout << "Received response: " << std::endl;
    printHex(response.data(), response.size());
    input_stream.str(response);
    input_stream.seekg(0);
    output_stream.str("");
    output_stream.seekp(0);
    rpc_client->handle_response(reader);
  }

  template <typename T>
  std::shared_ptr<serialization::type_description> register_rpc(T& t) {        
    rpc_server_type::register_recursive(t, rpc_server);
    return serialization::type_description::create(t, registry);  
  }
};

}