//
// Created by Michał Łaszczewski <michal@laszczewski.com> on 11/4/23.
//

#ifndef WASMOS_SOCKET_H
#define WASMOS_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#include <io/circular_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  SOCKET_FLAG_OPEN = 0x01,
  SOCKET_FLAG_READABLE = 0x02,
  SOCKET_FLAG_WRITABLE = 0x04,
  SOCKET_FLAG_ERROR = 0x08
};

typedef void (*socket_data_event_callback)(size_t);
typedef void (*socket_drain_event_callback)(size_t);

struct socket_info {
  struct circular_buffer input_buffer;
  struct circular_buffer output_buffer;
  size_t flush_threshold;
  size_t data_event_threshold;
  size_t drain_event_threshold;
  uint8_t flags;
  socket_data_event_callback data_event_callback;
  socket_drain_event_callback drain_event_callback;
};

struct sockets_table {
  size_t size;
  struct socket_info sockets[0];
};

struct socket_options {
  size_t input_buffer_size;
  size_t output_buffer_size;
  size_t flush_threshold;
  size_t data_event_threshold;
  size_t drain_event_threshold;
  uint8_t flags;
  new_length_callback input_new_length_callback;
  new_length_callback output_new_length_callback;
  socket_data_event_callback data_event_callback;
  socket_drain_event_callback drain_event_callback;
};

extern struct sockets_table *sockets_table;

/**
 * Get the sockets table pointer (exported for external access)
 * @return Pointer to the sockets table, or NULL if not initialized
 */
struct sockets_table* get_sockets_table(void);

/**
 * Initialize the sockets.
 *
 * @param initial_table_size The initial size of the sockets table.
 * @return 0 on success, -1 on failure.
 */
int sockets_init(size_t initial_table_size);

/**
 * Resize the sockets table.
 *
 * @param new_size The new size of the sockets table.
 */
int sockets_table_resize(size_t new_size);

/**
 * Finish the sockets - shorthand for sockets_table_resize(0)
 */
void sockets_done();

/**
 * Create a new socket.
 *
 * @param socket The new socket id or -1 to search for a free socket.
 * @return The new socket or -1 if no socket could be created.
 */
int socket_open(int socket, struct socket_options *options);

/**
 * Set socket options
 *
 * @param socket The socket to set options for.
 * @param options The options to set.
 */
void socket_set_options(int socket, struct socket_options *options);

/**
 * Get socket options
 *
 * @param socket The socket to get options for.
 * @param options The options to get.
 */
void socket_get_options(int socket, struct socket_options *options);

/**
 * Close the given socket.
 *
 * @param socket The socket to destroy.
 */
void socket_close(int socket);

/**
 * Get the socket info for the given socket.
 *
 * @param socket The socket to get info for.
 * @return The socket info.
 */
struct socket_info* get_socket_info(int socket);

/**
 * Get socket readable bytes.
 *
 * @param socket The socket to get info for.
 * @return The number of readable bytes.
 */
size_t socket_readable_bytes(int socket);

/**
 * Get socket writable bytes.
 *
 * @param socket The socket to get info for.
 * @return The number of writable bytes.
 */
size_t socket_writable_bytes(int socket);

/**
 * Reserve bytes for reading - increases buffer size so that readable bytes can be available.
 *
 * @param socket The socket to reserve bytes for.
 * @param size The number of bytes to reserve.
 * @return The number of bytes available for reading or writing after the call.
 */
size_t socket_reserve_for_reading(int socket, size_t size);

/**
 * Reserve bytes for writing - Reserves space for writing to the socket output buffer.
 *
 * @param socket The socket to reserve bytes for.
 * @param size The number of bytes to reserve.
 * @return The number of bytes available for writing after the call.
 */
size_t socket_reserve_for_writing(int socket, size_t size);

/**
 * Write data to the given socket.
 *
 * @param socket The socket to write to.
 * @param data The data to write.
 * @param length The length of the data to write.
 * @return The number of bytes written.
 */
size_t socket_write(int socket, const void *data, size_t length);

/**
 * Read data from the given socket.
 *
 * @param socket The socket to read from.
 * @param data The buffer to read to.
 * @param length The length of the buffer.
 * @return The number of bytes read.
 */
size_t socket_read(int socket, void *data, size_t length);

/**
 * Reads bytes from the socket input buffer without removing them.
 *
 * @param socket The socket to read from.
 * @param offset The offset to read from.
 * @param destination The destination of the bytes to read.
 * @param bytes The number of bytes to read.
 * @return The number of bytes read.
 */
size_t socket_peek(int socket, size_t offset, void *destination,  size_t bytes);

/**
 * Writes bytes to the socket output buffer without moving the write offset.
 *
 * @param socket The socket to write to.
 * @param offset The offset to write to.
 * @param source The source of the bytes to write.
 * @param bytes The number of bytes to write.
 * @return The number of bytes written.
 */
size_t socket_poke(int socket, size_t offset, const void *source, size_t bytes);

/**
 * Skip reading bytes from the socket input buffer.
 *
 * @param socket The socket to write to.
 * @param bytes The number of bytes to skip.
 * @return The number of bytes skipped.
 */
size_t socket_skip(int socket, size_t bytes);

/**
 * Move the write offset forward without writing any data.
 *
 * @param socket The socket to write to.
 * @param bytes The number of bytes to commit.
 * @return The number of bytes skipped.
 */
size_t socket_commit(int socket, size_t bytes);

/**
 * Returns socket input buffer capacity
 *
 * @param socket The socket.
 * @return The buffer capacity.
 */
size_t socket_read_capacity(int socket);

/**
 * Returns socket output buffer capacity
 *
 * @param socket The socket.
 * @return The buffer capacity.
 */
size_t socket_write_capacity(int socket);

/**
 * Flushes the socket output buffer.
 * @param socket The socket.
 */
void socket_flush(int socket);

/**
 * Reads unsigned variable length integer from the socket input buffer.
 *
 * @param buffer The buffer to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t socket_read_uvarint(int socket, uint64_t *value);

/**
 * Writes unsigned variable length integer to the socket output buffer.
 *
 * @param buffer The buffer to write to.
 * @param value The source of the integer.
 * @return The number of bytes written, or 0 if write failed.
 */
size_t socket_write_uvarint(int socket, const uint64_t value);

/**
 * Reads unsigned variable length integer from the socket input buffer without removing them.
 *
 * @param buffer The buffer to read from.
 * @param offset The offset to read from.
 * @param value The destination of the integer.
 * @return The number of bytes read, or 0 if read failed.
 */
size_t socket_peek_uvarint(int socket, size_t offset, uint64_t *value);

/**
 * Writes unsigned variable length integer to the socket output buffer without moving the write offset.
 *
 * @param buffer The buffer to write to.
 * @param offset The offset to write to.
 * @param value The source of the integer.
 * @return The number of bytes written, or 0 if write failed.
 */
size_t socket_poke_uvarint(int socket, size_t offset, const uint64_t value);

#ifdef __cplusplus
}
#endif

#endif //WASMOS_SOCKET_H
