#include <boost/ut.hpp>

#include <memory>
#include <sstream>
#include <string>

#include "observable/observable.hpp"
#include "serialization/annotations.hpp"
#include "serialization/flat_materialized_codec.hpp"
#include "serialization/socket_rpc_server.hpp"
#include "serialization/type_description.hpp"
#include "serialization/type_registry.hpp"
#include "test_socket_utils.hpp"

using namespace boost::ut;
using namespace serialization;

namespace {

  struct ServiceBase {
    virtual ~ServiceBase() = default;
    virtual void ping() = 0;
    std::shared_ptr<waos::observable::Observable<waos::observable::BinaryEntry>> live;
  };

  struct Service : ServiceBase {
    void ping() override {}

    [[=serialization::observable()]]
    static std::shared_ptr<waos::observable::Observable<waos::observable::BinaryEntry>> watch() {
      return {};
    }
  };

  using flat_socket_rpc_server = socket_rpc_server<flat_materialized_codec>;

}

int main() {
  "abstract base and Observable pointer register without descending"_test = [] {
    socket_loopback_pair sockets;
    flat_socket_rpc_server server(sockets.s0);
    Service svc;
    server.register_recursive(svc);

    std::shared_ptr<waos::observable::Observable<waos::observable::BinaryEntry>> ptr;
    server.register_recursive(ptr);

    type_registry registry(true);
    auto observablePtr = type_description::create(ptr, registry);
    expect(observablePtr->getKind() == type_description::Kind::Pointer);
    expect(observablePtr->getItem() == nullptr);

    auto type = type_description::create(svc, registry);
    expect(type != nullptr);
    expect(type->getKind() == type_description::Kind::Object);

    std::shared_ptr<type_description> method;
    for(const auto& member : type->getMembers()) {
      if(member.name.ends_with(".watch")) method = member.type;
    }
    expect(method != nullptr);
    expect(method->getKind() == type_description::Kind::Function);
    expect(method->getMember("stream") != nullptr);

    std::ostringstream printed;
    printed << *method;
    auto text = printed.str();
    expect(text.find("stream") != std::string::npos);
    expect(text.find("return type void") != std::string::npos);
  };
}
