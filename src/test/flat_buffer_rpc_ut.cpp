#include <boost/ut.hpp>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "serialization/socket_rpc_client.hpp"
#include "serialization/socket_rpc_server.hpp"
#include "serialization/socket_transport.hpp"
#include "serialization/promise.hpp"

extern "C" {
#include <io/circular_buffer.h>
}

using namespace serialization;

namespace {

  class TestService {
    public:
      static std::int32_t add(std::int32_t a, std::int32_t b) {
        return a + b;
      }

      static std::string greet(std::string name) {
        return "hello " + name;
      }

      static void noop() {}
  };

  struct buffer_pair {
    circular_buffer a_to_b{};
    circular_buffer b_to_a{};

    explicit buffer_pair(std::size_t buf_size = 4096) {
      circular_buffer_init(&a_to_b, buf_size, nullptr);
      circular_buffer_init(&b_to_a, buf_size, nullptr);
    }

    ~buffer_pair() {
      circular_buffer_deinit(&a_to_b);
      circular_buffer_deinit(&b_to_a);
    }

    buffer_pair(const buffer_pair&) = delete;
    buffer_pair& operator=(const buffer_pair&) = delete;
  };

  // Shared-buffer loopback: client.out == server.in, server.out == client.in.
  // After client.send, data is already in server.in → server.handle_data() →
  // response already in client.in → client.handle_data().
  void rpc_roundtrip_shared(
      flat_buffer_socket_rpc_client& client,
      flat_buffer_socket_rpc_server& server) {
    server.handle_data();
    client.handle_data();
  }

}

int main() {
  using namespace boost::ut;

  "flat buffer rpc call with int return"_test = [] {
    buffer_pair buffers;
    // Shared loopback: client.out == server.in, server.out == client.in
    flat_buffer_socket_rpc_server server(buffer_transport(&buffers.a_to_b, &buffers.b_to_a));
    flat_buffer_socket_rpc_client client(buffer_transport(&buffers.b_to_a, &buffers.a_to_b));

    server.register_basic_types();
    client.register_basic_types();

    TestService svc;
    server.register_recursive(svc);

    bool resolved = false;
    std::int32_t result = 0;

    client.call<std::int32_t>(0, std::int32_t(3), std::int32_t(5))->on_resolved([&](std::int32_t r) {
      resolved = true;
      result = r;
    });

    rpc_roundtrip_shared(client, server);

    expect(resolved == true);
    expect(result == 8_i);
  };

  "flat buffer rpc call with string return"_test = [] {
    buffer_pair buffers;
    flat_buffer_socket_rpc_server server(buffer_transport(&buffers.a_to_b, &buffers.b_to_a));
    flat_buffer_socket_rpc_client client(buffer_transport(&buffers.b_to_a, &buffers.a_to_b));

    server.register_basic_types();
    client.register_basic_types();

    TestService svc;
    server.register_recursive(svc);

    bool resolved = false;
    std::string result;

    client.call<std::string>(1, std::string("world"))->on_resolved([&](std::string r) {
      resolved = true;
      result = r;
    });

    rpc_roundtrip_shared(client, server);

    expect(resolved == true);
    expect(result == std::string("hello world"));
  };

  "flat buffer rpc void call"_test = [] {
    buffer_pair buffers;
    flat_buffer_socket_rpc_server server(buffer_transport(&buffers.a_to_b, &buffers.b_to_a));
    flat_buffer_socket_rpc_client client(buffer_transport(&buffers.b_to_a, &buffers.a_to_b));

    server.register_basic_types();
    client.register_basic_types();

    TestService svc;
    server.register_recursive(svc);

    bool resolved = false;

    client.call<void>(2)->on_resolved([&]() {
      resolved = true;
    });

    rpc_roundtrip_shared(client, server);

    expect(resolved == true);
  };

  "flat buffer rpc multiple sequential calls"_test = [] {
    buffer_pair buffers;
    flat_buffer_socket_rpc_server server(buffer_transport(&buffers.a_to_b, &buffers.b_to_a));
    flat_buffer_socket_rpc_client client(buffer_transport(&buffers.b_to_a, &buffers.a_to_b));

    server.register_basic_types();
    client.register_basic_types();

    TestService svc;
    server.register_recursive(svc);

    for(int i = 0; i < 10; ++i) {
      bool resolved = false;
      std::int32_t result = 0;

      client.call<std::int32_t>(0, std::int32_t(i), std::int32_t(i * 10))->on_resolved([&](std::int32_t r) {
        resolved = true;
        result = r;
      });

      rpc_roundtrip_shared(client, server);

      expect(resolved == true);
      expect(result == std::int32_t(i + i * 10));
    }
  };
}
