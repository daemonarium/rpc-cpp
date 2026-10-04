#include <boost/ut.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

#include "serialization/flat_materialized_codec.hpp"
#include "serialization/flat_circular_buffer_writer.hpp"
#include "serialization/flat_buffer_reader.hpp"
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

  bool operator==(const ping& a, const ping& b) { return a.seq == b.seq; }
  bool operator==(const pong& a, const pong& b) { return a.seq == b.seq && a.data == b.data; }

  // Helper: codec + a single shared circular buffer (input == output).
  struct codec_buffers {
    struct circular_buffer* cb;
    flat_materialized_codec codec;

    explicit codec_buffers(size_t buf_size = 8192)
      : cb(circular_buffer_create(buf_size, nullptr)),
        codec(cb, cb) {}

    ~codec_buffers() { circular_buffer_destroy(cb); }
  };

  // Write a full message (header + optional payload) into the codec's buffer
  // and commit it. Does NOT consume the length prefix — the readable region
  // after this call is [length-prefix][body]. Use dispatch_frame() to consume
  // the prefix and dispatch. Returns the body length (msg_len).
  template <typename T>
  size_t write_frame(flat_materialized_codec& codec, struct circular_buffer* cb,
                     const T& msg, const std::vector<std::uint8_t>& payload = {}) {
    uint32_t placeholder = 0;
    circular_buffer_poke(cb, 0, &placeholder, sizeof(placeholder));
    codec.writer().write_offset_ = sizeof(placeholder);

    codec.write_message_header<T>(msg);

    if(!payload.empty()) {
      for(auto b : payload) {
        codec.writer().write_u8(b);
      }
    }

    // Stamp payload_size into the header (mirrors framing.commit_send).
    codec.finalize_send();

    uint32_t payload_len = codec.writer().write_offset_ - sizeof(uint32_t);
    circular_buffer_poke(cb, 0, &payload_len, sizeof(payload_len));
    circular_buffer_commit_write(cb, codec.writer().write_offset_);
    return payload_len;
  }

  // Simulate framing dispatch of one message: consume the length prefix,
  // reset the codec reader, dispatch the body, then consume the body.
  // Mirrors socket_message_framing::handle_data for a single message.
  void dispatch_frame(flat_materialized_codec& codec, struct circular_buffer* cb, size_t msg_len) {
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

  // Simple dynamic type for write_typed/read_typed roundtrip.
  struct [[=serialization::message()]] node {
    std::uint64_t value;
    std::shared_ptr<node> next;
  };

  bool operator==(const node& a, const node& b) {
    if(a.value != b.value) return false;
    if(!a.next && !b.next) return true;
    if(!a.next || !b.next) return false;
    return *a.next == *b.next;
  }

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

  struct kennel {
    std::shared_ptr<animal> pet;
    std::shared_ptr<animal> twin;
  };

}

int main() {
  "flat codec register + dispatch roundtrip"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    codec.register_message_type<ping>();

    bool received = false;
    std::uint64_t received_seq = 0;
    codec.on_message<ping>(std::function<void(ping&)>([&](ping& p) {
      received = true;
      received_seq = p.seq;
    }));

    size_t msg_len = write_frame(codec, env.cb, ping{42});
    expect(msg_len > 0_u);

    dispatch_frame(codec, env.cb, msg_len);

    expect(received == true);
    expect(received_seq == 42_u);
  };

  "flat codec payload size reported to handler"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    codec.register_message_type<ping>();

    size_t received_payload_size = 0;
    std::uint64_t payload_value = 0;
    codec.on_message<ping>(std::function<void(ping&, size_t)>([&](ping& p, size_t payload_size) {
      received_payload_size = payload_size;
      if(payload_size > 0) {
        codec.reader().read_u64(payload_value);
      }
    }));

    // Write ping header, then a u64 payload (flat: 8 bytes LE)
    uint32_t placeholder = 0;
    circular_buffer_poke(env.cb, 0, &placeholder, sizeof(placeholder));
    codec.writer().write_offset_ = sizeof(placeholder);
    codec.write_message_header(ping{7});
    codec.writer().write_u64(12345);
    codec.finalize_send();
    uint32_t payload_len = codec.writer().write_offset_ - sizeof(uint32_t);
    circular_buffer_poke(env.cb, 0, &payload_len, sizeof(payload_len));
    circular_buffer_commit_write(env.cb, codec.writer().write_offset_);

    dispatch_frame(codec, env.cb, payload_len);

    expect(received_payload_size > 0_u);
    expect(payload_value == 12345_u);
  };

  "flat codec unread payload skipped cleanly"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    codec.register_message_type<ping>();

    bool received = false;
    // Handler ignores payload — does NOT read it from reader
    codec.on_message<ping>(std::function<void(ping&, size_t)>([&](ping& p, size_t) {
      received = true;
      expect(p.seq == 1_u);
    }));

    // Write ping with some payload bytes that will be left unread
    std::vector<std::uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD};
    size_t msg_len = write_frame(codec, env.cb, ping{1}, payload);

    // dispatch_frame should not throw even though payload is unread — framing
    // skips msg_len bytes after dispatch, consuming the unread payload.
    dispatch_frame(codec, env.cb, msg_len);
    expect(received == true);
  };

  "flat codec unknown type index throws"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    // Register ping on writer side only — index 0
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

  "flat codec multiple message types dispatch by index"_test = [] {
    codec_buffers env;
    auto& codec = env.codec;

    codec.register_message_type<ping>(); // index 0
    codec.register_message_type<pong>(); // index 1

    std::vector<std::uint64_t> ping_seqs;
    std::vector<std::string> pong_data;

    codec.on_message<ping>(std::function<void(ping&)>([&](ping& p) {
      ping_seqs.push_back(p.seq);
    }));
    codec.on_message<pong>(std::function<void(pong&)>([&](pong& p) {
      pong_data.push_back(p.data);
    }));

    // Write + dispatch first message, then second — mirroring how framing
    // processes one message at a time over a socket.
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

  "flat codec exception type roundtrip"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(256, nullptr);
    flat_materialized_codec codec(cb, cb);

    codec.register_exception_type<unknown_exception>();

    // Write exception into buffer
    flat_circular_buffer_writer& w = codec.writer();
    w.write_exception(unknown_exception("boom-flat-codec"));
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    // Read exception back
    flat_buffer_reader<circular_flat_source>& r = codec.reader();
    expect_exception_what(r.read_exception(), "boom-flat-codec");

    circular_buffer_destroy(cb);
  };

  "flat codec dynamic type roundtrip with shared_ptr graph"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(8192, nullptr);
    flat_materialized_codec codec(cb, cb);

    codec.register_dynamic_type<node>();

    // Build a small graph: root -> next -> next (3 nodes)
    auto root = std::make_shared<node>();
    root->value = 1;
    root->next = std::make_shared<node>();
    root->next->value = 2;
    root->next->next = std::make_shared<node>();
    root->next->next->value = 3;

    // Write typed
    flat_circular_buffer_writer& w = codec.writer();
    w.write_typed<node>(root);
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    // Read typed
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
    // Graph topology preserved (no cycles introduced)
    expect(static_cast<bool>(*out == *root));

    circular_buffer_destroy(cb);
  };

  "flat codec polymorphic dynamic type roundtrip"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(8192, nullptr);
    flat_materialized_codec codec(cb, cb);
    codec.register_dynamic_type<dog>();
    codec.register_dynamic_type<cat>();

    kennel src;
    auto pet = std::make_shared<dog>();
    pet->kind = 1;
    pet->bones = 4;
    src.pet = pet;
    src.twin = pet;

    flat_circular_buffer_writer& w = codec.writer();
    serialize(w, src);
    w.write_rest();
    circular_buffer_commit_write(cb, w.write_offset_);

    flat_buffer_reader<circular_flat_source>& r = codec.reader();
    kennel out{};
    deserialize(r, out);
    r.read_rest();

    expect(out.pet != nullptr);
    expect(out.twin.get() == out.pet.get());
    auto as_dog = std::dynamic_pointer_cast<dog>(out.pet);
    expect(as_dog != nullptr);
    expect(eq(as_dog->kind, 1_u));
    expect(eq(as_dog->bones, 4_u));
    expect(std::dynamic_pointer_cast<cat>(out.pet) == nullptr);

    circular_buffer_destroy(cb);
  };
}
