#include <boost/ut.hpp>
#include <cstring>
#include <vector>
#include <algorithm>

extern "C" {
#include <io/circular_buffer.h>
}

int main() {
  using namespace boost::ut;

  constexpr size_t initial_buffer_size = 128;
  constexpr size_t payload_size = 4096;

  "io circular_buffer pumps data larger than buffer"_test = [] {
    std::vector<std::uint8_t> data(payload_size);
    for (size_t i = 0; i < payload_size; ++i) {
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

    while (read_offset < payload_size) {
      size_t to_write = (payload_size - write_offset);
      if (to_write > 0) {
        size_t chunk = std::min(to_write, write_chunks[wi % (sizeof(write_chunks) / sizeof(write_chunks[0]))]);
        size_t n = circular_buffer_write(cb, data.data() + write_offset, chunk);
        write_offset += n;
        if (n > 0) wi++;
      }
      size_t to_read = payload_size - read_offset;
      if (to_read > 0) {
        size_t chunk = std::min(to_read, read_chunks[ri % (sizeof(read_chunks) / sizeof(read_chunks[0]))]);
        size_t n = circular_buffer_read(cb, received.data() + read_offset, chunk);
        read_offset += n;
        if (n > 0) ri++;
      }
    }

    expect(write_offset == payload_size);
    expect(read_offset == payload_size);
    expect(std::equal(data.begin(), data.end(), received.begin()));

    circular_buffer_destroy(cb);
  };

  "io circular_buffer single chunk write then read"_test = [] {
    std::vector<std::uint8_t> data(payload_size);
    for (size_t i = 0; i < payload_size; ++i) {
      data[i] = static_cast<std::uint8_t>(i % 256);
    }

    struct circular_buffer* cb = circular_buffer_create(initial_buffer_size, nullptr);
    expect(cb != nullptr);

    size_t written = 0;
    while (written < payload_size) {
      size_t n = circular_buffer_write(cb, data.data() + written, payload_size - written);
      written += n;
    }
    expect(written == payload_size);

    std::vector<std::uint8_t> received(payload_size, 0);
    size_t read_total = 0;
    while (read_total < payload_size) {
      size_t n = circular_buffer_read(cb, received.data() + read_total, payload_size - read_total);
      read_total += n;
    }
    expect(read_total == payload_size);
    expect(std::equal(data.begin(), data.end(), received.begin()));

    circular_buffer_destroy(cb);
  };
}
