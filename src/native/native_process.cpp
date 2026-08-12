#include "native/native_process.hpp"

#include <stdexcept>

namespace waos::native {

  native_process::native_process(std::uint64_t id, std::size_t socket_count)
      : id_(id) {
    sockets_.reserve(socket_count);
    for(std::size_t i = 0; i < socket_count; ++i) {
      sockets_.emplace_back(std::make_unique<socket_state>());
    }
  }

  native_process::~native_process() {
    for(std::uint32_t i = 0; i < sockets_.size(); ++i) {
      if(sockets_[i]->open) {
        close_socket(i);
      }
    }
  }

  socket_state& native_process::require_socket_(std::uint32_t socket) {
    if(socket >= sockets_.size()) {
      throw std::runtime_error("native_process: socket index out of range");
    }
    return *sockets_[socket];
  }

  socket_state& native_process::require_open_socket_(std::uint32_t socket) {
    auto& s = require_socket_(socket);
    if(!s.open) {
      throw std::runtime_error("native_process: socket is not open");
    }
    return s;
  }

  void native_process::open_socket(std::uint32_t index, std::size_t buf_size) {
    if(!is_alive_) {
      throw std::runtime_error("native_process: process is not alive");
    }
    auto& s = require_socket_(index);
    if(s.open) {
      throw std::runtime_error("native_process: socket already open");
    }
    if(buf_size == 0) {
      throw std::runtime_error("native_process: buf_size must be > 0");
    }
    if(circular_buffer_init(&s.input_buffer, buf_size, nullptr) == nullptr) {
      throw std::runtime_error("native_process: failed to init input buffer");
    }
    if(circular_buffer_init(&s.output_buffer, buf_size, nullptr) == nullptr) {
      circular_buffer_deinit(&s.input_buffer);
      throw std::runtime_error("native_process: failed to init output buffer");
    }
    s.open = true;
  }

  void native_process::close_socket(std::uint32_t index) {
    auto& s = require_socket_(index);
    if(!s.open) {
      return;
    }
    s.data_handler = nullptr;
    s.drain_handler = nullptr;
    circular_buffer_deinit(&s.input_buffer);
    circular_buffer_deinit(&s.output_buffer);
    s.open = false;
  }

  void native_process::handle_data(std::uint32_t socket) {
    if(!is_alive_) {
      return;
    }
    if(socket >= sockets_.size()) {
      return;
    }
    auto& s = *sockets_[socket];
    if(s.open && s.data_handler) {
      s.data_handler();
    }
  }

  void native_process::handle_drain(std::uint32_t socket) {
    if(!is_alive_) {
      return;
    }
    if(socket >= sockets_.size()) {
      return;
    }
    auto& s = *sockets_[socket];
    if(s.open && s.drain_handler) {
      s.drain_handler();
    }
  }

  void native_process::flush(std::uint32_t socket) {
    if(!is_alive_) {
      return;
    }
    if(flush_cb_) {
      flush_cb_(socket);
    }
  }

  void native_process::drain(std::uint32_t socket) {
    if(!is_alive_) {
      return;
    }
    if(drain_cb_) {
      drain_cb_(socket);
    }
  }

  void native_process::register_socket_handler(
      std::uint32_t socket,
      std::function<void()> data,
      std::function<void()> drain) {
    auto& s = require_open_socket_(socket);
    s.data_handler = std::move(data);
    s.drain_handler = std::move(drain);
  }

  void native_process::unregister_socket_handler(std::uint32_t socket) {
    auto& s = require_open_socket_(socket);
    s.data_handler = nullptr;
    s.drain_handler = nullptr;
  }

  circular_buffer& native_process::input_buffer(std::uint32_t socket) {
    return require_open_socket_(socket).input_buffer;
  }

  circular_buffer& native_process::output_buffer(std::uint32_t socket) {
    return require_open_socket_(socket).output_buffer;
  }

  void native_process::set_flush_callback(std::function<void(std::uint32_t)> cb) {
    flush_cb_ = std::move(cb);
  }

  void native_process::set_drain_callback(std::function<void(std::uint32_t)> cb) {
    drain_cb_ = std::move(cb);
  }

  void native_process::kill(std::optional<std::string> /*error*/) {
    if(!is_alive_) {
      return;
    }
    is_alive_ = false;
    for(std::uint32_t i = 0; i < sockets_.size(); ++i) {
      if(sockets_[i]->open) {
        close_socket(i);
      }
    }
  }

  bool native_process::is_alive() const {
    return is_alive_;
  }

  std::uint64_t native_process::id() const {
    return id_;
  }

  std::size_t native_process::socket_count() const {
    return sockets_.size();
  }

}
