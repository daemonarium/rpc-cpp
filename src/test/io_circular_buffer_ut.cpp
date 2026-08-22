#include <boost/ut.hpp>
#include <cstring>
#include <vector>
#include <algorithm>
#include <random>

extern "C" {
#include <io/circular_buffer.h>
}

namespace {

  void force_read_near_end(struct circular_buffer* cb, size_t pad) {
    std::vector<std::uint8_t> junk(pad, 0xAB);
    size_t written = 0;
    while(written < pad) {
      size_t n = circular_buffer_write(cb, junk.data() + written, pad - written);
      if(n == 0) break;
      written += n;
    }
    circular_buffer_skip_read(cb, written);
  }

  std::vector<std::uint8_t> concat_segments(struct circular_buffer_segment segs[2], int n) {
    std::vector<std::uint8_t> out;
    for(int i = 0; i < n; ++i) {
      out.insert(out.end(),
        reinterpret_cast<const std::uint8_t*>(segs[i].ptr),
        reinterpret_cast<const std::uint8_t*>(segs[i].ptr) + segs[i].len);
    }
    return out;
  }

}

int main() {
  using namespace boost::ut;

  constexpr size_t initial_buffer_size = 128;
  constexpr size_t payload_size = 4096;

  "io circular_buffer pumps data larger than buffer"_test = [] {
    std::vector<std::uint8_t> data(payload_size);
    for(size_t i = 0; i < payload_size; ++i) {
      data[i] = static_cast<std::uint8_t>(i);
    }

    struct circular_buffer* cb = circular_buffer_create(initial_buffer_size, nullptr);
    expect(cb != nullptr);

    std::vector<std::uint8_t> received(payload_size, 0);
    size_t write_offset = 0;
    size_t read_offset = 0;

    const size_t write_chunks[] = {1, 7, 31, 256};
    const size_t read_chunks[] = {3, 17, 64, 512};
    size_t wi = 0, ri = 0;

    while(read_offset < payload_size) {
      size_t to_write = (payload_size - write_offset);
      if(to_write > 0) {
        size_t chunk = std::min(to_write, write_chunks[wi % (sizeof(write_chunks) / sizeof(write_chunks[0]))]);
        size_t n = circular_buffer_write(cb, data.data() + write_offset, chunk);
        write_offset += n;
        if(n > 0) wi++;
      }
      size_t to_read = payload_size - read_offset;
      if(to_read > 0) {
        size_t chunk = std::min(to_read, read_chunks[ri % (sizeof(read_chunks) / sizeof(read_chunks[0]))]);
        size_t n = circular_buffer_read(cb, received.data() + read_offset, chunk);
        read_offset += n;
        if(n > 0) ri++;
      }
    }

    expect(write_offset == payload_size);
    expect(read_offset == payload_size);
    expect(std::equal(data.begin(), data.end(), received.begin()));

    circular_buffer_destroy(cb);
  };

  "io circular_buffer single chunk write then read"_test = [] {
    std::vector<std::uint8_t> data(payload_size);
    for(size_t i = 0; i < payload_size; ++i) {
      data[i] = static_cast<std::uint8_t>(i % 256);
    }

    struct circular_buffer* cb = circular_buffer_create(payload_size, nullptr);
    expect(cb != nullptr);

    size_t written = 0;
    while(written < payload_size) {
      size_t n = circular_buffer_write(cb, data.data() + written, payload_size - written);
      written += n;
    }
    expect(written == payload_size);

    std::vector<std::uint8_t> received(payload_size, 0);
    size_t read_total = 0;
    while(read_total < payload_size) {
      size_t n = circular_buffer_read(cb, received.data() + read_total, payload_size - read_total);
      read_total += n;
    }
    expect(read_total == payload_size);
    expect(std::equal(data.begin(), data.end(), received.begin()));

    circular_buffer_destroy(cb);
  };

  "io segments contiguous mid-buffer"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(64, nullptr);
    std::vector<std::uint8_t> payload(20);
    for(size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::uint8_t>(i + 1);
    circular_buffer_write(cb, payload.data(), payload.size());

    struct circular_buffer_segment segs[2];
    int n = circular_buffer_get_segments(cb, 2, 8, segs);
    expect(eq(n, 2) || eq(n, 1));
    expect(eq(n, 1));
    expect(eq(segs[0].len, std::size_t{8}));
    expect(std::memcmp(segs[0].ptr, payload.data() + 2, 8) == 0);
    expect(circular_buffer_region_contiguous(cb, 2, 8) != 0);
    const char* ptr = nullptr;
    expect(eq(circular_buffer_try_contiguous_ptr(cb, 2, 8, &ptr), std::size_t{8}));
    expect(ptr == segs[0].ptr);

    circular_buffer_destroy(cb);
  };

  "io segments wrap cross seam"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    force_read_near_end(cb, 28);
    std::vector<std::uint8_t> payload(16);
    for(size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::uint8_t>(0x40 + i);
    size_t w = circular_buffer_write(cb, payload.data(), payload.size());
    expect(eq(w, payload.size()));

    size_t readable = circular_buffer_readable_bytes(cb);
    expect(eq(readable, payload.size()));
    expect(circular_buffer_region_contiguous(cb, 0, readable) == 0);

    struct circular_buffer_segment segs[2];
    int n = circular_buffer_get_segments(cb, 0, readable, segs);
    expect(eq(n, 2));
    auto concat = concat_segments(segs, n);
    expect(static_cast<bool>(concat == payload));

    std::vector<std::uint8_t> peeked(readable);
    expect(eq(circular_buffer_peek(cb, 0, peeked.data(), readable), readable));
    expect(static_cast<bool>(peeked == payload));
    expect(static_cast<bool>(concat == peeked));

    circular_buffer_destroy(cb);
  };

  "io segments second half only"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    force_read_near_end(cb, 28);
    std::vector<std::uint8_t> payload(16);
    for(size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::uint8_t>(i);
    circular_buffer_write(cb, payload.data(), payload.size());

    size_t readable = circular_buffer_readable_bytes(cb);
    struct circular_buffer_segment all[2];
    int nall = circular_buffer_get_segments(cb, 0, readable, all);
    expect(eq(nall, 2));
    size_t first_len = all[0].len;

    struct circular_buffer_segment segs[2];
    int n = circular_buffer_get_segments(cb, first_len, readable - first_len, segs);
    expect(eq(n, 1));
    expect(eq(segs[0].len, readable - first_len));
    expect(std::memcmp(segs[0].ptr, payload.data() + first_len, segs[0].len) == 0);

    circular_buffer_destroy(cb);
  };

  "io segments first half only when wrapped"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    force_read_near_end(cb, 28);
    std::vector<std::uint8_t> payload(16);
    for(size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::uint8_t>(0x80 + i);
    circular_buffer_write(cb, payload.data(), payload.size());

    struct circular_buffer_segment all[2];
    int nall = circular_buffer_get_segments(cb, 0, circular_buffer_readable_bytes(cb), all);
    expect(eq(nall, 2));
    size_t first_len = all[0].len;

    struct circular_buffer_segment segs[2];
    int n = circular_buffer_get_segments(cb, 0, first_len, segs);
    expect(eq(n, 1));
    expect(eq(segs[0].len, first_len));
    expect(std::memcmp(segs[0].ptr, payload.data(), first_len) == 0);

    circular_buffer_destroy(cb);
  };

  "io segments OOB and empty"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(32, nullptr);
    std::uint8_t b[4] = {1, 2, 3, 4};
    circular_buffer_write(cb, b, 4);

    struct circular_buffer_segment segs[2];
    expect(eq(circular_buffer_get_segments(cb, 5, 1, segs), 0));
    expect(eq(circular_buffer_get_segments(cb, 0, 5, segs), 0));
    expect(eq(circular_buffer_get_segments(cb, 2, 0, segs), 1));
    expect(eq(segs[0].len, std::size_t{0}));
    expect(eq(circular_buffer_try_contiguous_ptr(cb, 0, 4, nullptr), std::size_t{0}));

    circular_buffer_destroy(cb);
  };

  "io segments parity vs peek property"_test = [] {
    std::mt19937 rng(42);
    for(int trial = 0; trial < 80; ++trial) {
      size_t cap = 32 + (rng() % 64);
      struct circular_buffer* cb = circular_buffer_create(cap, nullptr);
      size_t pad = rng() % (cap / 2 + 1);
      if(pad > 0) force_read_near_end(cb, pad);

      size_t want = 1 + rng() % (cap / 2 + 1);
      std::vector<std::uint8_t> payload(want);
      for(size_t i = 0; i < want; ++i) payload[i] = static_cast<std::uint8_t>(rng());
      size_t writable = circular_buffer_writable_bytes(cb);
      if(want > writable) want = writable;
      if(want == 0) {
        circular_buffer_destroy(cb);
        continue;
      }
      payload.resize(want);
      circular_buffer_write(cb, payload.data(), want);

      size_t readable = circular_buffer_readable_bytes(cb);
      for(int k = 0; k < 20; ++k) {
        size_t off = rng() % (readable + 1);
        size_t len = rng() % (readable - off + 1);
        struct circular_buffer_segment segs[2];
        int n = circular_buffer_get_segments(cb, off, len, segs);
        if(off + len > readable) {
          expect(eq(n, 0));
          continue;
        }
        expect(n == 1 || n == 2);
        auto concat = concat_segments(segs, n);
        std::vector<std::uint8_t> peeked(len);
        size_t got = circular_buffer_peek(cb, off, peeked.data(), len);
        expect(eq(got, len));
        expect(static_cast<bool>(concat == peeked));
        expect(eq(circular_buffer_region_contiguous(cb, off, len) != 0, n == 1));
      }
      circular_buffer_destroy(cb);
    }
  };

  "io linearize unwraps wrapped readable"_test = [] {
    struct circular_buffer* cb = circular_buffer_create(64, nullptr);
    force_read_near_end(cb, 50);
    std::vector<std::uint8_t> payload(40);
    for(size_t i = 0; i < payload.size(); ++i) {
      payload[i] = static_cast<std::uint8_t>(i + 1);
    }
    expect(eq(circular_buffer_write(cb, payload.data(), payload.size()), payload.size()));
    expect(eq(circular_buffer_region_contiguous(cb, 0, payload.size()), 0));
    circular_buffer_linearize(cb);
    expect(eq(circular_buffer_readable_bytes(cb), payload.size()));
    expect(neq(circular_buffer_region_contiguous(cb, 0, payload.size()), 0));
    std::vector<std::uint8_t> out(payload.size());
    expect(eq(circular_buffer_peek(cb, 0, out.data(), out.size()), out.size()));
    expect(static_cast<bool>(out == payload));
    circular_buffer_destroy(cb);
  };
}
