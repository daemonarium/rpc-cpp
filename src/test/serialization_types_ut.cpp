#include <boost/ut.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>

// type_registry.hpp must be included before rpc.hpp to resolve the circular dependency
#include "serialization/type_registry.hpp"
#include "serialization/stream_writer.hpp"
#include "serialization/stream_reader.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"
#include "serialization/rpc.hpp"

using namespace serialization;

namespace {

  struct [[=serialization::message()]] test_point {
    std::int32_t x;
    std::int32_t y;
  };

  struct [[=serialization::message()]] test_person {
    std::string name;
    std::uint32_t age;
  };

  struct [[=serialization::message()]] test_nested {
    test_point origin;
    std::vector<test_point> points;
  };

  template <typename T>
  T roundtrip(const T& value) {
    std::ostringstream os;
    stream_writer w(os);
    serialize(w, value);
    w.write_rest();

    std::istringstream is(os.str());
    stream_reader r(is);
    T result{};
    deserialize(r, result);
    r.read_rest();
    return result;
  }
}

int main() {
  using namespace boost::ut;

  "types serialize test_point roundtrip"_test = [] {
    test_point p{10, -20};
    auto result = roundtrip(p);
    expect(result.x == 10_i);
    expect(result.y == -20_i);
  };

  "types serialize test_point zero"_test = [] {
    test_point p{0, 0};
    auto result = roundtrip(p);
    expect(result.x == 0_i);
    expect(result.y == 0_i);
  };

  "types serialize test_person roundtrip"_test = [] {
    test_person p{"Jan Kowalski", 42};
    auto result = roundtrip(p);
    expect(result.name == std::string("Jan Kowalski"));
    expect(result.age == 42_u);
  };

  "types serialize test_person empty name"_test = [] {
    test_person p{"", 0};
    auto result = roundtrip(p);
    expect(result.name == std::string(""));
    expect(result.age == 0_u);
  };

  "types serialize test_nested roundtrip"_test = [] {
    test_nested n{
      {5, 10},
      {{1, 2}, {3, 4}, {-1, -2}}
    };
    auto result = roundtrip(n);
    expect(result.origin.x == 5_i);
    expect(result.origin.y == 10_i);
    expect(result.points.size() == 3_u);
    expect(result.points[0].x == 1_i);
    expect(result.points[0].y == 2_i);
    expect(result.points[1].x == 3_i);
    expect(result.points[1].y == 4_i);
    expect(result.points[2].x == -1_i);
    expect(result.points[2].y == -2_i);
  };

  "types serialize test_nested empty points"_test = [] {
    test_nested n{{0, 0}, {}};
    auto result = roundtrip(n);
    expect(result.origin.x == 0_i);
    expect(result.origin.y == 0_i);
    expect(result.points.empty());
  };

  "types serialize rpc_request roundtrip"_test = [] {
    rpc_request req{42, 7};
    auto result = roundtrip(req);
    expect(result.request_id == 42_u);
    expect(result.function_id == 7_u);
  };

  "types serialize rpc_request large ids"_test = [] {
    rpc_request req{0xFFFFFFFFFFFFFFFFULL, 0x123456789ABCDEFULL};
    auto result = roundtrip(req);
    expect(result.request_id == 0xFFFFFFFFFFFFFFFFULL);
    expect(result.function_id == 0x123456789ABCDEFULL);
  };

  "types serialize rpc_response roundtrip"_test = [] {
    rpc_response resp{42, true};
    auto result = roundtrip(resp);
    expect(result.request_id == 42_u);
    expect(result.success == true);
  };

  "types serialize rpc_response failure"_test = [] {
    rpc_response resp{99, false};
    auto result = roundtrip(resp);
    expect(result.request_id == 99_u);
    expect(result.success == false);
  };

  "types serialize rpc_error roundtrip"_test = [] {
    rpc_error err{42, false, "something failed"};
    auto result = roundtrip(err);
    expect(result.request_id == 42_u);
    expect(result.success == false);
    expect(result.error == std::string("something failed"));
  };

  "types serialize rpc_error empty message"_test = [] {
    rpc_error err{0, false, ""};
    auto result = roundtrip(err);
    expect(result.request_id == 0_u);
    expect(result.success == false);
    expect(result.error == std::string(""));
  };
}
