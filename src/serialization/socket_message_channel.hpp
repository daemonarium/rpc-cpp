#pragma once

#include "circular_buffer_reader.hpp"
#include "circular_buffer_writer.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include <socket/socket.h>
#include <memory>
#include <unordered_map>
#include <functional>
#include <typeindex>
#include <vector>
#include <stdexcept>
#include <string>

#ifdef __EMSCRIPTEN__

extern "C" {
  __attribute__((import_module("waos"), import_name("waos_socket_flush")))
  void waos_socket_flush(std::uint32_t socket);

  __attribute__((import_module("waos"), import_name("waos_socket_input_drain")))
  void waos_socket_input_drain(std::uint32_t socket);
}

#else

extern "C" {
  void waos_socket_flush(std::uint32_t socket);
  void waos_socket_input_drain(std::uint32_t socket);
}

#endif

namespace serialization {

  class socket_message_channel {
    public:
      socket_message_channel(uint32_t socket_id)
        : socket_id_(socket_id) {
        socket_ = get_socket_info(socket_id_);
        reader_ = std::make_unique<circular_buffer_reader>(&socket_->input_buffer, 0);
        writer_ = std::make_unique<circular_buffer_writer>(&socket_->output_buffer, 0);
        writer_->register_serialize_exception_function<unknown_exception>();
        reader_->register_deserialize_exception_function<unknown_exception>();
        writer_->register_serialize_exception_function<std::runtime_error>();
        reader_->register_deserialize_exception_function<std::runtime_error>();
      }
      ~socket_message_channel() {}

      template <typename T>
      void register_message_type() {
        writer_->register_serialize_message_function<T>();

        size_t index = dispatch_table_.size();
        message_type_indexes_[std::type_index(typeid(T))] = index;

        dispatch_table_.push_back([this]() {
          T msg;
          deserialize(*reader_, msg);
          reader_->read_rest();

          size_t payload_size = current_msg_len_ - reader_->read_offset_;

          auto it = message_handlers_.find(std::type_index(typeid(T)));
          if(it != message_handlers_.end()) {
            it->second(&msg, payload_size);
          }
          // After handler returns, handle_data() does circular_buffer_skip_read(msg_len)
          // which automatically skips any unread payload
        });
      }

      // Send a complete message (no payload)
      template <typename T>
      void send_message(const T& message) {
        begin_send(message);
        commit_send();
      }

      // Begin sending a message with optional payload.
      // After this call, write payload data to writer(), then call commit_send().
      template <typename T>
      circular_buffer_writer& begin_send(const T& message) {
        auto type = std::type_index(typeid(T));
        auto it = message_type_indexes_.find(type);
        if(it == message_type_indexes_.end()) {
          throw std::runtime_error("Message type not registered for sending");
        }
        size_t index = it->second;

        // Reserve space for length prefix
        uint32_t placeholder = 0;
        circular_buffer_poke(&socket_->output_buffer, 0, &placeholder, sizeof(placeholder));
        writer_->write_offset_ = sizeof(placeholder);

        // Write message type index + serialized header
        writer_->write_u64(index);
        serialize(*writer_, message);
        writer_->write_rest();

        return *writer_;
      }

      // Commit a message started with begin_send. Writes length prefix and flushes.
      void commit_send() {
        uint32_t payload_len = writer_->write_offset_ - sizeof(uint32_t);
        circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
        circular_buffer_commit_write(&socket_->output_buffer, writer_->write_offset_);
        waos_socket_flush(socket_id_);
      }

      // Register a handler that ignores payload
      template <typename T>
      void on_message(std::function<void(T&)> handler) {
        message_handlers_[std::type_index(typeid(T))] = [handler](void* ptr, size_t) {
          T& msg = *reinterpret_cast<T*>(ptr);
          handler(msg);
        };
      }

      // Register a handler with payload size info.
      // Handler can read payload_size bytes from reader() if payload_size > 0.
      // Unread payload is automatically skipped after handler returns.
      template <typename T>
      void on_message(std::function<void(T&, size_t payload_size)> handler) {
        message_handlers_[std::type_index(typeid(T))] = [handler](void* ptr, size_t payload_size) {
          T& msg = *reinterpret_cast<T*>(ptr);
          handler(msg, payload_size);
        };
      }

      void handle_data() {
        while(true) {
          // Read one length-prefixed message from socket input
          uint32_t msg_len = 0;
          if(circular_buffer_readable_bytes(&socket_->input_buffer) < sizeof(msg_len)) {
            waos_socket_input_drain(socket_id_);
            return;
          }
          circular_buffer_peek(&socket_->input_buffer, 0, &msg_len, sizeof(msg_len));
          size_t available = circular_buffer_readable_bytes(&socket_->input_buffer);
          if(available < sizeof(msg_len) + msg_len) {
            circular_buffer_reserve_for_reading(&socket_->input_buffer, msg_len + sizeof(msg_len));
            waos_socket_input_drain(socket_id_);
            return;
          }

          // Consume length prefix
          circular_buffer_skip_read(&socket_->input_buffer, sizeof(msg_len));
          reader_->read_offset_ = 0;

          // Read message type index
          std::uint64_t index;
          reader_->read_u64(index);

          if(index >= dispatch_table_.size()) {
            circular_buffer_skip_read(&socket_->input_buffer, msg_len);
            throw std::runtime_error("Unknown message type index: " + std::to_string(index));
          }

          // Store message length for payload size calculation in dispatch
          current_msg_len_ = msg_len;

          // Dispatch to the correct handler
          dispatch_table_[index]();

          // Skip entire message (including any unread payload)
          circular_buffer_skip_read(&socket_->input_buffer, msg_len);

          if(!circular_buffer_readable_bytes(&socket_->input_buffer)) {
            waos_socket_input_drain(socket_id_);
            return;
          }
        }
      }

      circular_buffer_reader& reader() { return *reader_; }
      circular_buffer_writer& writer() { return *writer_; }

    private:
      uint32_t socket_id_;
      struct socket_info* socket_;
      std::unique_ptr<circular_buffer_reader> reader_;
      std::unique_ptr<circular_buffer_writer> writer_;

      // Dispatch table: index -> lambda that deserializes T and calls handler
      std::vector<std::function<void()>> dispatch_table_;

      // Message type index lookup for send_message
      std::unordered_map<std::type_index, size_t> message_type_indexes_;

      // User-registered handlers by type: (void* msg_ptr, size_t payload_size)
      std::unordered_map<std::type_index, std::function<void(void*, size_t)>> message_handlers_;

      // Current message length, set in handle_data() before dispatch
      size_t current_msg_len_ = 0;
  };

}
