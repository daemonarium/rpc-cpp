#include <boost/ut.hpp>
#include <cstring>
#include <vector>
#include <algorithm>

extern "C" {
#include <socket/socket.h>
#include <io/circular_buffer.h>
}

int main() {
  using namespace boost::ut;

  constexpr size_t payload_size = 2048;
  // Socket buffers must hold the full payload since the write loop runs before the transfer
  constexpr size_t buffer_size = payload_size;

  "socket loopback pumps data larger than buffer"_test = [] {
    int r = sockets_init(2);
    expect(r == 0);

    struct socket_options opts = {};
    opts.input_buffer_size = buffer_size;
    opts.output_buffer_size = buffer_size;
    opts.flush_threshold = 1024;
    opts.data_event_threshold = 1024;
    opts.drain_event_threshold = 1024;
    opts.flags = 0;
    opts.input_new_length_callback = nullptr;
    opts.output_new_length_callback = nullptr;
    opts.data_event_callback = nullptr;
    opts.drain_event_callback = nullptr;

    int s0 = socket_open(0, &opts);
    int s1 = socket_open(1, &opts);
    expect(s0 == 0);
    expect(s1 == 1);

    std::vector<std::uint8_t> data(payload_size);
    for (size_t i = 0; i < payload_size; ++i) {
      data[i] = static_cast<std::uint8_t>(i);
    }

    size_t written = 0;
    while (written < payload_size) {
      size_t chunk = std::min(size_t(137), payload_size - written);
      size_t n = socket_write(s0, data.data() + written, chunk);
      written += n;
    }
    expect(written == payload_size);

    struct socket_info* a = get_socket_info(0);
    struct socket_info* b = get_socket_info(1);
    std::uint8_t tmp[256];
    for (;;) {
      size_t n = circular_buffer_read(&a->output_buffer, tmp, sizeof(tmp));
      if (n == 0) break;
      size_t off = 0;
      while (off < n) {
        size_t m = circular_buffer_write(&b->input_buffer, tmp + off, n - off);
        off += m;
      }
    }

    std::vector<std::uint8_t> received(payload_size, 0);
    size_t read_total = 0;
    while (read_total < payload_size) {
      size_t n = socket_read(s1, received.data() + read_total, payload_size - read_total);
      read_total += n;
    }
    expect(read_total == payload_size);
    expect(std::equal(data.begin(), data.end(), received.begin()));

    socket_close(0);
    socket_close(1);
    sockets_done();
  };
}
