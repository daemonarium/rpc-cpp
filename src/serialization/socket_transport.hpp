#pragma once

#include <cstdint>

extern "C" {
#include <socket/socket.h>
#include <io/circular_buffer.h>
}

#ifdef __EMSCRIPTEN__

extern "C" {
  __attribute__((import_module("waos"), import_name("waos_socket_flush")))
  void waos_socket_flush(std::uint32_t socket);

  __attribute__((import_module("waos"), import_name("waos_socket_input_drain")))
  void waos_socket_input_drain(std::uint32_t socket);
}

#else

extern "C" {
  void waos_socket_flush(std::uint32_t socket);
  void waos_socket_input_drain(std::uint32_t socket);
}

#endif

#include <native/native_process.hpp>

namespace serialization {

  // Transport policy for socket_message_framing.
  // Required interface (static polymorphism — no vtable, no std::function):
  //   circular_buffer& input_buffer()
  //   circular_buffer& output_buffer()
  //   void flush()
  //   void drain()

  // Default: global socket.c table + waos_socket_flush / waos_socket_input_drain.
  class socket_c_transport {
    public:
      explicit socket_c_transport(std::uint32_t socket_id)
        : socket_id_(socket_id),
          socket_(get_socket_info(socket_id)) {}

      circular_buffer& input_buffer() { return socket_->input_buffer; }
      circular_buffer& output_buffer() { return socket_->output_buffer; }
      void flush() { waos_socket_flush(socket_id_); }
      void drain() { waos_socket_input_drain(socket_id_); }

    private:
      std::uint32_t socket_id_;
      struct socket_info* socket_;
  };

  // native_process-owned circular_buffer pair.
  class native_transport {
    public:
      native_transport(waos::native::native_process& proc, std::uint32_t socket)
        : proc_(proc), socket_(socket) {}

      circular_buffer& input_buffer() { return proc_.input_buffer(socket_); }
      circular_buffer& output_buffer() { return proc_.output_buffer(socket_); }
      void flush() { proc_.flush(socket_); }
      void drain() { proc_.drain(socket_); }

    private:
      waos::native::native_process& proc_;
      std::uint32_t socket_;
  };

  // Pure in-memory buffers (tests without socket.c). flush/drain are no-ops.
  class buffer_transport {
    public:
      buffer_transport(circular_buffer* in, circular_buffer* out)
        : in_(in), out_(out) {}

      circular_buffer& input_buffer() { return *in_; }
      circular_buffer& output_buffer() { return *out_; }
      void flush() {}
      void drain() {}

    private:
      circular_buffer* in_;
      circular_buffer* out_;
  };

} // namespace serialization
