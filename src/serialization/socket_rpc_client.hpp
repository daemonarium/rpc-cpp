#pragma once

#include "circular_buffer_reader.hpp"
#include "circular_buffer_writer.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "type_concepts.hpp"
#include "promise.hpp"
#include "rpc_exception.hpp"
#include <socket/socket.h>
#include <memory>
#include <unordered_map>
#include <functional>
#include <tuple>
#include <exception>
#include <string>

extern "C" {
  __attribute__((import_module("waos"), import_name("waos_socket_flush")))
  void waos_socket_flush(std::uint32_t socket);

  __attribute__((import_module("waos"), import_name("waos_socket_input_drain")))
  void waos_socket_input_drain(std::uint32_t socket);
}

namespace serialization {

  class socket_rpc_client {
    public:
      socket_rpc_client(uint32_t socket_id)
        : socket_id_(socket_id) {
        socket_ = get_socket_info(socket_id_);
        reader_ = std::make_unique<circular_buffer_reader>(&socket_->input_buffer, 0);
        writer_ = std::make_unique<circular_buffer_writer>(&socket_->output_buffer, 0);
      }
      ~socket_rpc_client() {}

      template <typename Rt, typename... Args>
      requires(!Void<Rt>)
      std::shared_ptr<promise<Rt>> call(std::uint64_t function_id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;
        printf("call request: request_id=%d, function_id=%d\n", request_id, function_id);
        response_handlers_[request_id] = [p, request_id, this](circular_buffer_reader& reader) {
          printf("call response: request_id=%d\n", request_id);
          Rt result;
          std::string error_message;
          bool success = read_result(reader, result, error_message);
          if(success) {
            printf("call response: request_id=%d, success=true\n", request_id);
            p->resolve(result);
          } else {
            printf("call response: request_id=%d, success=false\n", request_id);
            printf("call response: error_message=%s\n", error_message.c_str());
            p->reject(std::make_exception_ptr(rpc_exception(error_message)));
          }
          response_handlers_.erase(request_id);
        };

        send_request(request_id, function_id, args...);
        return p;
      }

      template <typename Rt, typename... Args>
      requires(Void<Rt>)
      std::shared_ptr<promise<void>> call(std::uint64_t function_id, Args... args) {
        auto p = std::make_shared<promise<void>>();
        std::uint64_t request_id = last_request_id_++;

        response_handlers_[request_id] = [p, request_id, this](circular_buffer_reader& reader) {          
          std::string error_message;
          bool success = read_result(reader, error_message);                    
          if(success) {
            p->resolve();
          } else {
            p->reject(std::make_exception_ptr(rpc_exception(error_message)));
          }
          response_handlers_.erase(request_id);
        };

        send_request(request_id, function_id, args...);
        return p;
      }

      template <typename Id, typename Rt, typename... Args>
      requires(!Void<Rt>)
      std::shared_ptr<promise<Rt>> call_method(std::uint64_t function_id, Id id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;
        
        response_handlers_[request_id] = [p, request_id, this](circular_buffer_reader& reader) {
          Rt result;
          std::string error_message;
          bool success = read_result(reader, result, error_message);
          if(success) {
            p->resolve(result);
          } else {
            p->reject(std::make_exception_ptr(rpc_exception(error_message)));
          }
          response_handlers_.erase(request_id);
        };

        send_method_request(request_id, function_id, id, args...);
        return p;
      }

      template <typename Id, typename Rt, typename... Args>
      requires(Void<Rt>)
      std::shared_ptr<promise<void>> call_method(std::uint64_t function_id, Id id, Args... args) {
        auto p = std::make_shared<promise<void>>();
        std::uint64_t request_id = last_request_id_++;
        
        response_handlers_[request_id] = [p, request_id, this](circular_buffer_reader& reader) {
          std::string error_message;
          bool success = read_result(reader, error_message);
          if(success) {
            p->resolve();
          } else {
            p->reject(std::make_exception_ptr(rpc_exception(error_message)));
          }
          response_handlers_.erase(request_id);
        };

        send_method_request(request_id, function_id, id, args...);
        return p;
      }

      // Handle incoming data from socket
      void handle_data() {
        while(true) {

          auto all = circular_buffer_readable_bytes(&socket_->input_buffer);
          char* buffer = new char[all];
          circular_buffer_peek(&socket_->input_buffer, 0, buffer, all);
          
          // Added while debugging
          printf("handle_data: buffer content:\n");
          const unsigned char* bytes = (unsigned char*)buffer;
          for (size_t i = 0; i < all; i += 16) {
            if(i>32 && i< all-32) continue;            
            printf("%04X: ", (unsigned int)i);
            for (size_t j = 0; j < 16; j++) {
              if (i + j < all) {
                printf("%02X ", bytes[i + j]);
              }
            }
            printf("\n");
          }
          delete[] buffer;
          
          // Read one length-prefixed request from socket input
          uint32_t req_len = 0;
          if (circular_buffer_readable_bytes(&socket_->input_buffer) < sizeof(req_len)) {
            waos_socket_input_drain(socket_id_);
            return; // not enough data yet
          }
          circular_buffer_peek(&socket_->input_buffer, 0, &req_len, sizeof(req_len));
          size_t available = circular_buffer_readable_bytes(&socket_->input_buffer);
          if (available < sizeof(req_len) + req_len) {
            printf("handle_data: not enough data yet, available=%d, req_len=%d\n", available, req_len);
            circular_buffer_reserve_for_reading(&socket_->input_buffer, req_len + sizeof(req_len));
            waos_socket_input_drain(socket_id_);
            return; // wait for full message
          }

          printf("handle_data: consuming length prefix!\n");
          // Consume length prefix
          circular_buffer_skip_read(&socket_->input_buffer, sizeof(req_len));
          // Set reader offset to 0
          reader_->read_offset_ = 0;

          printf("handle_data: handling response!\n");
          // Handle response
          handle_response(*reader_);

          // Skip read bytes
          circular_buffer_skip_read(&socket_->input_buffer, req_len);

          if(!circular_buffer_readable_bytes(&socket_->input_buffer)) {
            waos_socket_input_drain(socket_id_);
            return;
          }
        }
      }

    private:
      uint32_t socket_id_;
      struct socket_info* socket_;
      std::unique_ptr<circular_buffer_reader> reader_;
      std::unique_ptr<circular_buffer_writer> writer_;
      
      std::unordered_map<std::uint64_t, std::function<void(circular_buffer_reader&)>> response_handlers_;
      std::uint64_t last_request_id_ = 0;

      template <typename... Args>
      void send_request(std::uint64_t request_id, std::uint64_t function_id, Args... args) {
        // Reserve space for length prefix (uint32_t)
        uint32_t placeholder = 0;
        circular_buffer_poke(&socket_->output_buffer, 0, &placeholder, sizeof(placeholder));
        writer_->write_offset_ = sizeof(placeholder);

        // Write request_id
        writer_->write_u64(request_id);
        
        // Write function_id and args
        writer_->write_u64(function_id);
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(*writer_, args_tuple);
        writer_->write_rest();

        // Write length prefix and flush
        uint32_t payload_len = writer_->write_offset_ - sizeof(placeholder);
        circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
        circular_buffer_commit_write(&socket_->output_buffer, writer_->write_offset_);
        waos_socket_flush(socket_id_);
      }

      template <typename Id, typename... Args>
      void send_method_request(std::uint64_t request_id, std::uint64_t function_id, Id id, Args... args) {
        // Reserve space for length prefix (uint32_t)
        uint32_t placeholder = 0;
        circular_buffer_poke(&socket_->output_buffer, 0, &placeholder, sizeof(placeholder));
        writer_->write_offset_ = sizeof(placeholder);

        // Write request_id
        writer_->write_u64(request_id);
        
        // Write function_id, id, and args
        writer_->write_u64(function_id);
        serialize(*writer_, id);
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(*writer_, args_tuple);
        writer_->write_rest();

        // Write length prefix and flush
        uint32_t payload_len = writer_->write_offset_ - sizeof(placeholder);
        circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
        circular_buffer_commit_write(&socket_->output_buffer, writer_->write_offset_);
        waos_socket_flush(socket_id_);
      }

      template <typename Rt>
      bool read_result(circular_buffer_reader& reader, Rt& result, std::string& error_message) {
        bool success;
        reader.read_bool(success);
        if(!success) {
          reader.read_string(error_message);
          return false;
        }
        deserialize(reader, result);
        reader.read_rest();
        return true;
      }

      bool read_result(circular_buffer_reader& reader, std::string& error_message) {
        bool success;
        reader.read_bool(success);
        if(!success) {
          reader.read_string(error_message);
          return false;
        }
        return true;
      }

      void handle_response(circular_buffer_reader& reader) {
        std::uint64_t request_id;
        reader.read_u64(request_id);
        printf("handle_response: request_id=%d\n", request_id);
        if(response_handlers_.find(request_id) != response_handlers_.end()) {
          response_handlers_[request_id](reader);
        } else {
          printf("handle_response: response handler not found\n");
          throw std::runtime_error("Response handler " + std::to_string(request_id) + " not found");
        }
      }
  };

}