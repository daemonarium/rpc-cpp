#pragma once

#include "socket_message_channel.hpp"
#include "socket_message_framing.hpp"
#include "socket_transport.hpp"
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
#include <type_traits>

namespace serialization {

  // Scalars and pointers do not save_offset(); objects, strings, and containers do.
  template <class T>
  constexpr bool rpc_arg_already_saved_offset =
    Binary<T> || (ContainerButNotString<T> && !Binary<T>) || Map<T> || String<T> || SimpleException<T> ||
    (UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> &&
     !SimpleException<T> && !Binary<T>);

  template <class T, class W>
  void serialize_flat_rpc_arg(W& writer, const T& arg) {
    serialize(writer, arg);
    if constexpr (!rpc_arg_already_saved_offset<T>) {
      writer.save_offset();
    }
  }

  // One argument matches serialize(std::tuple<T>) (nested element frame).
  // Several arguments stay flat: one checkpoint per scalar, pointer bodies
  // after every argument so read_rest does not consume a trailer.
  template <class W, class... Args>
  void write_rpc_args(W& writer, const Args&... args) {
    writer.begin_frame();
    if constexpr (sizeof...(Args) == 1) {
      writer.begin_frame();
      (serialize(writer, args), ...);
      writer.write_rest();
      writer.end_frame();
      writer.save_offset();
    } else if constexpr (sizeof...(Args) > 1) {
      (serialize_flat_rpc_arg(writer, args), ...);
      writer.write_rest();
    }
    writer.end_frame();
  }

  // Format-agnostic CRTP base for socket RPC clients. Owns the control /
  // registry / send machinery that is identical across all codecs:
  //   - the socket_message_framing<Codec, Transport> channel,
  //   - registration of control messages (rpc_response/error) via
  //     register_control_message_type (always materialized, regardless of
  //     the codec's user-message handler_signature),
  //   - the response / error handlers and reader() accessor used by
  //     rpc_call_mixin,
  //   - send_request_impl / send_method_request_impl.
  //
  // The thin derived socket_rpc_client<Codec, Transport> only adds the public
  // on_message<T> overloads, whose signature is adapted to the codec via
  // Codec::handler_signature<T>. Control messages always go through the
  // materialized on_control_message path provided here.
  template <typename Codec, typename Transport = socket_c_transport>
  class socket_rpc_client_base : public rpc_call_mixin<socket_rpc_client_base<Codec, Transport>> {
    public:
      using reader_type = typename Codec::reader_type;
      using writer_type = typename Codec::writer_type;
      using transport_type = Transport;

      explicit socket_rpc_client_base(Transport transport)
        : channel_(std::move(transport)) {

        // rpc_response / rpc_error are control messages: always materialized,
        // independent of the codec's user-message handler_signature.
        channel_.template on_control_message<rpc_response>([this](rpc_response& resp, size_t payload_size) {
          auto it = this->response_handlers_.find(resp.request_id);
          if(it == this->response_handlers_.end()) {
            throw std::runtime_error("Response handler " + std::to_string(resp.request_id) + " not found");
          }
          it->second(true, nullptr);
          this->response_handlers_.erase(it);
        });

        channel_.template on_control_message<rpc_error>([this](rpc_error& err, size_t) {
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

      // Backward-compatible ctor for socket_c_transport (socket_id).
      explicit socket_rpc_client_base(uint32_t socket_id)
        requires std::is_same_v<Transport, socket_c_transport>
        : socket_rpc_client_base(socket_c_transport(socket_id)) {}

      ~socket_rpc_client_base() {}

      void register_basic_types() {
        channel_.template register_control_message_type<rpc_request>();
        channel_.template register_control_message_type<rpc_response>();
        channel_.template register_control_message_type<rpc_error>();
        channel_.template register_exception_type<unknown_exception>();
      }

      void handle_data() {
        channel_.handle_data();
      }

      // Expose channel for user-defined message types (used by the derived
      // socket_rpc_client<Codec>::on_message, which adapts the handler
      // signature via Codec::handler_signature<T>).
      template <typename T> void register_message_type() { channel_.template register_message_type<T>(); }
      template <typename T> void send_message(const T& msg) { channel_.template send_message<T>(msg); }

      auto& channel() { return channel_; }

      reader_type& reader() { return channel_.reader(); }

    protected:
      template <typename... Args>
      void send_request_impl(std::uint64_t function_id, std::uint64_t request_id, Args... args) {
        auto& writer = channel_.template begin_send<rpc_request>(rpc_request{request_id, function_id});
        write_rpc_args(writer, args...);
        channel_.commit_send();
      }

      template <typename Id, typename... Args>
      void send_method_request_impl(std::uint64_t function_id, std::uint64_t request_id, Id id, Args... args) {
        auto& writer = channel_.template begin_send<rpc_request>(rpc_request{request_id, function_id});
        serialize(writer, id);
        write_rpc_args(writer, args...);
        channel_.commit_send();
      }

    private:
      friend class rpc_call_mixin<socket_rpc_client_base<Codec, Transport>>;

      socket_message_framing<Codec, Transport> channel_;
  };

} // namespace serialization
