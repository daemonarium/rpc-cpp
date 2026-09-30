#include "socket_handlers.hpp"
#include <algorithm>

namespace socket_handlers {

  static std::vector<socket_handler> handlers_;

  void register_socket_handler(std::uint32_t socket_id, std::function<void()> data_handler, std::function<void()> drain_handler) {
    // Remove existing handler for this socket if any
    handlers_.erase(
      std::remove_if(handlers_.begin(), handlers_.end(),
        [socket_id](const socket_handler& h) { return h.socket_id == socket_id; }),
      handlers_.end()
    );
    
    // Add new handler
    handlers_.emplace_back(socket_id, data_handler, drain_handler);
  }

  void unregister_socket_handler(std::uint32_t socket_id) {
    handlers_.erase(
      std::remove_if(handlers_.begin(), handlers_.end(),
        [socket_id](const socket_handler& h) { return h.socket_id == socket_id; }),
      handlers_.end()
    );
  }

  void handle_socket_data(std::uint32_t socket_id) {
    auto it = std::find_if(handlers_.begin(), handlers_.end(),
      [socket_id](const socket_handler& h) { return h.socket_id == socket_id; });
    
    if(it != handlers_.end() && it->data_handler) {
      try {
        it->data_handler();
      } catch(...) {
        // Framing already skipped the bad message. Do not propagate into the
        // WASM/JS boundary (WebAssembly.Exception desyncs in-flight RPCs).
      }
    }
  }

  void handle_socket_drain(std::uint32_t socket_id) {
    auto it = std::find_if(handlers_.begin(), handlers_.end(),
      [socket_id](const socket_handler& h) { return h.socket_id == socket_id; });
    
    if(it != handlers_.end() && it->drain_handler) {
      it->drain_handler();
    }
  }

}