#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

namespace waos::native {

  struct socket_state {
    circular_buffer input_buffer{};
    circular_buffer output_buffer{};
    std::function<void()> data_handler;
    std::function<void()> drain_handler;
    bool open = false;

    socket_state() {
      input_buffer.data = nullptr;
      input_buffer.new_length = nullptr;
      output_buffer.data = nullptr;
      output_buffer.new_length = nullptr;
    }

    socket_state(const socket_state&) = delete;
    socket_state& operator=(const socket_state&) = delete;
    socket_state(socket_state&&) = delete;
    socket_state& operator=(socket_state&&) = delete;
  };

  // C++ host-side socket layer (mirror of TS LocalProcess).
  // Owns per-socket circular_buffer pairs. No RPC / registry — those belong
  // on concrete services (e.g. db_native_process).
  class native_process {
    public:
      native_process(std::uint64_t id, std::size_t socket_count);
      ~native_process();

      native_process(const native_process&) = delete;
      native_process& operator=(const native_process&) = delete;

      // Socket lifecycle
      void open_socket(std::uint32_t index, std::size_t buf_size);
      void close_socket(std::uint32_t index);

      // Inbound: kernel pump calls these
      void handle_data(std::uint32_t socket);
      void handle_drain(std::uint32_t socket);

      // Outbound: service calls these after writing to output_buffer
      void flush(std::uint32_t socket);
      void drain(std::uint32_t socket);

      // Handler registration (generic, no RPC assumption)
      void register_socket_handler(
          std::uint32_t socket,
          std::function<void()> data,
          std::function<void()> drain = {});
      void unregister_socket_handler(std::uint32_t socket);

      // Buffer access (for handlers / rpc_server)
      circular_buffer& input_buffer(std::uint32_t socket);
      circular_buffer& output_buffer(std::uint32_t socket);

      // Kernel pump seam (host sets: Node TSFN, pure C++ kernel direct)
      void set_flush_callback(std::function<void(std::uint32_t)> cb);
      void set_drain_callback(std::function<void(std::uint32_t)> cb);

      // Lifecycle
      void kill(std::optional<std::string> error = std::nullopt);
      bool is_alive() const;
      std::uint64_t id() const;
      std::size_t socket_count() const;

    private:
      socket_state& require_socket_(std::uint32_t socket);
      socket_state& require_open_socket_(std::uint32_t socket);

      std::uint64_t id_;
      std::vector<std::unique_ptr<socket_state>> sockets_;
      std::function<void(std::uint32_t)> flush_cb_;
      std::function<void(std::uint32_t)> drain_cb_;
      bool is_alive_ = true;
  };

}
