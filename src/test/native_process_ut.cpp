#include "native/native_process.hpp"

#include <boost/ut.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

using namespace boost::ut;
using waos::native::native_process;

namespace {

  // Peek/consume only what the destination can accept (no auto-grow on write).
  std::size_t transfer_partial(
      native_process& from, std::uint32_t from_sock,
      native_process& to, std::uint32_t to_sock) {
    auto& src = from.output_buffer(from_sock);
    auto& dst = to.input_buffer(to_sock);
    std::uint8_t tmp[256];
    std::size_t total = 0;
    for(;;) {
      std::size_t available = circular_buffer_readable_bytes(&src);
      if(available == 0) {
        break;
      }
      std::size_t writable = circular_buffer_writable_bytes(&dst);
      if(writable == 0) {
        break;
      }
      std::size_t chunk = available < sizeof(tmp) ? available : sizeof(tmp);
      if(chunk > writable) {
        chunk = writable;
      }
      std::size_t n = circular_buffer_read(&src, tmp, chunk);
      std::size_t written = circular_buffer_write(&dst, tmp, n);
      total += written;
      if(written < n) {
        break;
      }
    }
    return total;
  }

}

int main() {
  "open_socket write and read"_test = [] {
    native_process a(1, 1);
    a.open_socket(0, 1024);
    const char msg[] = "hello";
    expect(eq(
        circular_buffer_write(&a.output_buffer(0), msg, sizeof(msg) - 1),
        sizeof(msg) - 1));
    char buf[16] = {};
    expect(eq(
        circular_buffer_read(&a.output_buffer(0), buf, sizeof(buf)),
        sizeof(msg) - 1));
    expect(eq(std::string(buf, sizeof(msg) - 1), std::string("hello")));
    a.close_socket(0);
  };

  "handle_data fires registered handler"_test = [] {
    native_process a(1, 1);
    a.open_socket(0, 1024);
    bool fired = false;
    a.register_socket_handler(0, [&] { fired = true; });
    const char msg[] = "x";
    circular_buffer_write(&a.input_buffer(0), msg, 1);
    a.handle_data(0);
    expect(fired);
  };

  "echo round-trip between two native_process"_test = [] {
    native_process a(1, 1);
    native_process b(2, 1);
    a.open_socket(0, 1024);
    b.open_socket(0, 1024);

    b.register_socket_handler(0, [&] {
      auto& in = b.input_buffer(0);
      auto& out = b.output_buffer(0);
      std::uint8_t tmp[256];
      for(;;) {
        std::size_t n = circular_buffer_read(&in, tmp, sizeof(tmp));
        if(n == 0) {
          break;
        }
        circular_buffer_write(&out, tmp, n);
      }
      b.flush(0);
    });

    bool a_got = false;
    std::string echoed;
    a.register_socket_handler(0, [&] {
      auto& in = a.input_buffer(0);
      std::uint8_t tmp[256];
      std::size_t n = circular_buffer_read(&in, tmp, sizeof(tmp));
      echoed.assign(reinterpret_cast<char*>(tmp), n);
      a_got = true;
    });

    const char msg[] = "ping";
    circular_buffer_write(&a.output_buffer(0), msg, sizeof(msg) - 1);
    a.flush(0);

    transfer_partial(a, 0, b, 0);
    b.handle_data(0);
    transfer_partial(b, 0, a, 0);
    a.handle_data(0);

    expect(a_got);
    expect(eq(echoed, std::string("ping")));
  };

  "flush and drain callbacks"_test = [] {
    native_process a(1, 1);
    a.open_socket(0, 1024);

    std::uint32_t flushed = 999;
    std::uint32_t drained = 999;
    a.set_flush_callback([&](std::uint32_t s) { flushed = s; });
    a.set_drain_callback([&](std::uint32_t s) { drained = s; });

    a.flush(0);
    a.drain(0);
    expect(eq(flushed, 0u));
    expect(eq(drained, 0u));
  };

  "kill closes sockets and makes handle_data a no-op"_test = [] {
    native_process a(1, 1);
    a.open_socket(0, 1024);
    bool fired = false;
    a.register_socket_handler(0, [&] { fired = true; });

    a.kill(std::string("boom"));
    expect(!a.is_alive());

    a.handle_data(0);
    expect(!fired);

    expect(throws<std::runtime_error>([&] { a.open_socket(0, 1024); }));
  };

  "backpressure: small buffers require chunked transfer"_test = [] {
    native_process a(1, 1);
    native_process b(2, 1);
    a.open_socket(0, 64);
    b.open_socket(0, 64);

    std::vector<std::uint8_t> payload(200);
    for(std::size_t i = 0; i < payload.size(); ++i) {
      payload[i] = static_cast<std::uint8_t>(i & 0xff);
    }

    std::size_t sent = 0;
    std::size_t received = 0;
    std::vector<std::uint8_t> got;
    got.reserve(payload.size());
    std::uint8_t sink[128];
    std::size_t rounds = 0;

    while(received < payload.size()) {
      ++rounds;
      // Fill A's output as much as fits (write does not auto-grow).
      while(sent < payload.size()) {
        std::size_t writable = circular_buffer_writable_bytes(&a.output_buffer(0));
        if(writable == 0) {
          break;
        }
        std::size_t n = payload.size() - sent;
        if(n > writable) {
          n = writable;
        }
        std::size_t w = circular_buffer_write(
            &a.output_buffer(0), payload.data() + sent, n);
        sent += w;
        if(w == 0) {
          break;
        }
      }

      std::size_t moved = transfer_partial(a, 0, b, 0);
      expect(moved > 0u || circular_buffer_readable_bytes(&a.output_buffer(0)) == 0);

      while(circular_buffer_readable_bytes(&b.input_buffer(0)) > 0) {
        std::size_t n = circular_buffer_read(&b.input_buffer(0), sink, sizeof(sink));
        got.insert(got.end(), sink, sink + n);
        received += n;
      }

      expect(rounds < 1000u); // safety
    }

    expect(eq(sent, payload.size()));
    expect(eq(received, payload.size()));
    expect(eq(got, payload));
    // Small buffers must force more than one pump round.
    expect(rounds > 1u);
  };

  "handle_drain fires registered drain handler"_test = [] {
    native_process a(1, 1);
    a.open_socket(0, 1024);
    bool drained = false;
    a.register_socket_handler(0, [] {}, [&] { drained = true; });
    a.handle_drain(0);
    expect(drained);
  };

  return 0;
}
