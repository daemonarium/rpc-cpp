#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <assert.h>

#include <io/circular_buffer.h>

#define log_printf(...) fprintf(stdout, __VA_ARGS__)
#define info_printf(...) fprintf(stdout, __VA_ARGS__)
#define debug_printf(...) fprintf(stdout, __VA_ARGS__)
#define error_printf(...) fprintf(stderr, __VA_ARGS__)

#define INFINITY UINT_MAX

typedef struct {
  size_t writeSize;
  size_t readSize;
} PacketConfiguration;

size_t bufferLengths[] = {1, 23, 1024, 4096, 10 * 1024};
size_t numBufferLengths = sizeof(bufferLengths) / sizeof(bufferLengths[0]);

PacketConfiguration packetConfigurations[] = {
    {1, 1}, {1, 2}, {1, INFINITY},
    {2, 1}, {2, INFINITY}, {2, 2},
    {INFINITY, INFINITY}
};
size_t numPacketConfigs = sizeof(packetConfigurations) / sizeof(packetConfigurations[0]);

size_t dataSizes[] = {233, 4096};
size_t numDataSizes = sizeof(dataSizes) / sizeof(dataSizes[0]);

uint8_t* create_test_data(size_t size) {
  uint8_t* buffer = (uint8_t*) malloc(sizeof(uint8_t) * size);
  if (buffer == NULL) {
    // Handle memory allocation failure
    return NULL;
  }

  for (size_t i = 0; i < size; ++i) {
    uint16_t number = (uint16_t)i;
    char hex_string[5]; // 4 chars for hex representation + null terminator
    // Convert number to hex string
    snprintf(hex_string, sizeof(hex_string), "%04x", (number / 4) & 0xFFFF);

    // Assign character to buffer, cycling through hex_string
    buffer[i] = hex_string[i % 4];
  }

  return buffer;
}

size_t minimal(size_t a, size_t b) {
  return (a < b) ? a : b;
}

void assert_mem_equal(const uint8_t* a, const uint8_t* b, size_t size) {
  if(memcmp(a, b, size) != 0) {
    error_printf("Memory not equal!\n");
    error_printf("buffer a:\n");
    error_printf("  %s\n", a);
    error_printf("buffer b:\n");
    error_printf("  %s\n", b);
    for (size_t i = 0; i < size; ++i) {
      if (a[i] != b[i]) {
        error_printf("Memory not equal at index %zu: %u != %u\n", i, a[i], b[i]);
      }
    }
    abort();
  }
}

void pump_data(
          struct circular_buffer* cb,
          const uint8_t* test_data, size_t test_data_len,
          uint8_t* read_data, size_t read_data_len,
          size_t maximal_write_size, size_t maximal_read_size,
          size_t* write_offset, size_t* read_offset
        ) {
  size_t read, written;
  do {
    read = 0;
    written = 0;
    size_t to_write = minimal(maximal_write_size, test_data_len - *write_offset);
    size_t to_read = minimal(maximal_read_size, read_data_len - *read_offset);
    if(to_write) {
      written = circular_buffer_write(cb, test_data + *write_offset, to_write);
      *write_offset += written;
    }
    if(to_read) {
      read = circular_buffer_read(cb, read_data + *read_offset, to_read);
      *read_offset += read;
      if(written && !read) {
        // Handle this error condition according to your requirement
        assert(!"Written data, but no data to read!");
      }
    }
  } while(written || read);
}

void debug_buffer_content(struct circular_buffer* buffer) {
  size_t readable_bytes = circular_buffer_readable_bytes(buffer);
  debug_printf("Readable bytes: %zd\n", readable_bytes);
  char* buffer_content = (char*) malloc(readable_bytes + 1);
  circular_buffer_peek(buffer, 0, buffer_content, readable_bytes);
  buffer_content[readable_bytes] = '\0';
  debug_printf("Buffer content: %s\n", buffer_content);
  free(buffer_content);
}

int main(int argc, char** argv) {
  int test_count = 0;
  for (size_t i = 0; i < numDataSizes; ++i) {
    size_t data_size = dataSizes[i];
    for (size_t j = 0; j < numBufferLengths; ++j) {
      size_t buffer_length = bufferLengths[j];
      for (size_t k = 0; k < numPacketConfigs; ++k) {
        PacketConfiguration packet_config = packetConfigurations[k];

        log_printf("Testing with data size %zd, buffer length %zd, write size %zd, read size %zd\n",
               data_size, buffer_length, packet_config.writeSize, packet_config.readSize);
        {
          log_printf("  Test %04d - static buffer size:\n", test_count);
          struct circular_buffer *cb = circular_buffer_create(buffer_length, NULL);
          uint8_t *test_data = create_test_data(data_size);
        uint8_t *read_data = (uint8_t*) malloc(data_size);
          memset(read_data, 0, data_size);

          size_t write_offset = 0;
          size_t read_offset = 0;

          pump_data(cb, test_data, data_size, read_data, data_size,
                    packet_config.writeSize, packet_config.readSize,
                    &write_offset, &read_offset);

          assert(memcmp(test_data, read_data, data_size) == 0);

          log_printf("    -> OK!\n");
          test_count++;
          free(test_data);
          free(read_data);
          circular_buffer_destroy(cb);
        }
        {
          size_t resize_at = data_size / 2;
          log_printf("  Test %04d - with buffer resize at %zu:\n", test_count, resize_at);
          struct circular_buffer *cb = circular_buffer_create(buffer_length, NULL);
          uint8_t *test_data = create_test_data(data_size);
          uint8_t *read_data = (uint8_t*) malloc(data_size);
          memset(read_data, 0, data_size);

          size_t write_offset = 0;
          size_t read_offset = 0;

          pump_data(cb, test_data, data_size, read_data, resize_at,
                    packet_config.writeSize, packet_config.readSize,
                    &write_offset, &read_offset);

          //debug_buffer_content(cb);

          info_printf("    Resizing buffer to %zd\n", buffer_length * 2);
          size_t grow_result = circular_buffer_grow(cb, buffer_length * 2);
          info_printf("    Grow result: %zd\n", grow_result);

          //debug_buffer_content(cb);

          pump_data(cb, test_data, data_size, read_data, data_size,
                    packet_config.writeSize, packet_config.readSize,
                    &write_offset, &read_offset);

          assert_mem_equal(test_data, read_data, data_size);

          log_printf("    -> OK!\n");
          test_count++;
          free(test_data);
          free(read_data);
          circular_buffer_destroy(cb);
        }
      }
    }
  }
  log_printf("All %d tests passed!\n", test_count);
  return 0;
}

