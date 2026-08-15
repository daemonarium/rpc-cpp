#pragma once

// socket_message_channel is now an alias for the templated framing + compressed codec.
// Existing API (register_message_type, on_message, send_message, begin_send, commit_send,
// handle_data, reader(), writer()) is preserved via delegation in socket_message_framing
// and compressed_codec.

#include "socket_message_framing.hpp"
#include "socket_transport.hpp"
#include "compressed_codec.hpp"
#include "flat_materialized_codec.hpp"
#include "flat_zero_copy_codec.hpp"

namespace serialization {
  using socket_message_channel = socket_message_framing<compressed_codec>;
  using flat_socket_message_channel = socket_message_framing<flat_materialized_codec>;
  using flat_zero_copy_socket_message_channel = socket_message_framing<flat_zero_copy_codec>;
  using flat_native_socket_message_channel = socket_message_framing<flat_materialized_codec, native_transport>;
  using flat_buffer_socket_message_channel = socket_message_framing<flat_materialized_codec, buffer_transport>;
}
