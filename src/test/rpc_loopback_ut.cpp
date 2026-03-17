#include <boost/ut.hpp>
#include <sstream>
#include <string>
#include <cstdint>
#include <memory>
#include <stdexcept>

// type_registry.hpp must be included before rpc.hpp to resolve the circular dependency
#include "serialization/type_registry.hpp"
#include "serialization/stream_writer.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/rpc_server.hpp"
#include "serialization/rpc_client.hpp"
#include "serialization/promise.hpp"

using namespace serialization;

namespace {

  // Test RPC service — static methods are discovered via reflection
  class TestService {
    public:
      static std::int32_t add(std::int32_t a, std::int32_t b) {
        return a + b;
      }

      static std::string greet(std::string name) {
        return "hello " + name;
      }

      static void noop() {
        // does nothing
      }

      static std::int32_t failing() {
        throw std::runtime_error("oops");
      }

      static std::string echo(std::string s) {
        return s;
      }
  };

  // Loopback: client writes → server reads & responds → client reads response
  struct test_loopback {
    using server_type = rpc_server<stream_reader, stream_writer>;
    using client_type = rpc_client<stream_reader, stream_writer>;

    std::ostringstream output_stream;
    stream_writer writer;
    std::istringstream input_stream;
    stream_reader reader;

    server_type server;
    std::shared_ptr<client_type> client;

    test_loopback()
      : writer(output_stream), reader(input_stream),
        server(),
        client(std::make_shared<client_type>(writer, [this]() { }, [this]() { this->handle_call(); }))
    {
      writer.register_serialize_exception_function<std::runtime_error>();
      reader.register_deserialize_exception_function<std::runtime_error>();
    }

    void handle_call() {
      std::string call = output_stream.str();
      input_stream.str(call);
      input_stream.seekg(0);
      output_stream.str("");
      output_stream.seekp(0);
      server.handle_call(reader, writer);
      std::string response = output_stream.str();
      input_stream.str(response);
      input_stream.seekg(0);
      output_stream.str("");
      output_stream.seekp(0);
      client->handle_response(reader);
    }

    void register_service() {
      TestService svc;
      server_type::register_recursive(svc, server);
    }
  };
}

int main() {
  using namespace boost::ut;

  // function_id assignment order matches class member declaration order:
  // 0=add, 1=greet, 2=noop, 3=failing, 4=echo

  "rpc loopback call with int return"_test = [] {
    test_loopback lb;
    lb.register_service();

    bool resolved = false;
    std::int32_t result = 0;

    lb.client->call<std::int32_t>(0, std::int32_t(3), std::int32_t(5))->on_resolved([&](std::int32_t r) {
      resolved = true;
      result = r;
    });

    expect(resolved == true);
    expect(result == 8_i);
  };

  "rpc loopback call with string return"_test = [] {
    test_loopback lb;
    lb.register_service();

    bool resolved = false;
    std::string result;

    lb.client->call<std::string>(1, std::string("world"))->on_resolved([&](std::string r) {
      resolved = true;
      result = r;
    });

    expect(resolved == true);
    expect(result == std::string("hello world"));
  };

  "rpc loopback void return"_test = [] {
    test_loopback lb;
    lb.register_service();

    bool resolved = false;

    lb.client->call<void>(2)->on_resolved([&]() {
      resolved = true;
    });

    expect(resolved == true);
  };

  "rpc loopback server exception"_test = [] {
    test_loopback lb;
    lb.register_service();

    bool rejected = false;
    std::string error_msg;

    // In synchronous loopback the promise is rejected before on_rejected is registered,
    // so promise<T>::reject() re-throws. Catch it here.
    try {
      lb.client->call<std::int32_t>(3)->on_rejected([&](std::exception_ptr ep) {
        rejected = true;
        try {
          std::rethrow_exception(ep);
        } catch(const std::runtime_error& e) {
          error_msg = e.what();
        }
      });
    } catch(const std::runtime_error& e) {
      rejected = true;
      error_msg = e.what();
    }

    expect(rejected == true);
    expect(error_msg == std::string("oops"));
  };

  "rpc loopback invalid function id"_test = [] {
    test_loopback lb;
    lb.register_service();

    bool rejected = false;

    try {
      lb.client->call<std::int32_t>(999)->on_rejected([&](std::exception_ptr) {
        rejected = true;
      });
    } catch(...) {
      rejected = true;
    }

    expect(rejected == true);
  };

  "rpc loopback multiple sequential calls"_test = [] {
    test_loopback lb;
    lb.register_service();

    for(int i = 0; i < 10; ++i) {
      bool resolved = false;
      std::int32_t result = 0;

      lb.client->call<std::int32_t>(0, std::int32_t(i), std::int32_t(i * 10))->on_resolved([&](std::int32_t r) {
        resolved = true;
        result = r;
      });

      expect(resolved == true);
      expect(result == std::int32_t(i + i * 10));
    }
  };

  "rpc loopback large string data"_test = [] {
    test_loopback lb;
    lb.register_service();

    std::string large(1024, 'A');
    bool resolved = false;
    std::string result;

    lb.client->call<std::string>(4, large)->on_resolved([&](std::string r) {
      resolved = true;
      result = r;
    });

    expect(resolved == true);
    expect(result == large);
  };
}
