#pragma once

#include "socket_rpc_server_base.hpp"
#include "compressed_codec.hpp"
#include "flat_materialized_codec.hpp"
#include <functional>

namespace serialization {

  // Thin derived socket RPC server. Inherits all control / registry / send
  // machinery from socket_rpc_server_base<Codec> and only adds the public
  // on_message<T> overloads. The handler signature is adapted to the codec
  // via Codec::handler_signature<T>:
  //   - compressed_codec / flat_materialized_codec -> void(T&) / void(T&, size_t)
  //   - flat_zero_copy_codec                     -> void(flat_view<T>&) / void(flat_view<T>&, size_t)
  // Control messages (rpc_request/response/error) are always materialized
  // and handled by the base via on_control_message / register_control_message_type.
  template <typename Codec>
  class socket_rpc_server : public socket_rpc_server_base<Codec> {
    public:
      using socket_rpc_server_base<Codec>::socket_rpc_server_base;

      template <typename T>
      void on_message(std::function<void(typename Codec::template handler_signature<T>)> handler) {
        this->channel().template on_message<T>(std::move(handler));
      }
      template <typename T>
      void on_message(std::function<void(typename Codec::template handler_signature<T>, size_t)> handler) {
        this->channel().template on_message<T>(std::move(handler));
      }
  };

  using compressed_socket_rpc_server = socket_rpc_server<compressed_codec>;
  using flat_socket_rpc_server = socket_rpc_server<flat_materialized_codec>;

} // namespace serialization
