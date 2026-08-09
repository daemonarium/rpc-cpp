#pragma once

#include "socket_message_channel.hpp"
#include "socket_message_framing.hpp"
#include "compressed_codec.hpp"
#include "rpc_server_registry.hpp"
#include "rpc_process_call_mixin.hpp"
#include "serialize.hpp"
#include "promise.hpp"
#include "rpc.hpp"
#include <memory>
#include <functional>
#include <exception>

namespace serialization {

  // RPC server over a socket, format-agnostic via Codec.
  //
  // Codec must be a socket-compatible codec (compressed_codec,
  // flat_materialized_codec, ...). The reader/writer types are taken from
  // Codec::reader_type / Codec::writer_type so rpc_server_registry and
  // rpc_process_call_mixin see the right types.
  template <typename Codec>
  class socket_rpc_server
    : public rpc_server_registry<typename Codec::reader_type, typename Codec::writer_type, socket_rpc_server<Codec>>,
      public rpc_process_call_mixin<socket_rpc_server<Codec>> {
    public:
      using reader_type = typename Codec::reader_type;
      using writer_type = typename Codec::writer_type;

      socket_rpc_server(uint32_t socket_id)
        : channel_(socket_id) {

        channel_.template on_message<rpc_request>([this](rpc_request& req, size_t payload_size) {
          handle_rpc_request(req, payload_size);
        });
      }
      ~socket_rpc_server() {}

      void register_basic_types() {
        channel_.template register_message_type<rpc_request>();
        channel_.template register_message_type<rpc_response>();
        channel_.template register_message_type<rpc_error>();
        channel_.template register_exception_type<unknown_exception>();
      }

      // Forward registration methods from rpc_server_registry
      template <typename T>
      void register_recursive(T value) {
        rpc_server_registry<reader_type, writer_type, socket_rpc_server<Codec>>::register_recursive(value, *this);
      }

      void handle_data() {
        channel_.handle_data();
      }

      // Expose channel for user-defined message types
      template <typename T> void register_message_type() { channel_.template register_message_type<T>(); }
      template <typename T> void on_message(std::function<void(T&)> handler) { channel_.template on_message<T>(handler); }
      template <typename T> void on_message(std::function<void(T&, size_t)> handler) { channel_.template on_message<T>(handler); }
      template <typename T> void send_message(const T& msg) { channel_.template send_message<T>(msg); }

      auto& channel() { return channel_; }

    private:
      friend class rpc_server_registry<reader_type, writer_type, socket_rpc_server<Codec>>;
      friend class rpc_process_call_mixin<socket_rpc_server<Codec>>;

      socket_message_framing<Codec> channel_;

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
  };

  using compressed_socket_rpc_server = socket_rpc_server<compressed_codec>;

} // namespace serialization
