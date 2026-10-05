#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include <socket/socket.h>
#include <io/circular_buffer.h>
}

struct socket_loopback_pair {
  uint32_t s0, s1;

  socket_loopback_pair(size_t buf_size = 4096) {
    sockets_init(2);

    struct socket_options opts = {};
    opts.input_buffer_size = buf_size;
    opts.output_buffer_size = buf_size;
    opts.flush_threshold = buf_size;
    opts.data_event_threshold = buf_size;
    opts.drain_event_threshold = buf_size;
    opts.flags = 0;
    opts.input_new_length_callback = nullptr;
    opts.output_new_length_callback = nullptr;
    opts.data_event_callback = nullptr;
    opts.drain_event_callback = nullptr;

    s0 = socket_open(0, &opts);
    s1 = socket_open(1, &opts);
  }

  ~socket_loopback_pair() {
    socket_close(s0);
    socket_close(s1);
    sockets_done();
  }

  // Copy all data from socket `from`'s output_buffer to socket `to`'s input_buffer
  void transfer(uint32_t from, uint32_t to) {
    struct socket_info* src = get_socket_info(from);
    struct socket_info* dst = get_socket_info(to);
    uint8_t tmp[4096];
    for(;;) {
      size_t n = circular_buffer_read(&src->output_buffer, tmp, sizeof(tmp));
      if(n == 0) break;
      size_t off = 0;
      while(off < n) {
        size_t m = circular_buffer_write(&dst->input_buffer, tmp + off, n - off);
        off += m;
      }
    }
  }

  // Transfer in both directions
  void transfer_both() {
    transfer(s0, s1);
    transfer(s1, s0);
  }
};

inline std::size_t socket_output_readable(std::uint32_t sock) {
  return circular_buffer_readable_bytes(&get_socket_info(sock)->output_buffer);
}
