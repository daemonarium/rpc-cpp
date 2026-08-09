#include <boost/ut.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <string_view>

#include "test_socket_utils.hpp"
#include "serialization/socket_message_channel.hpp"
#include "serialization/flat_view.hpp"
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

  "flat zero-copy channel send and receive simple message"_test = [] {
    socket_loopback_pair sockets;

    flat_zero_copy_socket_message_channel ch0(sockets.s0);
    flat_zero_copy_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch1.register_message_type<ping>();

    bool received = false;
    std::uint64_t received_seq = 0;

    ch1.on_message<ping>(std::function<void(flat_view<ping>&)>([&](flat_view<ping>& v) {
      received = true;
      received_seq = v.get<^^ping::seq>();
    }));

    ch0.send_message(ping{42});
    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    expect(received == true);
    expect(received_seq == 42_u);
  };

  "flat zero-copy channel send message with string field"_test = [] {
    socket_loopback_pair sockets;

    flat_zero_copy_socket_message_channel ch0(sockets.s0);
    flat_zero_copy_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<pong>();
    ch1.register_message_type<pong>();

    bool received = false;
    std::uint64_t received_seq = 0;
    std::string received_data;

    ch1.on_message<pong>(std::function<void(flat_view<pong>&)>([&](flat_view<pong>& v) {
      received = true;
      received_seq = v.get<^^pong::seq>();
      received_data = std::string(std::string_view(v.get<^^pong::data>()));
    }));

    ch0.send_message(pong{1, "hello world"});
    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    expect(received == true);
    expect(received_seq == 1_u);
    expect(received_data == std::string("hello world"));
  };

  "flat zero-copy channel multiple messages in sequence"_test = [] {
    socket_loopback_pair sockets;

    flat_zero_copy_socket_message_channel ch0(sockets.s0);
    flat_zero_copy_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch1.register_message_type<ping>();

    std::vector<std::uint64_t> received_seqs;

    ch1.on_message<ping>(std::function<void(flat_view<ping>&)>([&](flat_view<ping>& v) {
      received_seqs.push_back(v.get<^^ping::seq>());
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

  "flat zero-copy channel mixed message types"_test = [] {
    socket_loopback_pair sockets;

    flat_zero_copy_socket_message_channel ch0(sockets.s0);
    flat_zero_copy_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch0.register_message_type<pong>();
    ch1.register_message_type<ping>();
    ch1.register_message_type<pong>();

    std::vector<std::uint64_t> ping_seqs;
    std::vector<std::string> pong_data;

    ch1.on_message<ping>(std::function<void(flat_view<ping>&)>([&](flat_view<ping>& v) {
      ping_seqs.push_back(v.get<^^ping::seq>());
    }));
    ch1.on_message<pong>(std::function<void(flat_view<pong>&)>([&](flat_view<pong>& v) {
      pong_data.push_back(std::string(std::string_view(v.get<^^pong::data>())));
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

  "flat zero-copy channel unknown type index throws"_test = [] {
    socket_loopback_pair sockets;

    flat_zero_copy_socket_message_channel ch0(sockets.s0);
    flat_zero_copy_socket_message_channel ch1(sockets.s1);

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

  "flat zero-copy channel bidirectional communication"_test = [] {
    socket_loopback_pair sockets;

    flat_zero_copy_socket_message_channel ch0(sockets.s0);
    flat_zero_copy_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch1.register_message_type<ping>();

    std::uint64_t ch0_received = 0;
    std::uint64_t ch1_received = 0;

    ch0.on_message<ping>(std::function<void(flat_view<ping>&)>([&](flat_view<ping>& v) {
      ch0_received = v.get<^^ping::seq>();
    }));
    ch1.on_message<ping>(std::function<void(flat_view<ping>&)>([&](flat_view<ping>& v) {
      ch1_received = v.get<^^ping::seq>();
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

  "flat zero-copy channel view lifetime only during dispatch"_test = [] {
    // The flat_view is valid only inside the handler (before framing's
    // skip_read(msg_len) consumes the body). After handle_data returns, the
    // session is released. This test documents that contract: we copy out
    // the data we need inside the handler, and the channel stays usable
    // for subsequent messages.
    socket_loopback_pair sockets;

    flat_zero_copy_socket_message_channel ch0(sockets.s0);
    flat_zero_copy_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<pong>();
    ch1.register_message_type<pong>();

    std::string first_data;
    std::string second_data;

    ch1.on_message<pong>(std::function<void(flat_view<pong>&)>([&](flat_view<pong>& v) {
      // Copy data out — view is not valid after handler returns.
      std::string s = std::string(std::string_view(v.get<^^pong::data>()));
      if(first_data.empty()) first_data = std::move(s);
      else second_data = std::move(s);
    }));

    ch0.send_message(pong{1, "first"});
    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    ch0.send_message(pong{2, "second"});
    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    expect(first_data == std::string("first"));
    expect(second_data == std::string("second"));
  };

  "flat zero-copy channel send message with payload"_test = [] {
    socket_loopback_pair sockets;

    flat_zero_copy_socket_message_channel ch0(sockets.s0);
    flat_zero_copy_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch1.register_message_type<ping>();

    bool received = false;
    std::uint64_t received_seq = 0;
    size_t received_payload_size = 0;
    std::uint64_t payload_value = 0;

    ch1.on_message<ping>(std::function<void(flat_view<ping>&, size_t)>(
      [&](flat_view<ping>& v, size_t payload_size) {
        received = true;
        received_seq = v.get<^^ping::seq>();
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
    expect(received_payload_size == 8_u);
    expect(payload_value == 12345_u);
  };

  "flat zero-copy channel unread payload skipped"_test = [] {
    socket_loopback_pair sockets;

    flat_zero_copy_socket_message_channel ch0(sockets.s0);
    flat_zero_copy_socket_message_channel ch1(sockets.s1);

    ch0.register_message_type<ping>();
    ch1.register_message_type<ping>();

    std::vector<std::uint64_t> received_seqs;

    // Handler takes only flat_view — payload is ignored, framing skips it.
    ch1.on_message<ping>(std::function<void(flat_view<ping>&)>([&](flat_view<ping>& v) {
      received_seqs.push_back(v.get<^^ping::seq>());
    }));

    // First message: with payload (unread by handler)
    auto& w = ch0.begin_send(ping{1});
    w.write_u64(99999);
    w.write_rest();
    ch0.commit_send();

    // Second message: simple (no payload)
    ch0.send_message(ping{2});

    sockets.transfer(sockets.s0, sockets.s1);
    ch1.handle_data();

    expect(received_seqs.size() == 2_u);
    expect(received_seqs[0] == 1_u);
    expect(received_seqs[1] == 2_u);
  };
}
