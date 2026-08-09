#include <boost/ut.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

#include "test_socket_utils.hpp"
#include "serialization/socket_message_channel.hpp"
#include "serialization/serialize.hpp"
#include "serialization/deserialize.hpp"

using namespace serialization;

namespace {

  struct [[=serialization::message()]] ping {
    std::uint64_t seq;
  };

  struct [[=serialization::message()]] pong {
    std::uint64_t seq;
    std::string data;
  };
}

int main() {
  using namespace boost::ut;

  "flat channel send and receive simple message"_test = [] {
    socket_loopback_pair sockets;

    flat_socket_message_channel ch0(sockets.s0);
    flat_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch1.register_message_type<ping>();

    bool received = false;
    std::uint64_t received_seq = 0;

    ch1.on_message<ping>(std::function<void(ping&)>([&](ping& p) {
      received = true;
      received_seq = p.seq;
    }));

    ch0.send_message(ping{42});
    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    expect(received == true);
    expect(received_seq == 42_u);
  };

  "flat channel send message with string field"_test = [] {
    socket_loopback_pair sockets;

    flat_socket_message_channel ch0(sockets.s0);
    flat_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<pong>();
    ch1.register_message_type<pong>();

    bool received = false;
    std::uint64_t received_seq = 0;
    std::string received_data;

    ch1.on_message<pong>(std::function<void(pong&)>([&](pong& p) {
      received = true;
      received_seq = p.seq;
      received_data = p.data;
    }));

    ch0.send_message(pong{1, "hello world"});
    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    expect(received == true);
    expect(received_seq == 1_u);
    expect(received_data == std::string("hello world"));
  };

  "flat channel send message with payload"_test = [] {
    socket_loopback_pair sockets;

    flat_socket_message_channel ch0(sockets.s0);
    flat_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch1.register_message_type<ping>();

    bool received = false;
    std::uint64_t received_seq = 0;
    size_t received_payload_size = 0;
    std::uint64_t payload_value = 0;

    ch1.on_message<ping>(std::function<void(ping&, size_t)>([&](ping& p, size_t payload_size) {
      received = true;
      received_seq = p.seq;
      received_payload_size = payload_size;
      if(payload_size > 0) {
        ch1.reader().read_u64(payload_value);
      }
    }));

    // Send ping with payload
    auto& writer = ch0.begin_send(ping{7});
    writer.write_u64(12345);
    writer.write_rest();
    ch0.commit_send();

    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    expect(received == true);
    expect(received_seq == 7_u);
    expect(received_payload_size > 0_u);
    expect(payload_value == 12345_u);
  };

  "flat channel multiple messages in sequence"_test = [] {
    socket_loopback_pair sockets;

    flat_socket_message_channel ch0(sockets.s0);
    flat_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch1.register_message_type<ping>();

    std::vector<std::uint64_t> received_seqs;

    ch1.on_message<ping>(std::function<void(ping&)>([&](ping& p) {
      received_seqs.push_back(p.seq);
    }));

    ch0.send_message(ping{1});
    ch0.send_message(ping{2});
    ch0.send_message(ping{3});

    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    expect(received_seqs.size() == 3_u);
    expect(received_seqs[0] == 1_u);
    expect(received_seqs[1] == 2_u);
    expect(received_seqs[2] == 3_u);
  };

  "flat channel mixed message types"_test = [] {
    socket_loopback_pair sockets;

    flat_socket_message_channel ch0(sockets.s0);
    flat_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch0.register_message_type<pong>();
    ch1.register_message_type<ping>();
    ch1.register_message_type<pong>();

    std::vector<std::uint64_t> ping_seqs;
    std::vector<std::string> pong_data;

    ch1.on_message<ping>(std::function<void(ping&)>([&](ping& p) {
      ping_seqs.push_back(p.seq);
    }));
    ch1.on_message<pong>(std::function<void(pong&)>([&](pong& p) {
      pong_data.push_back(p.data);
    }));

    ch0.send_message(ping{1});
    ch0.send_message(pong{2, "first"});
    ch0.send_message(ping{3});
    ch0.send_message(pong{4, "second"});

    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    expect(ping_seqs.size() == 2_u);
    expect(ping_seqs[0] == 1_u);
    expect(ping_seqs[1] == 3_u);

    expect(pong_data.size() == 2_u);
    expect(pong_data[0] == std::string("first"));
    expect(pong_data[1] == std::string("second"));
  };

  "flat channel unread payload is skipped"_test = [] {
    socket_loopback_pair sockets;

    flat_socket_message_channel ch0(sockets.s0);
    flat_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch1.register_message_type<ping>();

    std::vector<std::uint64_t> received_seqs;

    // Handler ignores payload — does NOT read from reader
    ch1.on_message<ping>(std::function<void(ping&, size_t)>([&](ping& p, size_t) {
      received_seqs.push_back(p.seq);
    }));

    // First message: with payload (unread)
    auto& w = ch0.begin_send(ping{1});
    w.write_u64(99999);
    w.write_rest();
    ch0.commit_send();

    // Second message: simple
    ch0.send_message(ping{2});

    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    // Both messages should be received correctly
    expect(received_seqs.size() == 2_u);
    expect(received_seqs[0] == 1_u);
    expect(received_seqs[1] == 2_u);
  };

  "flat channel unknown type index throws"_test = [] {
    socket_loopback_pair sockets;

    flat_socket_message_channel ch0(sockets.s0);
    flat_socket_message_channel ch1(sockets.s1);

    // ch0 registers 2 types, ch1 registers only 1
    ch0.register_message_type<ping>();
    ch0.register_message_type<pong>();
    ch1.register_message_type<ping>();
    // ch1 does NOT register pong — index 1 is unknown

    ch0.send_message(pong{1, "test"}); // sends with type index 1

    sockets.transfer(sockets.s0, sockets.s1);

    expect(throws<std::runtime_error>([&] {
      ch1.handle_data();
    }));
  };

  "flat channel bidirectional communication"_test = [] {
    socket_loopback_pair sockets;

    flat_socket_message_channel ch0(sockets.s0);
    flat_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch1.register_message_type<ping>();

    std::uint64_t ch0_received = 0;
    std::uint64_t ch1_received = 0;

    ch0.on_message<ping>(std::function<void(ping&)>([&](ping& p) {
      ch0_received = p.seq;
    }));
    ch1.on_message<ping>(std::function<void(ping&)>([&](ping& p) {
      ch1_received = p.seq;
    }));

    // ch0 sends to ch1
    ch0.send_message(ping{100});
    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    // ch1 sends back to ch0
    ch1.send_message(ping{200});
    sockets.transfer(sockets.s1, sockets.s0);
    ch0.handle_data();

    expect(ch1_received == 100_u);
    expect(ch0_received == 200_u);
  };
}
