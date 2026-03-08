#include <malloc.h>
#include <string.h>
#include <stdbool.h>
#include "circular_buffer.h"

#define HEADER_SIZE 12

size_t default_new_length(size_t requested_length) {
  // returns nearest power of two
  size_t new_length = 1;
  while(new_length < requested_length) new_length <<= 1;
  return new_length;
}

void circular_buffer_init_data(struct circular_buffer_data* data, size_t initial_size) {
  data->size = initial_size + HEADER_SIZE + 1; // one additional byte so that
  // read_offset != write_offset means buffer is full
  data->read_offset = 0;
  data->write_offset = 0;
}

struct circular_buffer* circular_buffer_init(struct circular_buffer* buffer,
    size_t initial_size, new_length_callback new_length) {
  struct circular_buffer_data* data = malloc(sizeof(struct circular_buffer_data) + initial_size + 1);
  if(data == NULL) return NULL;
  circular_buffer_init_data(data, initial_size);
  if(buffer == NULL) {
    free(data);
    return NULL;
  }
  buffer->data = data;
  if(new_length == NULL) new_length = default_new_length;
  buffer->new_length = new_length;
  return buffer;
}

struct circular_buffer* circular_buffer_create(size_t initial_size, new_length_callback new_length) {
  struct circular_buffer* buffer = malloc(sizeof(struct circular_buffer));
  return circular_buffer_init(buffer, initial_size, new_length);
}

void circular_buffer_deinit(struct circular_buffer* buffer) {
  free(buffer->data);
  buffer->data = NULL;
}

void circular_buffer_destroy(struct circular_buffer* buffer) {
  free(buffer->data);
  free(buffer);
}

size_t circular_buffer_data_readable_bytes(struct circular_buffer_data* buffer) {
  if(buffer->read_offset <= buffer->write_offset) {
    return buffer->write_offset - buffer->read_offset;
  } else {
    return buffer->size - HEADER_SIZE - buffer->read_offset + buffer->write_offset;
  }
}

size_t circular_buffer_data_writable_bytes(struct circular_buffer_data* buffer) {
  if(buffer->read_offset <= buffer->write_offset) {
    return buffer->size - HEADER_SIZE - buffer->write_offset + buffer->read_offset - 1;
  } else {
    return buffer->read_offset - buffer->write_offset - 1; // one additional byte so that
                                                           // read_offset != write_offset means buffer is full
  }
}

size_t circular_buffer_readable_bytes(struct circular_buffer* buffer) {
  return circular_buffer_data_readable_bytes(buffer->data);
}

size_t circular_buffer_writable_bytes(struct circular_buffer* buffer) {
  return circular_buffer_data_writable_bytes(buffer->data);
}

size_t circular_buffer_grow(struct circular_buffer* buffer, size_t new_minimum_size) {
  if(new_minimum_size <= buffer->data->size - HEADER_SIZE - 1) {
    //printf("new buffer length %zd must be greater than current buffer length %zd\n",
    //     new_minimum_size, buffer->data->size - HEADER_SIZE - 1);
    //__asm__("unreachable");
  }
  size_t new_length = buffer->new_length(new_minimum_size + HEADER_SIZE + 1); // one additional byte so that
                                                                // read_offset != write_offset means buffer is full
  size_t old_buffer_size = buffer->data->size - HEADER_SIZE;
  size_t new_buffer_length = new_length - HEADER_SIZE;
  size_t buffer_increment = new_buffer_length - old_buffer_size;
  //debug_printf("growing buffer from %zd to %zd\n", old_buffer_size, new_buffer_length);

  //buffer->data = realloc(buffer->data, sizeof(struct circular_buffer_data) - 1 + new_length);
  struct circular_buffer_data* new_data = malloc(new_length + sizeof(size_t));
  if(new_data == NULL) return old_buffer_size - 1; // allocation failed, return old buffer size
  memcpy(new_data, buffer->data, buffer->data->size + sizeof(size_t));
  free(buffer->data);
  buffer->data = new_data;
  buffer->data->size = new_length;

  size_t read_offset = buffer->data->read_offset;
  size_t write_offset = buffer->data->write_offset;
  if(read_offset > write_offset) { // move read offset to the end of buffer to create more space for writing
    //debug_printf("moving read offset from %zd to %zd\n", read_offset, read_offset + buffer_increment);
    size_t new_read_offset = read_offset + buffer_increment;
    size_t bytes_to_move = old_buffer_size - read_offset;
    memcpy(
        buffer->data->data + new_read_offset,
        buffer->data->data + read_offset,
        bytes_to_move);
    buffer->data->read_offset = new_read_offset;
  }
  return new_buffer_length - 1;
}

size_t circular_buffer_reserve_for_reading(struct circular_buffer* buffer, size_t bytes) {
  size_t read_available = circular_buffer_readable_bytes(buffer) + circular_buffer_writable_bytes(buffer);
  if(read_available >= bytes) return read_available;
  size_t proposed_buffer_increment = bytes - read_available;
  size_t new_minimum_buffer_size = buffer->data->size + proposed_buffer_increment;
  circular_buffer_grow(buffer, new_minimum_buffer_size);
  return circular_buffer_readable_bytes(buffer) + circular_buffer_writable_bytes(buffer);
}

size_t circular_buffer_reserve_for_writing(struct circular_buffer* buffer, size_t bytes) {
  size_t write_available = circular_buffer_writable_bytes(buffer);
  if(write_available >= bytes) return write_available;
  size_t proposed_buffer_increment = bytes - write_available;
  size_t new_minimum_buffer_size = buffer->data->size + proposed_buffer_increment;
  circular_buffer_grow(buffer, new_minimum_buffer_size);
  return circular_buffer_writable_bytes(buffer);
}

size_t circular_buffer_data_poke(struct circular_buffer_data* buffer, size_t offset, const void* source, size_t bytes) {
  size_t write_request = bytes + offset;
  size_t write_length = circular_buffer_data_writable_bytes(buffer);
  if(write_length > write_request) write_length = write_request;
  if(write_length == 0) return 0;
  size_t write_at_end = buffer->size - HEADER_SIZE - buffer->write_offset;
  if(write_length < write_at_end) write_at_end = write_length;
  size_t write_at_start = write_length - write_at_end;
  if(write_at_end > offset) {
    memcpy(buffer->data + buffer->write_offset + offset, source, write_at_end - offset);
    memcpy(buffer->data, source + write_at_end - offset, write_at_start);
  } else {
    size_t offset_rest = (offset - write_at_end);
    memcpy(buffer->data + offset_rest, source, write_at_end - offset_rest);
  }
  return write_length - offset;
}

/* 
#include <stdio.h>

void print_hex(const void* data, size_t size) {
  const unsigned char* bytes = data;
  for (size_t i = 0; i < size; i += 16) {
    printf("%04X: ", (unsigned int)i);
    for (size_t j = 0; j < 16; j++) {
      if (i + j < size) {
        printf("%02X ", bytes[i + j]);
      }
    }
    printf("\n");
  }
}
 */
 
size_t circular_buffer_data_peek(struct circular_buffer_data* buffer, size_t offset, void* dest, size_t bytes) {
  size_t read_request = bytes + offset;
  size_t readable_bytes = circular_buffer_data_readable_bytes(buffer);
  size_t read_length = readable_bytes;
  if(read_length > read_request) read_length = read_request;
  if(read_length == 0) return 0;
  size_t source_buffer_length = buffer->size - HEADER_SIZE;
  size_t read_at_end = source_buffer_length - buffer->read_offset;
  if(read_length < read_at_end) read_at_end = read_length;
  size_t read_at_start = read_length - read_at_end;
  /* printf("buffer content:\n");
  print_hex(buffer->data, source_buffer_length); */
  if(read_at_end > offset) {
    memcpy(dest, buffer->data + buffer->read_offset + offset, read_at_end - offset);
    memcpy(dest + read_at_end - offset, buffer->data, read_at_start);
  } else {
    size_t offset_rest = (offset - read_at_end);
    memcpy(dest, buffer->data + offset_rest, read_at_start - offset_rest);
  }
  /* printf("read content:\n");
  print_hex(dest, read_length - offset); */
  return read_length - offset;
}

size_t circular_buffer_data_write(struct circular_buffer_data* buffer, const void* source, size_t bytes) {
  size_t write_length = circular_buffer_data_poke(buffer, 0, source, bytes);
  buffer->write_offset = (buffer->write_offset + write_length) % (buffer->size - HEADER_SIZE);
  return write_length;
}

size_t circular_buffer_data_read(struct circular_buffer_data* buffer, void* dest, size_t bytes) {
  size_t read_length = circular_buffer_data_peek(buffer, 0, dest, bytes);
  buffer->read_offset = (buffer->read_offset + read_length) % (buffer->size - HEADER_SIZE);
  return read_length;
}

size_t circular_buffer_write(struct circular_buffer* buffer, const void* source, size_t bytes) {
  return circular_buffer_data_write(buffer->data, source, bytes);
}

size_t circular_buffer_read(struct circular_buffer* buffer, void* dest, size_t bytes) {
  return circular_buffer_data_read(buffer->data, dest, bytes);
}

size_t circular_buffer_poke(struct circular_buffer* buffer, size_t offset, const void* source, size_t bytes) {
  return circular_buffer_data_poke(buffer->data, offset, source, bytes);
}

size_t circular_buffer_peek(struct circular_buffer* buffer, size_t offset, void* dest, size_t bytes) {
  return circular_buffer_data_peek(buffer->data, offset, dest, bytes);
}

size_t circular_buffer_capacity(struct circular_buffer* buffer) {
  return buffer->data->size - HEADER_SIZE - 1;
}

void circular_buffer_clear(struct circular_buffer* buffer) {
  buffer->data->read_offset = 0;
  buffer->data->write_offset = 0;
}

size_t circular_buffer_skip_read(struct circular_buffer* buffer, size_t bytes) {
  size_t read_length = circular_buffer_readable_bytes(buffer);
  if(read_length > bytes) read_length = bytes;
  if(read_length == 0) return 0;
  size_t source_buffer_length = buffer->data->size - HEADER_SIZE;
  buffer->data->read_offset = (buffer->data->read_offset + read_length) % source_buffer_length;
  return read_length;
}

size_t circular_buffer_commit_write(struct circular_buffer* buffer, size_t bytes) {
  size_t write_length = circular_buffer_writable_bytes(buffer);
  if(write_length > bytes) write_length = bytes;
  if(write_length == 0) return 0;
  size_t source_buffer_length = buffer->data->size - HEADER_SIZE;
  buffer->data->write_offset = (buffer->data->write_offset + write_length) % source_buffer_length;
  return write_length;
}

size_t circular_buffer_peek_uvarint(struct circular_buffer *buffer, size_t offset, uint64_t *value) {
  uint8_t number[9];
  size_t first_read = circular_buffer_peek(buffer, offset, number, 1);
  if(first_read == 0) return 0;
  if((number[0] & 0x80) == 0) { // 7 bit number encoded in 1 byte
    *value = number[0];
    return first_read;
  } else if ((number[0] & 0x40) == 0) { // 14 bit number encoded in 2 bytes
    size_t second_read = circular_buffer_peek(buffer, offset + 1, number + 1, 1);
    if(second_read == 0) return 0;
    *value = ((number[0] & 0x3F) << 8) | number[1];
    return first_read + second_read;
  } else if ((number[0] & 0x20) == 0) { // 21 bit number encoded in 3 bytes
    size_t second_read = circular_buffer_peek(buffer, offset + 1, number + 1, 2);
    if(second_read == 0) return 0;
    *value = ((number[0] & 0x1F) << 16) | (number[1] << 8) | number[2];
    return first_read + second_read;
  } else if ((number[0] & 0x10) == 0) { // 28 bit number encoded in 4 bytes
    size_t second_read = circular_buffer_peek(buffer, offset + 1, number + 1, 3);
    if(second_read == 0) return 0;
    *value = ((number[0] & 0x0F) << 24) | (number[1] << 16) | (number[2] << 8) | number[3];
    return first_read + second_read;
  } else if ((number[0] & 0x08) == 0) { // 35 bit number encoded in 5 bytes
    size_t second_read = circular_buffer_peek(buffer, offset + 1, number + 1, 4);
    if(second_read == 0) return 0;
    *value = ((uint64_t)(number[0] & 0x07) << 32) | (number[1] << 24) | (number[2] << 16) | (number[3] << 8)
        | number[4];
    return first_read + second_read;
  } else if((number[0] & 0x04) == 0) { // 42 bit number encoded in 6 bytes
    size_t second_read = circular_buffer_peek(buffer, offset + 1, number + 1, 5);
    if(second_read == 0) return 0;
    *value = ((uint64_t)(number[0] & 0x03) << 40) | ((uint64_t)number[1] << 32) | (number[2] << 24) | (number[3] << 16)
        | (number[4] << 8) | number[5];
    return first_read + second_read;
  } else if((number[0] & 0x02) == 0) { // 49 bit number encoded in 7 bytes
    size_t second_read = circular_buffer_peek(buffer, offset + 1, number + 1, 6);
    if(second_read == 0) return 0;
    *value = ((uint64_t)(number[0] & 0x01) << 48) | ((uint64_t)number[1] << 40) | ((uint64_t)number[2] << 32)
        | (number[3] << 24) | (number[4] << 16) | (number[5] << 8) | number[6];
    return first_read + second_read;
  } else if((number[0] & 0x01) == 0) { // 56 bit number encoded in 8 bytes
    size_t second_read = circular_buffer_peek(buffer, offset + 1, number + 1, 7);
    if(second_read == 0) return 0;
    *value = ((uint64_t)number[1] << 48) | ((uint64_t)number[2] << 40) | ((uint64_t)number[3] << 32)
        | (number[4] << 24) | (number[5] << 16) | (number[6] << 8) | number[7];
    return first_read + second_read;
  } else { // 64 bit number encoded in 9 bytes
    size_t second_read = circular_buffer_peek(buffer, offset + 1, number + 1, 8);
    if(second_read == 0) return 0;
    *value = ((uint64_t)number[1] << 56) | ((uint64_t)number[2] << 48) | ((uint64_t)number[3] << 40)
        | ((uint64_t)number[4] << 32) | (number[5] << 24) | (number[6] << 16) | (number[7] << 8) | number[8];
    return first_read + second_read;
  }
}

size_t circular_buffer_poke_uvarint(struct circular_buffer *buffer, size_t offset, uint64_t value) {
  if(value < 0x80) { // 7 bit number encoded in 1 byte
    uint8_t number = value;
    return circular_buffer_poke(buffer, offset, &number, 1);
  } else if(value < 0x4000) { // 14 bit number encoded in 2 bytes
    uint8_t number[2] = {0x80 | (value >> 8), value & 0xFF};
    return circular_buffer_poke(buffer, offset, number, 2);
  } else if(value < 0x200000) { // 21 bit number encoded in 3 bytes
    uint8_t number[3] = {0xC0 | (value >> 16), (value >> 8) & 0xFF, value & 0xFF};
    return circular_buffer_poke(buffer, offset, number, 3);
  } else if(value < 0x10000000) { // 28 bit number encoded in 4 bytes
    uint8_t number[4] = {0xE0 | (value >> 24), (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF};
    return circular_buffer_poke(buffer, offset, number, 4);
  } else if(value < 0x800000000) { // 35 bit number encoded in 5 bytes
    uint8_t number[5] = {0xF0 | (value >> 32), (value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF,
        value & 0xFF};
    return circular_buffer_poke(buffer, offset, number, 5);
  } else if(value < 0x40000000000) { // 42 bit number encoded in 6 bytes
    uint8_t number[6] = {0xF8 | (value >> 40), (value >> 32) & 0xFF, (value >> 24) & 0xFF, (value >> 16) & 0xFF,
        (value >> 8) & 0xFF, value & 0xFF};
    return circular_buffer_poke(buffer, offset, number, 6);
  } else if(value < 0x2000000000000) { // 49 bit number encoded in 7 bytes
    uint8_t number[7] = {0xFC | (value >> 48), (value >> 40) & 0xFF, (value >> 32) & 0xFF, (value >> 24) & 0xFF,
        (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF};
    return circular_buffer_poke(buffer, offset, number, 7);
  } else if(value < 0x100000000000000) { // 56 bit number encoded in 8 bytes
    uint8_t number[8] = {0xFE, (value >> 48) & 0xFF, (value >> 40) & 0xFF, (value >> 32) & 0xFF, (value >> 24) & 0xFF,
        (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF};
    return circular_buffer_poke(buffer, offset, number, 8);
  } else { // 64 bit number encoded in 9 bytes
    uint8_t number[9] = {0xFF, (value >> 56) & 0xFF, (value >> 48) & 0xFF, (value >> 40) & 0xFF, (value >> 32) & 0xFF,
                         (value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF};
    return circular_buffer_poke(buffer, offset, number, 9);
  }
}

size_t circular_buffer_read_uvarint(struct circular_buffer *buffer, uint64_t *value) {
  size_t read_length = circular_buffer_peek_uvarint(buffer, 0, value);
  if(read_length == 0) return 0;
  circular_buffer_skip_read(buffer, read_length);
  return read_length;
}

size_t circular_buffer_write_uvarint(struct circular_buffer *buffer, uint64_t value) {
  size_t write_length = circular_buffer_poke_uvarint(buffer, 0, value);
  if(write_length == 0) return 0;
  circular_buffer_commit_write(buffer, write_length);
  return write_length;
}

size_t circular_buffer_peek_svarint(struct circular_buffer *buffer, size_t offset, int64_t *value) {
  uint8_t number[9];
  size_t first_read = circular_buffer_peek(buffer, offset, number, 1);
  if(first_read == 0) return 0;
  bool negative = (number[0] & 0x80) == 0;
  if((number[0] & 0x40) == 0) { // 6 bit signed number encoded in 1 byte
    *value = negative
        ? -((~number[0]) & 0x3F)
        : (number[0] & 0x3F);
    return first_read;
  } else if((number[0] & 0x20) == 0) { // 13 bit signed number encoded in 2 bytes
    size_t second_read = circular_buffer_peek(buffer, offset + 1, number + 1, 1);
    if(second_read == 0) return 0;
    *value = negative
        ? -(~(((number[0] & 0x1F) << 8) | number[1]) & 0x3FFF)
        : ((number[0] & 0x1F) << 8) | number[1];
    return first_read + second_read;
  } else if((number[0] & 0x10) == 0) { // 20 bit signed number encoded in 3 bytes
    size_t rest = circular_buffer_peek(buffer, offset + 1, number + 1, 2);
    if(rest < 2) return 0;
    *value = negative
        ? -(~(((number[0] & 0x0F) << 16) | (number[1] << 8) | number[2]) & 0x1FFFF)
        : (((number[0] & 0x0F) << 16) | (number[1] << 8) | number[2]);
    return first_read + rest;
  } else if((number[0] & 0x08) == 0) { // 27 bit signed number encoded in 4 bytes
    size_t rest = circular_buffer_peek(buffer, offset + 1, number + 1, 3);
    if(rest < 3) return 0;
    *value = negative
        ? -(~(((number[0] & 0x07) << 24) | (number[1] << 16) | (number[2] << 8) | number[3]) & 0xFFFFFF)
        : (((number[0] & 0x07) << 24) | (number[1] << 16) | (number[2] << 8) | number[3]);
    return first_read + rest;
  } else if((number[0] & 0x04) == 0) { // 34 bit signed number encoded in 5 bytes
    size_t rest = circular_buffer_peek(buffer, offset + 1, number + 1, 4);
    if(rest < 4) return 0;
    *value = negative
        ? -(~(((uint64_t)(number[0] & 0x03) << 32) | (number[1] << 24) | (number[2] << 16) | (number[3] << 8) | number[4]) & 0x7FFFFFFF)
        : (((uint64_t)(number[0] & 0x03) << 32) | (number[1] << 24) | (number[2] << 16) | (number[3] << 8) | number[4]);
    return first_read + rest;
  } else if((number[0] & 0x02) == 0) { // 41 bit signed number encoded in 6 bytes
    size_t rest = circular_buffer_peek(buffer, offset + 1, number + 1, 5);
    if(rest < 5) return 0;
    *value = negative
        ? -(~(((uint64_t)(number[0] & 0x01) << 40) | ((uint64_t)number[1] << 32) | (number[2] << 24) | (number[3] << 16) | (number[4] << 8) | number[5]) & 0x7FFFFFFFFF)
        : (((uint64_t)(number[0] & 0x01) << 40) | ((uint64_t)number[1] << 32) | (number[2] << 24) | (number[3] << 16) | (number[4] << 8) | number[5]);
    return first_read + rest;
  } else if((number[0] & 0x01) == 0) { // 48 bit signed number encoded in 7 bytes
    size_t rest = circular_buffer_peek(buffer, offset + 1, number + 1, 6);
    if(rest < 6) return 0;
    *value = negative
        ? -(~(((uint64_t)(number[0] & 0x00) << 48) | ((uint64_t)number[1] << 40) | ((uint64_t)number[2] << 32) | (number[3] << 24) | (number[4] << 16) | (number[5] << 8) | number[6]) & 0x7FFFFFFFFFFFFF)
        : (((uint64_t)(number[0] & 0x00) << 48) | ((uint64_t)number[1] << 40) | ((uint64_t)number[2] << 32) | (number[3] << 24) | (number[4] << 16) | (number[5] << 8) | number[6]);
    return first_read + rest;
  } else { // 64 bit signed number encoded in 9 bytes
    size_t rest = circular_buffer_peek(buffer, offset + 1, number + 1, 8);
    if(rest < 8) return 0;
    *value = negative
        ? -(~(((uint64_t)number[1] << 56) | ((uint64_t)number[2] << 48) | ((uint64_t)number[3] << 40) | ((uint64_t)number[4] << 32) | (number[5] << 24) | (number[6] << 16) | (number[7] << 8) | number[8]) & 0x7FFFFFFFFFFFFFFF)
        : (((uint64_t)number[1] << 56) | ((uint64_t)number[2] << 48) | ((uint64_t)number[3] << 40) | ((uint64_t)number[4] << 32) | (number[5] << 24) | (number[6] << 16) | (number[7] << 8) | number[8]);
    return first_read + rest;
  }
}

size_t circular_buffer_poke_svarint(struct circular_buffer *buffer, size_t offset, int64_t value) {
  bool negative = value < 0;
  uint64_t unsigned_value = negative ? (uint64_t)(-value) : (uint64_t)value;
  if(unsigned_value < 0x40) { // 6 bit signed number encoded in 1 byte
    uint8_t number = negative ? (uint8_t)((~unsigned_value) & 0x3F) : (uint8_t)(0x80 | unsigned_value);
    return circular_buffer_poke(buffer, offset, &number, 1);
  } else if(unsigned_value < 0x2000) { // 13 bit signed number encoded in 2 bytes
    if(negative) {
      uint8_t number[2] = {
          (uint8_t)(0x40 | (((~unsigned_value) >> 8) & 0x1F)),
          (uint8_t)(((~unsigned_value) & 0xFF))};
      return circular_buffer_poke(buffer, offset, number, 2);
    } else {
      uint8_t number[2] = {
          (uint8_t)(0x80 | 0x40 | (unsigned_value >> 8)),
          (uint8_t)(unsigned_value & 0xFF)};
      return circular_buffer_poke(buffer, offset, number, 2);
    }
  } else if(unsigned_value < 0x100000) { // 20 bit signed number encoded in 3 bytes
    if(negative) {
      uint8_t number[3] = {
          (uint8_t)(0x60 | (((~unsigned_value) >> 16) & 0x0F)),
          (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
          (uint8_t)(((~unsigned_value) & 0xFF))};
      return circular_buffer_poke(buffer, offset, number, 3);
    } else {
      uint8_t number[3] = {
          (uint8_t)(0x80 | 0x60 | (unsigned_value >> 16)),
          (uint8_t)((unsigned_value >> 8) & 0xFF),
          (uint8_t)(unsigned_value & 0xFF)};
      return circular_buffer_poke(buffer, offset, number, 3);
    }
  } else if(unsigned_value < 0x8000000) { // 27 bit signed number encoded in 4 bytes
    if(negative) {
      uint8_t number[4] = {
          (uint8_t)(0x70 | (((~unsigned_value) >> 24) & 0x07)),
          (uint8_t)(((~unsigned_value) >> 16) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
          (uint8_t)(((~unsigned_value) & 0xFF))};
      return circular_buffer_poke(buffer, offset, number, 4);
    } else {
      uint8_t number[4] = {
          (uint8_t)(0x80 | 0x70 | (unsigned_value >> 24)),
          (uint8_t)((unsigned_value >> 16) & 0xFF),
          (uint8_t)((unsigned_value >> 8) & 0xFF),
          (uint8_t)(unsigned_value & 0xFF)};
      return circular_buffer_poke(buffer, offset, number, 4);
    }
  } else if(unsigned_value < 0x400000000) { // 34 bit signed number encoded in 5 bytes
    if(negative) {
      uint8_t number[5] = {
          (uint8_t)(0x78 | (((~unsigned_value) >> 32) & 0x03)),
          (uint8_t)(((~unsigned_value) >> 24) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 16) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
          (uint8_t)(((~unsigned_value) & 0xFF))};
      return circular_buffer_poke(buffer, offset, number, 5);
    } else {
      uint8_t number[5] = {
          (uint8_t)(0x80 | 0x78 | (unsigned_value >> 32)),
          (uint8_t)((unsigned_value >> 24) & 0xFF),
          (uint8_t)((unsigned_value >> 16) & 0xFF),
          (uint8_t)((unsigned_value >> 8) & 0xFF),
          (uint8_t)(unsigned_value & 0xFF)};
      return circular_buffer_poke(buffer, offset, number, 5);
    }
  } else if(unsigned_value < 0x20000000000) { // 41 bit signed number encoded in 6 bytes
    if(negative) {
      uint8_t number[6] = {
          (uint8_t)(0x7C | (((~unsigned_value) >> 40) & 0x01)),
          (uint8_t)(((~unsigned_value) >> 32) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 24) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 16) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
          (uint8_t)(((~unsigned_value) & 0xFF))};
      return circular_buffer_poke(buffer, offset, number, 6);
    } else {
      uint8_t number[6] = {
          (uint8_t)(0x80 | 0x7C | (unsigned_value >> 40)),
          (uint8_t)((unsigned_value >> 32) & 0xFF),
          (uint8_t)((unsigned_value >> 24) & 0xFF),
          (uint8_t)((unsigned_value >> 16) & 0xFF),
          (uint8_t)((unsigned_value >> 8) & 0xFF),
          (uint8_t)(unsigned_value & 0xFF)};
      return circular_buffer_poke(buffer, offset, number, 6);
    }
  } else if(unsigned_value < 0x1000000000000) { // 48 bit signed number encoded in 7 bytes
    if(negative) {
      uint8_t number[7] = {
          (uint8_t)(0x7E),
          (uint8_t)(((~unsigned_value) >> 40) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 32) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 24) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 16) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
          (uint8_t)(((~unsigned_value) & 0xFF))};
      return circular_buffer_poke(buffer, offset, number, 7);
    } else {
      uint8_t number[7] = {
          (uint8_t)(0x80 | 0x7E),
          (uint8_t)((unsigned_value >> 40) & 0xFF),
          (uint8_t)((unsigned_value >> 32) & 0xFF),
          (uint8_t)((unsigned_value >> 24) & 0xFF),
          (uint8_t)((unsigned_value >> 16) & 0xFF),
          (uint8_t)((unsigned_value >> 8) & 0xFF),
          (uint8_t)(unsigned_value & 0xFF)};
      return circular_buffer_poke(buffer, offset, number, 7);
    }
  } else { // 64 bit signed number encoded in 9 bytes
    if(negative) {
      uint8_t number[9] = {
          (uint8_t)(0x7F),
          (uint8_t)(((~unsigned_value) >> 56) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 48) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 40) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 32) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 24) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 16) & 0xFF),
          (uint8_t)(((~unsigned_value) >> 8) & 0xFF),
          (uint8_t)(((~unsigned_value) & 0xFF))
      };
      return circular_buffer_poke(buffer, offset, number, 9);
    } else {
      uint8_t number[9] = {
          (uint8_t)(0x80 | 0x7F),
          (uint8_t)((unsigned_value >> 56) & 0xFF),
          (uint8_t)((unsigned_value >> 48) & 0xFF),
          (uint8_t)((unsigned_value >> 40) & 0xFF),
          (uint8_t)((unsigned_value >> 32) & 0xFF),
          (uint8_t)((unsigned_value >> 24) & 0xFF),
          (uint8_t)((unsigned_value >> 16) & 0xFF),
          (uint8_t)((unsigned_value >> 8) & 0xFF),
          (uint8_t)(unsigned_value & 0xFF)
      };
      return circular_buffer_poke(buffer, offset, number, 9);
    }
  }
}

size_t circular_buffer_read_svarint(struct circular_buffer *buffer, int64_t *value) {
  size_t read_length = circular_buffer_peek_svarint(buffer, 0, value);
  if(read_length == 0) return 0;
  circular_buffer_skip_read(buffer, read_length);
  return read_length;
}

size_t circular_buffer_write_svarint(struct circular_buffer *buffer, int64_t value) {
  size_t write_length = circular_buffer_poke_svarint(buffer, 0, value);
  if(write_length == 0) return 0;
  circular_buffer_commit_write(buffer, write_length);
  return write_length;
}

