//
// Created by Michał Łaszczewski <michal@laszczewski.com> on 11/4/23.
//

#include <string.h>
#include <malloc.h>

#include "socket.h"

struct sockets_table *sockets_table;

__attribute__((export_name("waos_sockets_table")))
struct sockets_table* get_sockets_table() {
  return sockets_table;
}

int sockets_init(size_t initial_table_size) {
  sockets_table = malloc(sizeof(struct sockets_table) + initial_table_size * sizeof(struct socket_info));
  if(!sockets_table) return -1;
  sockets_table->size = initial_table_size;
  memset(sockets_table->sockets, 0, initial_table_size * sizeof(struct socket_info));
  return 0;
}

int sockets_table_resize(size_t new_size) {
  struct sockets_table *new_table = 0;
  if(new_size > 0) {
    new_table = malloc(sizeof(struct sockets_table) + new_size * sizeof(struct socket_info));
    if (!new_table) return -1;
  }
  new_table->size = new_size;
  size_t sockets_to_copy = sockets_table->size;
  if(new_size < sockets_to_copy) {
    sockets_to_copy = new_size;
    for(size_t i = new_size; i < sockets_table->size; i++) {
      if(sockets_table->sockets[i].flags & SOCKET_FLAG_OPEN) {
        socket_close(i);
      }
    }
  }
  if(sockets_to_copy > 0) {
    memcpy(new_table->sockets, sockets_table->sockets, sockets_to_copy * sizeof(struct socket_info));
  }
  if(new_size > sockets_table->size) {
    memset(new_table->sockets + sockets_table->size, 0, 
           (new_size - sockets_table->size) * sizeof(struct socket_info));
  }
  if(sockets_table) {
    free(sockets_table);
  }
  sockets_table = new_table;
  return 0;
}

void sockets_done() {
  sockets_table_resize(0);
}

int socket_search_position;

int socket_open(int socket, struct socket_options *options) {
  if(socket < 0) {
    int socket_search_start = socket_search_position;
    do {
      socket_search_position = (socket_search_position + 1) % sockets_table->size;
      if(!(sockets_table->sockets[socket_search_position].flags & SOCKET_FLAG_OPEN)) {
        socket = socket_search_position;
        break;
      }
    } while(socket_search_position != socket_search_start);
    if(socket < 0) {
      socket = sockets_table->size;
      sockets_table_resize(sockets_table->size * 2);
      socket_search_position = socket;
    }
  } else if(socket >= sockets_table->size) {
    sockets_table_resize(socket + 1);
  } else if(sockets_table->sockets[socket].flags & SOCKET_FLAG_OPEN) {
    return -1; // socket already open
  }
  struct socket_info *socket_info = sockets_table->sockets + socket;
  socket_info->flags = SOCKET_FLAG_OPEN | options->flags;
  circular_buffer_init(&socket_info->input_buffer, options->input_buffer_size, options->input_new_length_callback);
  circular_buffer_init(&socket_info->output_buffer, options->output_buffer_size, options->output_new_length_callback);
  socket_info->flush_threshold = options->flush_threshold;
  socket_info->data_event_threshold = options->data_event_threshold;
  socket_info->drain_event_threshold = options->drain_event_threshold;
  socket_info->data_event_callback = options->data_event_callback;
  socket_info->drain_event_callback = options->drain_event_callback;
  return socket;
}

struct socket_info* get_socket_info(int socket) {
  if(socket < 0 || socket >= sockets_table->size) {
    //printf(stderr, "Invalid socket number: %d\n", socket);
    __builtin_trap();
  };
  struct socket_info *socket_info = sockets_table->sockets + socket;
  if(!(socket_info->flags & SOCKET_FLAG_OPEN)) {
    //fprintf(stderr, "Socket not open: %d\n", socket);
    __builtin_trap();
  }
  return socket_info;
}

void socket_set_options(int socket, struct socket_options *options) {
  struct socket_info *socket_info = get_socket_info(socket);
  socket_info->flags = options->flags & SOCKET_FLAG_OPEN;
  circular_buffer_grow(&socket_info->input_buffer, options->input_buffer_size);
  circular_buffer_grow(&socket_info->output_buffer, options->output_buffer_size);
  if(options->input_new_length_callback) {
    socket_info->input_buffer.new_length = options->input_new_length_callback;
  }
  if(options->output_new_length_callback) {
    socket_info->output_buffer.new_length= options->output_new_length_callback;
  }
  socket_info->flush_threshold = options->flush_threshold;
  socket_info->data_event_threshold = options->data_event_threshold;
  socket_info->drain_event_threshold = options->drain_event_threshold;
  socket_info->data_event_callback = options->data_event_callback;
  socket_info->drain_event_callback = options->drain_event_callback;
}

void socket_get_options(int socket, struct socket_options *options) {
  struct socket_info *socket_info = get_socket_info(socket);
  options->flags = socket_info->flags;
  options->input_buffer_size = circular_buffer_capacity(&socket_info->input_buffer);
  options->output_buffer_size = circular_buffer_capacity(&socket_info->output_buffer);
  options->input_new_length_callback = socket_info->input_buffer.new_length;
  options->output_new_length_callback = socket_info->output_buffer.new_length;
  options->flush_threshold = socket_info->flush_threshold;
  options->data_event_threshold = socket_info->data_event_threshold;
  options->drain_event_threshold = socket_info->drain_event_threshold;
  options->data_event_callback = socket_info->data_event_callback;
  options->drain_event_callback = socket_info->drain_event_callback;
}

void socket_close(int socket) {
  struct socket_info *socket_info = get_socket_info(socket);
  if (socket_info->flags & SOCKET_FLAG_OPEN) {
    socket_info->flags = 0;
    circular_buffer_destroy(&socket_info->input_buffer);
    circular_buffer_destroy(&socket_info->output_buffer);
  }
}

size_t socket_readable_bytes(int socket) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_readable_bytes(&socket_info->input_buffer);
}

size_t socket_writable_bytes(int socket) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_writable_bytes(&socket_info->output_buffer);
}

size_t socket_reserve_for_reading(int socket, size_t size) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_reserve_for_reading(&socket_info->input_buffer, size);
}

size_t socket_reserve_for_writing(int socket, size_t size) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_reserve_for_writing(&socket_info->output_buffer, size);
}

size_t socket_write(int socket, const void *data, size_t length) {
  struct socket_info *socket_info = get_socket_info(socket);
  int written = circular_buffer_write(&socket_info->output_buffer, data, length);
  int readable_bytes = circular_buffer_readable_bytes(&socket_info->output_buffer);
  if(readable_bytes >= socket_info->flush_threshold) {
    socket_flush(socket);
  }
  return written;
}

size_t socket_read(int socket, void *data, size_t length) {
  struct socket_info *socket_info = get_socket_info(socket);
  int read = circular_buffer_read(&socket_info->input_buffer, data, length);
  int readable_bytes = circular_buffer_readable_bytes(&socket_info->input_buffer);
  return read;
}

size_t socket_peek(int socket, size_t offset, void *destination, size_t bytes) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_peek(&socket_info->input_buffer, offset, destination, bytes);
}

size_t socket_poke(int socket, size_t offset, const void *source, size_t bytes) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_poke(&socket_info->output_buffer, offset, source, bytes);
}

size_t socket_skip(int socket, size_t bytes) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_skip_read(&socket_info->input_buffer, bytes);
}

size_t socket_commit(int socket, size_t bytes) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_commit_write(&socket_info->output_buffer, bytes);
}

size_t socket_read_capacity(int socket) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_capacity(&socket_info->input_buffer);
}

size_t socket_write_capacity(int socket) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_capacity(&socket_info->output_buffer);
}

void socket_flush(int socket) __attribute__((
  __import_module__("waos"),
  __import_name__("waos_socket_flush")
));

__attribute__((export_name("waos_socket_handle_data")))
void socket_handle_data(int socket) {
  struct socket_info *socket_info = get_socket_info(socket);
  int data_size = circular_buffer_readable_bytes(&socket_info->input_buffer);
  if(socket_info->data_event_callback) {
    socket_info->data_event_callback(data_size);
  }
}

__attribute__((export_name("waos_socket_handle_drain")))
void socket_handle_drain(int socket) {
  struct socket_info *socket_info = get_socket_info(socket);
  int data_size = circular_buffer_readable_bytes(&socket_info->input_buffer);
  if(socket_info->drain_event_callback) {
    socket_info->drain_event_callback(data_size);
  }
}

size_t socket_read_uvarint(int socket, uint64_t *value) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_read_uvarint(&socket_info->input_buffer, value);
}

size_t socket_write_uvarint(int socket, const uint64_t value) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_write_uvarint(&socket_info->output_buffer, value);
}

size_t socket_peek_uvarint(int socket, size_t offset, uint64_t *value) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_peek_uvarint(&socket_info->input_buffer, offset, value);
}

size_t socket_poke_uvarint(int socket, size_t offset, const uint64_t value) {
  struct socket_info *socket_info = get_socket_info(socket);
  return circular_buffer_poke_uvarint(&socket_info->output_buffer, offset, value);
}
