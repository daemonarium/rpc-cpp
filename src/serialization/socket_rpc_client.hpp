#pragma once

#include "socket_rpc_client_base.hpp"
#include "socket_transport.hpp"
#include "compressed_codec.hpp"
#include "flat_materialized_codec.hpp"
#include <functional>

namespace serialization {

  // Thin derived socket RPC client. Inherits all control / registry / send
  // machinery from socket_rpc_client_base<Codec, Transport> and only adds the
  // public on_message<T> overloads. The handler signature is adapted to the
  // codec via Codec::handler_signature<T>:
  //   - compressed_codec / flat_materialized_codec -> void(T&) / void(T&, size_t)
  //   - flat_zero_copy_codec                     -> void(flat_view<T>&) / void(flat_view<T>&, size_t)
  // Control messages (rpc_request/response/error) are always materialized
  // and handled by the base via on_control_message / register_control_message_type.
  template <typename Codec, typename Transport = socket_c_transport>
  class socket_rpc_client : public socket_rpc_client_base<Codec, Transport> {
    public:
      using socket_rpc_client_base<Codec, Transport>::socket_rpc_client_base;

      template <typename T>
      void on_message(std::function<void(typename Codec::template handler_signature<T>)> handler) {
        this->channel().template on_message<T>(std::move(handler));
      }
      template <typename T>
      void on_message(std::function<void(typename Codec::template handler_signature<T>, size_t)> handler) {
        this->channel().template on_message<T>(std::move(handler));
      }
  };

  using compressed_socket_rpc_client = socket_rpc_client<compressed_codec>;
  using flat_socket_rpc_client = socket_rpc_client<flat_materialized_codec>;
  using flat_native_socket_rpc_client = socket_rpc_client<flat_materialized_codec, native_transport>;
  using flat_buffer_socket_rpc_client = socket_rpc_client<flat_materialized_codec, buffer_transport>;

} // namespace serialization
