#ifndef CIRCULAR_BUFFER_CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_CIRCULAR_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*new_length_callback)(size_t);

/**
 * Struct that holds the data of the circular buffer.
 */
struct circular_buffer_data {
  size_t size;
  size_t read_offset;
  size_t write_offset;
  char data[0];
};

/**
 * Circular buffer is a resizable buffer that can be used to store data in a FIFO manner.
 * It is implemented as a ring buffer. It is not thread safe.
 * The buffer is implemented as a single contiguous block of memory.
 * The buffer is resized by doubling its size when it is full.
 * TODO: Resize buffer by halving its size when it is less than 1/4 full.
 */
struct circular_buffer {
  struct circular_buffer_data *data;
  new_length_callback new_length;
};

/**
 * Creates a new circular buffer with the given initial size.
 * Returns NULL if the buffer cannot be allocated.
 *
 * @param initial_size The initial size of the buffer.
 * @return The newly created buffer.
 */
struct circular_buffer *circular_buffer_create(size_t initial_size, new_length_callback new_length);

/**
 * Initializes the given circular buffer with the given initial size.
 * @param buffer
 * @param initial_size
 * @param new_length
 * @return initialized buffer or NULL if the buffer cannot be allocated.
 */
struct circular_buffer* circular_buffer_init(struct circular_buffer* buffer,
                                             size_t initial_size, new_length_callback new_length);

/**
 * Destroys the given circular buffer created with circular_buffer_create.
 * Frees both the data and the buffer struct itself.
 *
 * @param buffer The buffer to destroy.
 */
void circular_buffer_destroy(struct circular_buffer *buffer);

/**
 * Deinitializes the given circular buffer initialized with circular_buffer_init.
 * Frees only the data, NOT the buffer struct (which is owned by the caller).
 *
 * @param buffer The buffer to deinitialize.
 */
void circular_buffer_deinit(struct circular_buffer *buffer);

/**
 * Returns the number of bytes that can be read from the buffer.
 *
 * @param buffer The buffer to read from.
 * @return The number of bytes that can be read from the buffer.
 */
size_t circular_buffer_readable_bytes(struct circular_buffer *buffer);

/**
 * Returns the number of bytes that can be written to the buffer.
 *
 * @param buffer The buffer to write to.
 * @return The number of bytes that can be written to the buffer after the reservation.
 */
size_t circular_buffer_writable_bytes(struct circular_buffer *buffer);

/**
 * Reserve space for reading from the buffer.
 *
 * @param buffer The buffer to read from.
 * @param bytes The number of bytes to reserve.
 * @return The number of bytes available for reading or writing after the call.
 */
size_t circular_buffer_reserve_for_reading(struct circular_buffer *buffer, size_t bytes);

/**
 * Reserves space for writing to the buffer.
 *
 * @param buffer The buffer to write to.
 * @param bytes The number of bytes to reserve.
 */
size_t circular_buffer_reserve_for_writing(struct circular_buffer *buffer, size_t bytes);

/**
 * Write bytes to the buffer
 *
 * @param buffer The buffer to write to.
 * @param source The source of the bytes to write.
 * @param bytes The number of bytes to write.
 */
size_t circular_buffer_write(struct circular_buffer *buffer, const void *source, size_t bytes);

/**
 * Read bytes from the buffer
 *
 * @param buffer The buffer to read from.
 * @param destination The destination of the bytes to read.
 * @param bytes The number of bytes to read.
 */
size_t circular_buffer_read(struct circular_buffer *buffer, void *destination, size_t bytes);

/**
 * Returns buffer capacity.
 *
 * @param buffer The buffer to read from.
 * @return The buffer capacity.
 */
size_t circular_buffer_capacity(struct circular_buffer *buffer);

/**
 * Grow the buffer to the given size.
 *
 * @param buffer The buffer to grow.
 * @param size The new size of the buffer.
 * @return The buffer capacity.
 */
size_t circular_buffer_grow(struct circular_buffer *buffer, size_t size);

/**
 * Resets the buffer and clears all data.
 *
 * @param buffer The buffer to reset.
 */
void circular_buffer_clear(struct circular_buffer *buffer);

/**
 * Skip reading bytes from the buffer.
 *
 * @param buffer The buffer to read from.
 * @param bytes The number of bytes to skip.
 * @return The number of bytes skipped.
 */
size_t circular_buffer_skip_read(struct circular_buffer *buffer, size_t bytes);

/**
 * Move the write offset forward without writing any data.
 *
 * @param buffer The buffer to write to.
 * @param bytes The number of bytes to skip.
 * @return The number of bytes skipped.
 */
size_t circular_buffer_commit_write(struct circular_buffer *buffer, size_t bytes);

/**
 * Reads bytes from the buffer without removing them.
 *
 * @param buffer The buffer to read from.
 * @param offset The offset to read from.
 * @param destination The destination of the bytes to read.
 * @param bytes The number of bytes to read.
 * @return The number of bytes read.
 */
size_t circular_buffer_peek(struct circular_buffer *buffer, size_t offset, void *destination, size_t bytes);

/**
 * One contiguous physical slice of a logical readable range.
 * Pointers are into buffer storage and remain valid only until skip_read/grow/write
 * that changes layout (single-thread handler lifetime).
 */
struct circular_buffer_segment {
  const char *ptr;
  size_t len;
};

/**
 * Describes logical range [offset, offset+len) in peek space (0 = current read_offset).
 * Writes 1 or 2 segments into out. Returns number of segments, or 0 on OOB/error.
 * When return==1, out[1].len is 0.
 */
int circular_buffer_get_segments(
    struct circular_buffer *buffer,
    size_t offset,
    size_t len,
    struct circular_buffer_segment out[2]);

/**
 * Returns non-zero if [offset, offset+len) lies in one contiguous physical slice.
 */
int circular_buffer_region_contiguous(
    struct circular_buffer *buffer, size_t offset, size_t len);

/**
 * If the range is contiguous, sets *ptr to its start and returns len.
 * Otherwise sets *ptr to NULL and returns 0.
 */
size_t circular_buffer_try_contiguous_ptr(
    struct circular_buffer *buffer, size_t offset, size_t len, const char **ptr);

/**
 * Writes bytes to the buffer without moving the write offset.
 *
 * @param buffer The buffer to write to.
 * @param offset The offset to write to.
 * @param source The source of the bytes to write.
 * @param bytes The number of bytes to write.
 * @return The number of bytes written.
 */
size_t circular_buffer_poke(struct circular_buffer *buffer, size_t offset, const void *source,  size_t bytes);

/**
 * Reads unsigned 64-bit variable length integer from the buffer.
 *
 * @param buffer The buffer to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_read_uvarint64(struct circular_buffer *buffer, uint64_t *value);

/**
 * Writes unsigned 64-bit variable length integer to the buffer.
 *
 * @param buffer The buffer to write to.
 * @param value The source of the integer.
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_write_uvarint64(struct circular_buffer *buffer, uint64_t value);

/**
 * Reads signed 64-bit variable length integer from the buffer.
 *
 * @param buffer The buffer to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_read_svarint64(struct circular_buffer *buffer, int64_t *value);

/**
 * Writes signed 64-bit variable length integer to the buffer.
 *
 * @param buffer The buffer to write to.
 * @param value The source of the integer.
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_write_svarint64(struct circular_buffer *buffer, int64_t value);

/**
 * Reads unsigned 32-bit variable length integer from the buffer.
 *
 * @param buffer The buffer to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_read_uvarint32(struct circular_buffer *buffer, uint32_t *value);

/**
 * Writes unsigned 32-bit variable length integer to the buffer.
 *
 * @param buffer The buffer to write to.
 * @param value The source of the integer.
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_write_uvarint32(struct circular_buffer *buffer, uint32_t value);

/**
 * Reads signed 32-bit variable length integer from the buffer.
 *
 * @param buffer The buffer to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_read_svarint32(struct circular_buffer *buffer, int32_t *value);

/**
 * Writes signed 32-bit variable length integer to the buffer.
 *
 * @param buffer The buffer to write to.
 * @param value The source of the integer.
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_write_svarint32(struct circular_buffer *buffer, int32_t value);

/**
 * Reads unsigned 16-bit variable length integer from the buffer.
 *
 * @param buffer The buffer to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_read_uvarint16(struct circular_buffer *buffer, uint16_t *value);

/**
 * Writes unsigned 16-bit variable length integer to the buffer.
 *
 * @param buffer The buffer to write to.
 * @param value The source of the integer.
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_write_uvarint16(struct circular_buffer *buffer, uint16_t value);

/**
 * Reads signed 16-bit variable length integer from the buffer.
 *
 * @param buffer The buffer to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_read_svarint16(struct circular_buffer *buffer, int16_t *value);

/**
 * Writes signed 16-bit variable length integer to the buffer.
 *
 * @param buffer The buffer to write to.
 * @param value The source of the integer.
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_write_svarint16(struct circular_buffer *buffer, int16_t value);

/**
 * Reads unsigned 64-bit variable length integer from the buffer without removing it.
 *
 * @param buffer The buffer to read from.
 * @param offset The offset to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_peek_uvarint64(struct circular_buffer *buffer, size_t offset, uint64_t *value);

/**
 * Reads signed 64-bit variable length integer from the buffer without removing it.
 *
 * @param buffer The buffer to read from.
 * @param offset The offset to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_peek_svarint64(struct circular_buffer *buffer, size_t offset, int64_t *value);

/**
 * Reads unsigned 32-bit variable length integer from the buffer without removing it.
 *
 * @param buffer The buffer to read from.
 * @param offset The offset to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_peek_uvarint32(struct circular_buffer *buffer, size_t offset, uint32_t *value);

/**
 * Reads signed 32-bit variable length integer from the buffer without removing it.
 *
 * @param buffer The buffer to read from.
 * @param offset The offset to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_peek_svarint32(struct circular_buffer *buffer, size_t offset, int32_t *value);

/**
 * Reads unsigned 16-bit variable length integer from the buffer without removing it.
 *
 * @param buffer The buffer to read from.
 * @param offset The offset to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_peek_uvarint16(struct circular_buffer *buffer, size_t offset, uint16_t *value);

/**
 * Reads signed 16-bit variable length integer from the buffer without removing it.
 *
 * @param buffer The buffer to read from.
 * @param offset The offset to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t circular_buffer_peek_svarint16(struct circular_buffer *buffer, size_t offset, int16_t *value);

/**
 * Writes unsigned 64-bit variable length integer to the buffer without moving the write offset.
 * @param buffer
 * @param offset
 * @param value
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_poke_uvarint64(struct circular_buffer *buffer, size_t offset, uint64_t value);

/**
 * Writes signed 64-bit variable length integer to the buffer without moving the write offset.
 * @param buffer
 * @param offset
 * @param value
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_poke_svarint64(struct circular_buffer *buffer, size_t offset, int64_t value);

/**
 * Writes unsigned 32-bit variable length integer to the buffer without moving the write offset.
 * @param buffer
 * @param offset
 * @param value
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_poke_uvarint32(struct circular_buffer *buffer, size_t offset, uint32_t value);

/**
 * Writes signed 32-bit variable length integer to the buffer without moving the write offset.
 * @param buffer
 * @param offset
 * @param value
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_poke_svarint32(struct circular_buffer *buffer, size_t offset, int32_t value);

/**
 * Writes unsigned 16-bit variable length integer to the buffer without moving the write offset.
 * @param buffer
 * @param offset
 * @param value
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_poke_uvarint16(struct circular_buffer *buffer, size_t offset, uint16_t value);

/**
 * Writes signed 16-bit variable length integer to the buffer without moving the write offset.
 * @param buffer
 * @param offset
 * @param value
 * @return The number of bytes written, or 0 if write failed.
 */
size_t circular_buffer_poke_svarint16(struct circular_buffer *buffer, size_t offset, int16_t value);

#ifdef __cplusplus
}
#endif

#endif //CIRCULAR_BUFFER_CIRCULAR_BUFFER_H
