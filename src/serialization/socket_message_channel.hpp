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

extern "C" {
  __attribute__((import_module("waos"), import_name("waos_socket_flush")))
  void waos_socket_flush(std::uint32_t socket);

  __attribute__((import_module("waos"), import_name("waos_socket_input_drain")))
  void waos_socket_input_drain(std::uint32_t socket);
}

namespace serialization {

  class socket_message_channel {
    public:
      socket_message_channel(uint32_t socket_id)
        : socket_id_(socket_id) {
        socket_ = get_socket_info(socket_id_);
        reader_ = std::make_unique<circular_buffer_reader>(&socket_->input_buffer, 0);
        writer_ = std::make_unique<circular_buffer_writer>(&socket_->output_buffer, 0);
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

          auto it = message_handlers_.find(std::type_index(typeid(T)));
          if(it != message_handlers_.end()) {
            it->second(&msg);
          }
        });
      }

      template <typename T>
      void send_message(const T& message) {
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

        // Write message type index + serialized data
        writer_->write_u64(index);
        serialize(*writer_, message);
        writer_->write_rest();

        // Write length prefix and flush
        uint32_t payload_len = writer_->write_offset_ - sizeof(placeholder);
        circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
        circular_buffer_commit_write(&socket_->output_buffer, writer_->write_offset_);
        waos_socket_flush(socket_id_);
      }

      template <typename T>
      void on_message(std::function<void(T&)> handler) {
        message_handlers_[std::type_index(typeid(T))] = [handler](void* ptr) {
          T& msg = *reinterpret_cast<T*>(ptr);
          handler(msg);
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
            // Skip past this message's data
            circular_buffer_skip_read(&socket_->input_buffer, msg_len);
            throw std::runtime_error("Unknown message type index: " + std::to_string(index));
          }

          // Dispatch to the correct handler
          dispatch_table_[index]();

          // Skip read bytes
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

      // User-registered handlers by type
      std::unordered_map<std::type_index, std::function<void(void*)>> message_handlers_;
  };

}
