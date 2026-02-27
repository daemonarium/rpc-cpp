#pragma once

#include "circular_buffer_reader.hpp"
#include "circular_buffer_writer.hpp"
#include "rpc_server_registry.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "type_concepts.hpp"
#include "promise.hpp"
#include <socket/socket.h>
#include <memory>
#include <functional>
#include <exception>
#include <string>

extern "C" {
  __attribute__((import_module("waos"), import_name("waos_socket_flush")))
  void waos_socket_flush(std::uint32_t socket);

  __attribute__((import_module("waos"), import_name("waos_socket_input_drain")))
  void waos_socket_input_drain(std::uint32_t socket);
}

namespace serialization {

  class socket_rpc_server : public rpc_server_registry<circular_buffer_reader, circular_buffer_writer, socket_rpc_server> {
    public:
      socket_rpc_server(uint32_t socket_id)
        : socket_id_(socket_id) {
        socket_ = get_socket_info(socket_id_);
        reader_ = std::make_unique<circular_buffer_reader>(&socket_->input_buffer, 0);
        writer_ = std::make_unique<circular_buffer_writer>(&socket_->output_buffer, 0);
      }
      ~socket_rpc_server() {}

      // Forward registration methods from rpc_server_registry
      template <typename T>
      void register_recursive(T value) {
        rpc_server_registry<circular_buffer_reader, circular_buffer_writer, socket_rpc_server>::register_recursive(value, *this);
      }

      // Handle incoming data from socket
      void handle_data() {
        while(true) {
          // Read one length-prefixed request from socket input
          uint32_t req_len = 0;
          if (circular_buffer_readable_bytes(&socket_->input_buffer) < sizeof(req_len)) {
            waos_socket_input_drain(socket_id_);
            return; // not enough data yet
          }
          circular_buffer_peek(&socket_->input_buffer, 0, &req_len, sizeof(req_len));
          size_t available = circular_buffer_readable_bytes(&socket_->input_buffer);
          if (available < sizeof(req_len) + req_len) {
            circular_buffer_reserve_for_reading(&socket_->input_buffer, req_len + sizeof(req_len));
            waos_socket_input_drain(socket_id_);
            return; // wait for full message
          }

          // Consume length prefix
          circular_buffer_skip_read(&socket_->input_buffer, sizeof(req_len));
          // Set reader offset to 0
          reader_->read_offset_ = 0;

          // Handle call
          auto promise = handle_call(*reader_, *writer_);

          // Skip read bytes
          circular_buffer_skip_read(&socket_->input_buffer, req_len);

          // Wait for promise to resolve before processing next message
          promise->on_resolved([this]() {
            // Length prefix is already written by process_call
            // Just need to ensure we're done
          });

          if(!circular_buffer_readable_bytes(&socket_->input_buffer)) {
            waos_socket_input_drain(socket_id_);
            return;
          }
        }
      }

      std::shared_ptr<serialization::promise<void>> handle_call(circular_buffer_reader& reader, circular_buffer_writer& writer) {
        std::uint64_t request_id;
        reader.read_u64(request_id);        
        std::uint64_t function_id;
        reader.read_u64(function_id);
        if (function_id >= this->functions_.size()) {
          // Reserve space for response length prefix
          uint32_t payload_len = 0;
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          writer.write_offset_ = sizeof(payload_len);
          
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string("Function not found");
          writer.write_rest();
          
          // Write length prefix and flush
          payload_len = writer.write_offset_ - sizeof(payload_len);
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
          waos_socket_flush(socket_id_);
          
          return serialization::promise<void>::resolved();
        }
        return this->functions_[function_id](reader, writer, request_id);
      }

    private:
      friend class rpc_server_registry<circular_buffer_reader, circular_buffer_writer, socket_rpc_server>;

      uint32_t socket_id_;
      struct socket_info* socket_;
      std::unique_ptr<circular_buffer_reader> reader_;
      std::unique_ptr<circular_buffer_writer> writer_;

      // Implement process_call similar to rpc_server but with length prefix handling
      template <typename Rt> 
      requires(!PromisePointer<Rt>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, circular_buffer_writer& writer, uint64_t request_id
      ) {
        // Reserve space for response length prefix        
        
        try {
          Rt result = function();

          uint32_t payload_len = 0;
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          writer.write_offset_ = sizeof(payload_len);

          writer.write_u64(request_id);
          writer.write_bool(true);
          serialize(writer, result);
          writer.write_rest();
          
          // Write length prefix and flush
          payload_len = writer.write_offset_ - sizeof(payload_len);
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
          waos_socket_flush(socket_id_);
        } catch(const std::exception& e) {
          uint32_t payload_len = 0;
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          writer.write_offset_ = sizeof(payload_len);

          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(e.what());
          writer.write_rest();
          
          // Write length prefix and flush
          payload_len = writer.write_offset_ - sizeof(payload_len);
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
          waos_socket_flush(socket_id_);
        } catch(...) {
          std::string error_msg = "Unknown non-standard exception";
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(error_msg);
          writer.write_rest();
          
          // Write length prefix and flush
          uint32_t payload_len = writer.write_offset_ - sizeof(payload_len);
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
          waos_socket_flush(socket_id_);
        }
        return serialization::promise<void>::resolved();
      }

      template <typename Rt> 
      requires(PromisePointer<Rt>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, circular_buffer_writer& writer, uint64_t request_id
      ) {
        auto p = std::make_shared<serialization::promise<void>>();
        

        try {
          Rt result = function();
          result->handle([this, request_id, p](Rt::element_type::promised_type& result) {
            // Reserve space for response length prefix
            uint32_t payload_len = 0;
            circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
            writer_->write_offset_ = sizeof(payload_len);

            writer_->write_u64(request_id);
            writer_->write_bool(true);
            serialize(*writer_, result);
            writer_->write_rest();
            
            // Write length prefix and flush
            payload_len = writer_->write_offset_ - sizeof(payload_len);
            circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
            circular_buffer_commit_write(&socket_->output_buffer, writer_->write_offset_);
            waos_socket_flush(socket_id_);
            
            p->resolve();
          }, [&](std::exception_ptr exception) {
            uint32_t payload_len = 0;
            circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
            writer.write_offset_ = sizeof(payload_len);

            writer.write_u64(request_id);
            try {
              std::rethrow_exception(exception);
            } catch(const std::exception& e) {
              writer.write_bool(false);
              writer.write_string(e.what());
            } catch(...) {
              std::string error_msg = "Unknown non-standard exception";
              writer.write_bool(false);
              writer.write_string(error_msg);
            }
            writer.write_rest();
            
            // Write length prefix and flush
            payload_len = writer.write_offset_ - sizeof(payload_len);
            circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
            circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
            waos_socket_flush(socket_id_);
            
            p->resolve();
          });
        } catch(const std::exception& e) {
          uint32_t payload_len = 0;
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          writer.write_offset_ = sizeof(payload_len);

          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(e.what());
          writer.write_rest();
          
          // Write length prefix and flush
          payload_len = writer_->write_offset_ - sizeof(payload_len);
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
          waos_socket_flush(socket_id_);
          
          p->resolve();
        } catch(...) {
          std::string error_msg = "Unknown non-standard exception";
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(error_msg);
          writer.write_rest();
          
          // Write length prefix and flush
          uint32_t payload_len = writer.write_offset_ - sizeof(payload_len);
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
          waos_socket_flush(socket_id_);
          
          p->resolve();
        }
        return p;
      }
      
      template <>
      inline std::shared_ptr<serialization::promise<void>> process_call<void>(std::function<void()> function, circular_buffer_writer& writer, uint64_t request_id) {
        
        try {
          function();

          // Reserve space for response length prefix
          uint32_t payload_len = 0;
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          writer_->write_offset_ = sizeof(payload_len);

          writer.write_u64(request_id);
          writer.write_bool(true);
          writer.write_rest();
          
          // Write length prefix and flush
          payload_len = writer.write_offset_ - sizeof(payload_len);
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
          waos_socket_flush(socket_id_);
        } catch(const std::exception& e) {
          uint32_t payload_len = 0;
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          writer.write_offset_ = sizeof(payload_len);

          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(e.what());
          writer.write_rest();
          
          // Write length prefix and flush
          payload_len = writer.write_offset_ - sizeof(payload_len);
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
          waos_socket_flush(socket_id_);
        } catch(...) {
          std::string error_msg = "Unknown non-standard exception";
          uint32_t payload_len = 0;
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          writer.write_offset_ = sizeof(payload_len);

          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(error_msg);
          writer.write_rest();
          
          // Write length prefix and flush
          payload_len = writer.write_offset_ - sizeof(payload_len);
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
          waos_socket_flush(socket_id_);
        }
        return serialization::promise<void>::resolved();
      }

      template <> 
      inline std::shared_ptr<serialization::promise<void>> process_call<std::shared_ptr<serialization::promise<void>>>(
        std::function<std::shared_ptr<serialization::promise<void>>()> function,
        circular_buffer_writer& writer,
        uint64_t request_id
      ) {
        auto p = std::make_shared<serialization::promise<void>>();
        
        try {
          std::shared_ptr<serialization::promise<void>> result = function();
          result->handle([this, request_id, p]() {
            // Reserve space for response length prefix
            uint32_t payload_len = 0;
            circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
            writer_->write_offset_ = sizeof(payload_len);

            writer_->write_u64(request_id);
            writer_->write_bool(true);
            writer_->write_rest();
            
            // Write length prefix and flush
            payload_len = writer_->write_offset_ - sizeof(payload_len);
            circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
            circular_buffer_commit_write(&socket_->output_buffer, writer_->write_offset_);
            waos_socket_flush(socket_id_);
            
            p->resolve();
          }, [this, request_id, p](std::exception_ptr exception) {
            uint32_t payload_len = 0;
            circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
            writer_->write_offset_ = sizeof(payload_len);

            writer_->write_u64(request_id);
            try {
              std::rethrow_exception(exception);
            } catch(const std::exception& e) {
              writer_->write_bool(false);
              writer_->write_string(e.what());
            } catch(...) {
              std::string error_msg = "Unknown non-standard exception";
              writer_->write_bool(false);
              writer_->write_string(error_msg);
            }
            writer_->write_rest();
            
            // Write length prefix and flush
            payload_len = writer_->write_offset_ - sizeof(payload_len);
            circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
            circular_buffer_commit_write(&socket_->output_buffer, writer_->write_offset_);
            waos_socket_flush(socket_id_);
            
            p->resolve();
          });
        } catch(const std::exception& e) {
          uint32_t payload_len = 0;
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          writer.write_offset_ = sizeof(payload_len);

          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(e.what());
          writer.write_rest();
          
          // Write length prefix and flush
          payload_len = writer.write_offset_ - sizeof(payload_len);
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
          waos_socket_flush(socket_id_);
          
          p->resolve();
        } catch(...) {
          std::string error_msg = "Unknown non-standard exception";
          uint32_t payload_len = 0;
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          writer.write_offset_ = sizeof(payload_len);

          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(error_msg);
          writer.write_rest();
          
          // Write length prefix and flush
          payload_len = writer.write_offset_ - sizeof(payload_len);
          circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
          circular_buffer_commit_write(&socket_->output_buffer, writer.write_offset_);
          waos_socket_flush(socket_id_);
          
          p->resolve();
        }
        return p;
      }
  };

}
