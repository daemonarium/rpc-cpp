#include <boost/ut.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

#include "serialization/flat_zero_copy_codec.hpp"
#include "serialization/flat_circular_buffer_writer.hpp"
#include "serialization/flat_buffer_reader.hpp"
#include "serialization/flat_view.hpp"
#include "serialization/rpc.hpp"
#include "serialization/annotations.hpp"

using namespace boost::ut;
using namespace serialization;

namespace {

  struct [[=serialization::message()]] ping {
    std::uint64_t seq;
  };

  struct [[=serialization::message()]] pong {
    std::uint64_t seq;
    std::string data;
  };

  // Helper: codec + a single shared circular buffer (input == output).
  struct codec_buffers {
    struct circular_buffer* cb;
    flat_zero_copy_codec codec;

    explicit codec_buffers(size_t buf_size = 8192)
      : cb(circular_buffer_create(buf_size, nullptr)),
        codec(cb, cb) {}

    ~codec_buffers() { circular_buffer_destroy(cb); }
  };

  // Write a full message into the codec's buffer and commit it. Does NOT
  // consume the length prefix — readable region is [prefix][body]. Use
  // dispatch_frame() to consume the prefix and dispatch. Returns msg_len.
  template <typename T>
  size_t write_frame(flat_zero_copy_codec& codec, struct circular_buffer* cb, const T& msg) {
    uint32_t placeholder = 0;
    circular_buffer_poke(cb, 0, &placeholder, sizeof(placeholder));
    codec.writer().write_offset_ = sizeof(placeholder);
    codec.write_message_header<T>(msg);
    // Stamp payload_size (mirrors framing.commit_send).
    codec.finalize_send();
    uint32_t payload_len = codec.writer().write_offset_ - sizeof(uint32_t);
    circular_buffer_poke(cb, 0, &payload_len, sizeof(payload_len));
    circular_buffer_commit_write(cb, codec.writer().write_offset_);
    return payload_len;
  }

  // Write a message with a raw payload appended after the header. Returns msg_len.
  template <typename T>
  size_t write_frame_with_payload(flat_zero_copy_codec& codec, struct circular_buffer* cb,
                                  const T& msg, std::function<void(flat_circular_buffer_writer&)> write_payload) {
    uint32_t placeholder = 0;
    circular_buffer_poke(cb, 0, &placeholder, sizeof(placeholder));
    codec.writer().write_offset_ = sizeof(placeholder);
    codec.write_message_header<T>(msg);
    write_payload(codec.writer());
    codec.finalize_send();
    uint32_t payload_len = codec.writer().write_offset_ - sizeof(uint32_t);
    circular_buffer_poke(cb, 0, &payload_len, sizeof(payload_len));
    circular_buffer_commit_write(cb, codec.writer().write_offset_);
    return payload_len;
  }

  // Simulate framing dispatch of one message: consume length prefix, reset
  // codec reader, dispatch the body, then consume the body.
  void dispatch_frame(flat_zero_copy_codec& codec, struct circular_buffer* cb, size_t msg_len) {
    circular_buffer_skip_read(cb, sizeof(uint32_t));
    codec.reset_for_dispatch();
    try {
      codec.dispatch(msg_len);
    } catch(...) {
      circular_buffer_skip_read(cb, msg_len);
      throw;
    }
    circular_buffer_skip_read(cb, msg_len);
  }

  void expect_exception_what(std::exception_ptr eptr, const std::string& expected) {
    expect(eptr != nullptr);
    try {
      std::rethrow_exception(eptr);
    } catch(const unknown_exception& e) {
      expect(eq(std::string(e.what()), expected));
    } catch(...) {
      expect(false) << "unexpected exception type";
    }
  }

  // Dynamic type for write_typed/read_typed roundtrip (materialized path).
  struct [[=serialization::message()]] node {
    std::uint64_t value;
    std::shared_ptr<node> next;
  };

  struct animal {
    virtual ~animal() = default;
    std::uint64_t kind = 0;
  };

  struct dog : animal {
    std::uint64_t bones = 0;
  };

  struct cat : animal {
    std::uint64_t lives = 0;
  };

  struct [[=serialization::message()]] kennel_msg {
    std::shared_ptr<animal> pet;
    std::shared_ptr<animal> other;
  };

}

int main() {
  "flat zero-copy register + dispatch roundtrip"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    codec.register_message_type<ping>();

    bool received = false;
    std::uint64_t received_seq = 0;
    codec.on_message<ping>(std::function<void(flat_view<ping>&)>([&](flat_view<ping>& v) {
      received = true;
      received_seq = v.get<^^ping::seq>();
    }));

    size_t msg_len = write_frame(codec, env.cb, ping{42});
    expect(msg_len > 0_u);

    dispatch_frame(codec, env.cb, msg_len);

    expect(received == true);
    expect(received_seq == 42_u);
  };

  "flat zero-copy string field borrowed"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    codec.register_message_type<pong>();

    bool received = false;
    std::uint64_t received_seq = 0;
    std::string received_data;
    codec.on_message<pong>(std::function<void(flat_view<pong>&)>([&](flat_view<pong>& v) {
      received = true;
      received_seq = v.get<^^pong::seq>();
      // flat_string converts to string_view; copy out for assertions
      received_data = std::string(std::string_view(v.get<^^pong::data>()));
    }));

    size_t msg_len = write_frame(codec, env.cb, pong{1, "hello world"});
    dispatch_frame(codec, env.cb, msg_len);

    expect(received == true);
    expect(received_seq == 1_u);
    expect(received_data == std::string("hello world"));
  };

  "flat zero-copy unknown type index throws"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    codec.register_message_type<ping>();

    // Manually craft a frame with type index 5 (out of range)
    // Flat format: [type index 4B][root_frame_size 4B][payload_size 4B][body]
    uint32_t placeholder = 0;
    circular_buffer_poke(env.cb, 0, &placeholder, sizeof(placeholder));
    codec.writer().write_offset_ = sizeof(placeholder);
    codec.writer().write_u32(static_cast<std::uint32_t>(5)); // unknown index
    codec.writer().write_u32(0); // root_frame_size (dummy)
    codec.writer().write_u32(0); // payload_size (dummy)
    codec.writer().write_u64(0); // dummy body
    codec.writer().write_rest();
    uint32_t payload_len = codec.writer().write_offset_ - sizeof(uint32_t);
    circular_buffer_poke(env.cb, 0, &payload_len, sizeof(payload_len));
    circular_buffer_commit_write(env.cb, codec.writer().write_offset_);

    expect(throws<std::runtime_error>([&] {
      dispatch_frame(codec, env.cb, payload_len);
    }));
  };

  "flat zero-copy multiple message types dispatch by index"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    codec.register_message_type<ping>(); // index 0
    codec.register_message_type<pong>(); // index 1

    std::vector<std::uint64_t> ping_seqs;
    std::vector<std::string> pong_data;

    codec.on_message<ping>(std::function<void(flat_view<ping>&)>([&](flat_view<ping>& v) {
      ping_seqs.push_back(v.get<^^ping::seq>());
    }));
    codec.on_message<pong>(std::function<void(flat_view<pong>&)>([&](flat_view<pong>& v) {
      pong_data.push_back(std::string(std::string_view(v.get<^^pong::data>())));
    }));

    size_t len1 = write_frame(codec, env.cb, ping{1});
    expect(len1 > 0_u);
    dispatch_frame(codec, env.cb, len1);

    size_t len2 = write_frame(codec, env.cb, pong{2, "hello"});
    expect(len2 > 0_u);
    dispatch_frame(codec, env.cb, len2);

    expect(ping_seqs.size() == 1_u);
    expect(ping_seqs[0] == 1_u);
    expect(pong_data.size() == 1_u);
    expect(pong_data[0] == std::string{"hello"});
  };

  "flat zero-copy exception type roundtrip"_test = [] {
    // Exceptions materialize (read_exception returns exception_ptr), so this
    // path is identical to flat_materialized_codec.
    struct circular_buffer* cb = circular_buffer_create(256, nullptr);
    flat_zero_copy_codec codec(cb, cb);

    codec.register_exception_type<unknown_exception>();

    flat_circular_buffer_writer& w = codec.writer();
    w.write_exception(unknown_exception("boom-zero-copy"));
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    flat_buffer_reader<circular_flat_source>& r = codec.reader();
    expect_exception_what(r.read_exception(), "boom-zero-copy");

    circular_buffer_destroy(cb);
  };

  "flat zero-copy dynamic type roundtrip with shared_ptr graph"_test = [] {
    // Dynamic types materialize via read_typed (graph of shared_ptr<node>),
    // so this path is identical to flat_materialized_codec. Zero-copy
    // applies only to message dispatch, not to typed graph roundtrips.
    struct circular_buffer* cb = circular_buffer_create(8192, nullptr);
    flat_zero_copy_codec codec(cb, cb);

    codec.register_dynamic_type<node>();

    auto root = std::make_shared<node>();
    root->value = 1;
    root->next = std::make_shared<node>();
    root->next->value = 2;
    root->next->next = std::make_shared<node>();
    root->next->next->value = 3;

    flat_circular_buffer_writer& w = codec.writer();
    w.write_typed<node>(root);
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    flat_buffer_reader<circular_flat_source>& r = codec.reader();
    std::shared_ptr<node> out;
    r.read_typed(out);
    r.read_rest();

    expect(out != nullptr);
    expect(eq(out->value, std::uint64_t{1}));
    expect(out->next != nullptr);
    expect(eq(out->next->value, std::uint64_t{2}));
    expect(out->next->next != nullptr);
    expect(eq(out->next->next->value, std::uint64_t{3}));
    expect(out->next->next->next == nullptr);

    circular_buffer_destroy(cb);
  };

  "flat zero-copy dispatch with payload"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    codec.register_message_type<ping>();

    bool received = false;
    std::uint64_t received_seq = 0;
    size_t received_payload_size = 0;
    std::uint64_t payload_value = 0;
    codec.on_message<ping>(std::function<void(flat_view<ping>&, size_t)>(
      [&](flat_view<ping>& v, size_t payload_size) {
        received = true;
        received_seq = v.get<^^ping::seq>();
        received_payload_size = payload_size;
        if(payload_size > 0) {
          codec.reader().read_u64(payload_value);
        }
      }));

    size_t msg_len = write_frame_with_payload(codec, env.cb, ping{7}, [](flat_circular_buffer_writer& w) {
      w.write_u64(12345);
    });
    dispatch_frame(codec, env.cb, msg_len);

    expect(received == true);
    expect(received_seq == 7_u);
    expect(received_payload_size == 8_u);
    expect(payload_value == 12345_u);
  };

  "flat zero-copy dispatch payload ignored by no-payload handler"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    codec.register_message_type<ping>();

    bool received = false;
    std::uint64_t received_seq = 0;
    // Handler takes only flat_view — payload is ignored, framing skips it.
    codec.on_message<ping>(std::function<void(flat_view<ping>&)>([&](flat_view<ping>& v) {
      received = true;
      received_seq = v.get<^^ping::seq>();
    }));

    size_t msg_len = write_frame_with_payload(codec, env.cb, ping{9}, [](flat_circular_buffer_writer& w) {
      w.write_u64(0xDEAD);
      w.write_u64(0xBEEF);
    });
    dispatch_frame(codec, env.cb, msg_len);

    expect(received == true);
    expect(received_seq == 9_u);
  };

  "flat zero-copy string field with payload simultaneously"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    codec.register_message_type<pong>();

    std::uint64_t received_seq = 0;
    std::string received_data;
    size_t received_payload_size = 0;
    std::uint64_t payload_value = 0;
    codec.on_message<pong>(std::function<void(flat_view<pong>&, size_t)>(
      [&](flat_view<pong>& v, size_t payload_size) {
        received_seq = v.get<^^pong::seq>();
        received_data = std::string(std::string_view(v.get<^^pong::data>()));
        received_payload_size = payload_size;
        codec.reader().read_u64(payload_value);
      }));

    size_t msg_len = write_frame_with_payload(codec, env.cb, pong{1, "hello"}, [](flat_circular_buffer_writer& w) {
      w.write_u64(4242);
    });
    dispatch_frame(codec, env.cb, msg_len);

    expect(received_seq == 1_u);
    expect(received_data == std::string("hello"));
    expect(received_payload_size == 8_u);
    expect(payload_value == 4242_u);
  };

  "flat zero-copy polymorphic dispatch"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;
    codec.register_message_type<kennel_msg>();
    codec.register_dynamic_type<dog>();
    codec.register_dynamic_type<cat>();

    kennel_msg msg;
    auto pet = std::make_shared<dog>();
    pet->kind = 3;
    pet->bones = 9;
    auto other = std::make_shared<cat>();
    other->kind = 4;
    other->lives = 8;
    msg.pet = pet;
    msg.other = other;

    bool received = false;
    codec.on_message<kennel_msg>(std::function<void(flat_view<kennel_msg>&)>([&](flat_view<kennel_msg>& v) {
      received = true;
      auto pet_view = v.get<^^kennel_msg::pet>();
      expect(pet_view.is_type<dog>());
      expect(!pet_view.is_type<cat>());
      auto dog_view = pet_view.get<dog>();
      expect(dog_view != nullptr);
      expect(eq(dog_view->get<^^animal::kind>(), 3_u));
      expect(eq(dog_view->get<^^dog::bones>(), 9_u));
      expect(throws<std::runtime_error>([&] { pet_view.get<cat>(); }));

      auto other_view = v.get<^^kennel_msg::other>();
      expect(other_view.is_type<cat>());
      expect(eq(other_view.get<cat>()->get<^^cat::lives>(), 8_u));
      expect(other_view != nullptr);
    }));

    size_t msg_len = write_frame(codec, env.cb, msg);
    dispatch_frame(codec, env.cb, msg_len);
    expect(received == true);
  };
}
