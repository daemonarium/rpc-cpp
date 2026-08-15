#include <boost/ut.hpp>
#include <string>
#include <cstdint>
#include <memory>
#include <stdexcept>

#include "test_socket_utils.hpp"
#include "serialization/socket_rpc_client.hpp"
#include "serialization/socket_rpc_server.hpp"
#include "serialization/flat_materialized_codec.hpp"
#include "serialization/promise.hpp"

using namespace serialization;

namespace {

  struct typed_error : public std::exception {
    typed_error() = default;
    explicit typed_error(const std::string& msg) : msg_(msg) {}
    const char* what() const noexcept override { return msg_.c_str(); }
    std::string msg_;
  };

  // Test RPC service with static methods (discovered via reflection)
  class TestService {
    public:
      static std::int32_t add(std::int32_t a, std::int32_t b) {
        return a + b;
      }

      static std::string greet(std::string name) {
        return "hello " + name;
      }

      static void noop() {
      }

      static std::int32_t failing() {
        throw std::runtime_error("oops");
      }

      static std::int32_t failing_typed() {
        throw typed_error("typed oops");
      }

      static std::string echo(std::string s) {
        return s;
      }
  };

  // function_id assignment order: 0=add, 1=greet, 2=noop, 3=failing, 4=failing_typed, 5=echo

  struct [[=serialization::message()]] custom_msg {
    std::uint64_t value;
    std::string text;
  };

  using flat_socket_rpc_server = socket_rpc_server<flat_materialized_codec>;
  using flat_socket_rpc_client = socket_rpc_client<flat_materialized_codec>;

  // Helper: execute one RPC call roundtrip
  void rpc_roundtrip(socket_loopback_pair& sockets,
                     flat_socket_rpc_client& client,
                     flat_socket_rpc_server& server) {
    // client → server
    sockets.transfer(sockets.s1, sockets.s0);
    server.handle_data();
    // server → client
    sockets.transfer(sockets.s0, sockets.s1);
    client.handle_data();
  }
}

int main() {
  using namespace boost::ut;

  "flat socket rpc call with int return"_test = [] {
    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);

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

    rpc_roundtrip(sockets, client, server);

    expect(resolved == true);
    expect(result == 8_i);
  };

  "flat socket rpc call with string return"_test = [] {
    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);

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

    rpc_roundtrip(sockets, client, server);

    expect(resolved == true);
    expect(result == std::string("hello world"));
  };

  "flat socket rpc void call"_test = [] {
    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);

    server.register_basic_types();
    client.register_basic_types();

    TestService svc;
    server.register_recursive(svc);

    bool resolved = false;

    client.call<void>(2)->on_resolved([&]() {
      resolved = true;
    });

    rpc_roundtrip(sockets, client, server);

    expect(resolved == true);
  };

  "flat socket rpc error propagation"_test = [] {
    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);

    server.register_basic_types();
    client.register_basic_types();

    TestService svc;
    server.register_recursive(svc);
    server.channel().register_exception_type<std::runtime_error>();
    client.channel().register_exception_type<std::runtime_error>();

    bool rejected = false;
    std::string error_msg;

    client.call<std::int32_t>(3)->on_rejected([&](std::exception_ptr ep) {
      rejected = true;
      try {
        std::rethrow_exception(ep);
      } catch(const std::runtime_error& e) {
        error_msg = e.what();
      }
    });

    rpc_roundtrip(sockets, client, server);

    expect(rejected == true);
    expect(error_msg == std::string("oops"));
  };

  "flat socket rpc typed exception propagation"_test = [] {
    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);

    server.register_basic_types();
    client.register_basic_types();

    TestService svc;
    server.register_recursive(svc);

    server.channel().register_exception_type<typed_error>();
    client.channel().register_exception_type<typed_error>();

    bool rejected = false;
    std::string error_msg;
    bool caught_typed = false;

    client.call<std::int32_t>(4)->on_rejected([&](std::exception_ptr ep) {
      rejected = true;
      try {
        std::rethrow_exception(ep);
      } catch(const typed_error& e) {
        caught_typed = true;
        error_msg = e.what();
      }
    });

    rpc_roundtrip(sockets, client, server);

    expect(rejected == true);
    expect(caught_typed == true);
    expect(error_msg == std::string("typed oops"));
  };

  "flat socket rpc invalid function id"_test = [] {
    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);

    server.register_basic_types();
    client.register_basic_types();

    TestService svc;
    server.register_recursive(svc);

    bool rejected = false;

    client.call<std::int32_t>(999)->on_rejected([&](std::exception_ptr) {
      rejected = true;
    });

    rpc_roundtrip(sockets, client, server);

    expect(rejected == true);
  };

  "flat socket rpc multiple sequential calls"_test = [] {
    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);

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

      rpc_roundtrip(sockets, client, server);

      expect(resolved == true);
      expect(result == std::int32_t(i + i * 10));
    }
  };

  "flat socket rpc large string data"_test = [] {
    socket_loopback_pair sockets(16384);
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);

    server.register_basic_types();
    client.register_basic_types();

    TestService svc;
    server.register_recursive(svc);

    std::string large(1024, 'B');
    bool resolved = false;
    std::string result;

    client.call<std::string>(5, large)->on_resolved([&](std::string r) {
      resolved = true;
      result = r;
    });

    rpc_roundtrip(sockets, client, server);

    expect(resolved == true);
    expect(result == large);
  };

  "flat socket rpc mixed with custom messages"_test = [] {
    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    flat_socket_rpc_client client(sockets.s1);

    server.register_basic_types();
    client.register_basic_types();

    TestService svc;
    server.register_recursive(svc);

    server.register_message_type<custom_msg>();
    client.register_message_type<custom_msg>();

    bool custom_received = false;
    std::uint64_t custom_value = 0;
    std::string custom_text;

    server.on_message<custom_msg>(std::function<void(custom_msg&)>([&](custom_msg& msg) {
      custom_received = true;
      custom_value = msg.value;
      custom_text = msg.text;
    }));

    client.send_message(custom_msg{42, "custom hello"});

    bool rpc_resolved = false;
    std::int32_t rpc_result = 0;
    client.call<std::int32_t>(0, std::int32_t(10), std::int32_t(20))->on_resolved([&](std::int32_t r) {
      rpc_resolved = true;
      rpc_result = r;
    });

    rpc_roundtrip(sockets, client, server);

    expect(custom_received == true);
    expect(custom_value == 42_u);
    expect(custom_text == std::string("custom hello"));
    expect(rpc_resolved == true);
    expect(rpc_result == 30_i);
  };
}
