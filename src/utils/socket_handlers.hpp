#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace socket_handlers {

  struct socket_handler {
    std::uint32_t socket_id;
    std::function<void()> data_handler;
    std::function<void()> drain_handler;
    
    socket_handler(std::uint32_t id, std::function<void()> data, std::function<void()> drain = nullptr)
      : socket_id(id), data_handler(data), drain_handler(drain) {}
  };

  void register_socket_handler(std::uint32_t socket_id, std::function<void()> data_handler, std::function<void()> drain_handler = nullptr);
  void unregister_socket_handler(std::uint32_t socket_id);
  void handle_socket_data(std::uint32_t socket_id);
  void handle_socket_drain(std::uint32_t socket_id);

}