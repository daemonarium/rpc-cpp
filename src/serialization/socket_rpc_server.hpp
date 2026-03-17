#pragma once

#include "socket_message_channel.hpp"
#include "rpc_server_registry.hpp"
#include "serialize.hpp"
#include "promise.hpp"
#include "rpc.hpp"
#include <memory>
#include <functional>
#include <exception>

namespace serialization {

  class socket_rpc_server : public rpc_server_registry<circular_buffer_reader, circular_buffer_writer, socket_rpc_server> {
    public:
      socket_rpc_server(uint32_t socket_id)
        : channel_(socket_id) {
        channel_.register_message_type<rpc_request>();
        channel_.register_message_type<rpc_response>();
        channel_.register_message_type<rpc_error>();

        channel_.on_message<rpc_request>([this](rpc_request& req, size_t payload_size) {
          handle_rpc_request(req, payload_size);
        });
      }
      ~socket_rpc_server() {}

      // Forward registration methods from rpc_server_registry
      template <typename T>
      void register_recursive(T value) {
        rpc_server_registry<circular_buffer_reader, circular_buffer_writer, socket_rpc_server>::register_recursive(value, *this);
      }

      void handle_data() {
        channel_.handle_data();
      }

      // Expose channel for user-defined message types
      template <typename T> void register_message_type() { channel_.register_message_type<T>(); }
      template <typename T> void on_message(std::function<void(T&)> handler) { channel_.on_message<T>(handler); }
      template <typename T> void on_message(std::function<void(T&, size_t)> handler) { channel_.on_message<T>(handler); }
      template <typename T> void send_message(const T& msg) { channel_.send_message(msg); }

      socket_message_channel& channel() { return channel_; }

    private:
      friend class rpc_server_registry<circular_buffer_reader, circular_buffer_writer, socket_rpc_server>;

      socket_message_channel channel_;

      void handle_rpc_request(rpc_request& req, size_t payload_size) {
        if(req.function_id >= this->functions_.size()) {
          send_error(req.request_id, std::runtime_error("Function not found"));
          return;
        }
        // The reader is positioned at the payload (serialized arguments).
        // functions_[id] reads args from reader, then calls process_call.
        this->functions_[req.function_id](channel_.reader(), channel_.writer(), req.request_id);
      }

      void send_response(uint64_t request_id) {
        channel_.send_message(rpc_response{request_id});
      }

      template <typename Rt>
      void send_response(uint64_t request_id, Rt& result) {
        auto& writer = channel_.begin_send(rpc_response{request_id});
        serialize(writer, result);
        writer.write_rest();
        channel_.commit_send();
      }

      void send_error(uint64_t request_id, const std::exception& e) {
        auto& writer = channel_.begin_send(rpc_error{request_id});
        try {
          writer.write_exception(e);
        } catch(unknown_exception& ex) {
          auto rt = unknown_exception(std::string("Unknown exception: ") + e.what());
          writer.write_exception(rt);
        }
        writer.write_rest();
        channel_.commit_send();
      }

      // process_call: non-void, non-promise return type
      template <typename Rt>
      requires(!PromisePointer<Rt>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, circular_buffer_writer& /*unused*/, uint64_t request_id
      ) {
        try {
          Rt result = function();
          send_response(request_id, result);
        } catch(const std::exception& e) {
          send_error(request_id, e);
        } catch(...) {
          send_error(request_id, unknown_exception("Unknown exception"));
        }
        return serialization::promise<void>::resolved();
      }

      // process_call: promise return type (async)
      template <typename Rt>
      requires(PromisePointer<Rt>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, circular_buffer_writer& /*unused*/, uint64_t request_id
      ) {
        auto p = std::make_shared<serialization::promise<void>>();
        try {
          Rt result = function();
          result->handle([this, request_id, p](typename Rt::element_type::promised_type& result) {
            send_response(request_id, result);
            p->resolve();
          }, [this, request_id, p](std::exception_ptr exception) {
            try {
              std::rethrow_exception(exception);
            } catch(const std::exception& e) {
              send_error(request_id, e);
            } catch(...) {
              send_error(request_id, unknown_exception("Unknown exception"));
            }
            p->resolve();
          });
        } catch(const std::exception& e) {
          send_error(request_id, e);
          p->resolve();
        } catch(...) {
          send_error(request_id, unknown_exception("Unknown exception"));
          p->resolve();
        }
        return p;
      }

      // process_call: void return type
      template <>
      inline std::shared_ptr<serialization::promise<void>> process_call<void>(
        std::function<void()> function, circular_buffer_writer& /*unused*/, uint64_t request_id
      ) {
        try {
          function();
          send_response(request_id);
        } catch(const std::exception& e) {
          send_error(request_id, e);
        } catch(...) {
          send_error(request_id, unknown_exception("Unknown exception"));
        }
        return serialization::promise<void>::resolved();
      }

      // process_call: promise<void> return type (async void)
      template <>
      inline std::shared_ptr<serialization::promise<void>> process_call<std::shared_ptr<serialization::promise<void>>>(
        std::function<std::shared_ptr<serialization::promise<void>>()> function,
        circular_buffer_writer& /*unused*/,
        uint64_t request_id
      ) {
        auto p = std::make_shared<serialization::promise<void>>();
        try {
          std::shared_ptr<serialization::promise<void>> result = function();
          result->handle([this, request_id, p]() {
            send_response(request_id);
            p->resolve();
          }, [this, request_id, p](std::exception_ptr exception) {
            try {
              std::rethrow_exception(exception);
            } catch(const std::exception& e) {
              send_error(request_id, e);
            } catch(...) {
              send_error(request_id, unknown_exception("Unknown exception"));
            }
            p->resolve();
          });
        } catch(const std::exception& e) {
          send_error(request_id, e);
          p->resolve();
        } catch(...) {
          send_error(request_id, unknown_exception("Unknown exception"));
          p->resolve();
        }
        return p;
      }
  };

}
