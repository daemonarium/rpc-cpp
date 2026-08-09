#pragma once

#include "socket_message_channel.hpp"
#include "socket_message_framing.hpp"
#include "compressed_codec.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "promise.hpp"
#include "rpc.hpp"
#include "rpc_call_mixin.hpp"
#include <memory>
#include <unordered_map>
#include <functional>
#include <tuple>
#include <exception>
#include <string>

namespace serialization {

  // RPC client over a socket, format-agnostic via Codec.
  //
  // Inherits call/call_method from rpc_call_mixin and implements reader()
  // (via channel_.reader()) and send_request_impl / send_method_request_impl
  // (via channel_.begin_send / commit_send).
  template <typename Codec>
  class socket_rpc_client : public rpc_call_mixin<socket_rpc_client<Codec>> {
    public:
      using reader_type = typename Codec::reader_type;
      using writer_type = typename Codec::writer_type;

      socket_rpc_client(uint32_t socket_id)
        : channel_(socket_id) {

        channel_.template on_message<rpc_response>([this](rpc_response& resp, size_t payload_size) {
          auto it = this->response_handlers_.find(resp.request_id);
          if(it == this->response_handlers_.end()) {
            throw std::runtime_error("Response handler " + std::to_string(resp.request_id) + " not found");
          }
          it->second(true, nullptr);
          this->response_handlers_.erase(it);
        });

        channel_.template on_message<rpc_error>([this](rpc_error& err, size_t) {
          auto it = this->response_handlers_.find(err.request_id);
          if(it == this->response_handlers_.end()) {
            throw std::runtime_error("Response handler " + std::to_string(err.request_id) + " not found");
          }
          std::exception_ptr eptr = channel_.reader().read_exception();
          channel_.reader().read_rest();
          it->second(false, eptr);
          this->response_handlers_.erase(it);
        });
      }
      ~socket_rpc_client() {}

      void register_basic_types() {
        channel_.template register_message_type<rpc_request>();
        channel_.template register_message_type<rpc_response>();
        channel_.template register_message_type<rpc_error>();
        channel_.template register_exception_type<unknown_exception>();
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

      reader_type& reader() { return channel_.reader(); }

    private:
      friend class rpc_call_mixin<socket_rpc_client<Codec>>;

      socket_message_framing<Codec> channel_;

      template <typename... Args>
      void send_request_impl(std::uint64_t function_id, std::uint64_t request_id, Args... args) {
        auto& writer = channel_.template begin_send<rpc_request>(rpc_request{request_id, function_id});
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(writer, args_tuple);
        writer.write_rest();
        channel_.commit_send();
      }

      template <typename Id, typename... Args>
      void send_method_request_impl(std::uint64_t function_id, std::uint64_t request_id, Id id, Args... args) {
        auto& writer = channel_.template begin_send<rpc_request>(rpc_request{request_id, function_id});
        serialize(writer, id);
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(writer, args_tuple);
        writer.write_rest();
        channel_.commit_send();
      }
  };

  using compressed_socket_rpc_client = socket_rpc_client<compressed_codec>;

} // namespace serialization
