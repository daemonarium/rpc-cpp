#pragma once

#include <cstdint>
#include <functional>
#include <memory>

extern "C" {
#include <socket/socket.h>
#include <io/circular_buffer.h>
}

#ifdef __EMSCRIPTEN__

extern "C" {
  __attribute__((import_module("waos"), import_name("waos_socket_flush")))
  void waos_socket_flush(std::uint32_t socket);

  __attribute__((import_module("waos"), import_name("waos_socket_input_drain")))
  void waos_socket_input_drain(std::uint32_t socket);
}

#else

extern "C" {
  void waos_socket_flush(std::uint32_t socket);
  void waos_socket_input_drain(std::uint32_t socket);
}

#endif

namespace serialization {

  // Format-agnostic socket message framing. Owns socket I/O (length-prefix,
  // flush, drain) and delegates format-specific body (type index, serialize,
  // dispatch) to Codec.
  //
  // Codec must provide:
  //   - Codec(input_buffer*, output_buffer*) ctor
  //   - register_message_type<T>(), register_exception_type<T>(), register_dynamic_type<T>()
  //   - on_message<T>(handler) (two overloads)
  //   - write_message_header<T>(const T&) — write type index + serialize + write_rest
  //   - reset_for_dispatch() — reset reader offset to 0
  //   - dispatch(size_t msg_len) — read type index, call handler
  //   - reader(), writer() — accessors
  template <typename Codec>
  class socket_message_framing {
    public:
      explicit socket_message_framing(uint32_t socket_id)
        : socket_id_(socket_id) {
        socket_ = get_socket_info(socket_id_);
        codec_ = std::make_unique<Codec>(&socket_->input_buffer, &socket_->output_buffer);
      }
      ~socket_message_framing() {}

      template <typename T>
      void register_message_type() { codec_->template register_message_type<T>(); }

      template <typename T>
      void register_exception_type() { codec_->template register_exception_type<T>(); }

      template <typename T>
      void register_dynamic_type() { codec_->template register_dynamic_type<T>(); }

      // Generic on_message: forwards the handler to the codec, which validates
      // the signature (compressed: void(T&)/void(T&,size_t); zero_copy: void(flat_view<T>&)).
      template <typename T, typename Handler>
      void on_message(Handler handler) { codec_->template on_message<T>(std::move(handler)); }

      // Send a complete message (no payload)
      template <typename T>
      void send_message(const T& message) {
        begin_send(message);
        commit_send();
      }

      // Begin sending a message with optional payload.
      // After this call, write payload data to writer(), then call commit_send().
      template <typename T>
      auto& begin_send(const T& message) {
        // Reserve space for length prefix
        uint32_t placeholder = 0;
        circular_buffer_poke(&socket_->output_buffer, 0, &placeholder, sizeof(placeholder));
        codec_->writer().write_offset_ = sizeof(placeholder);

        // Write message type index + serialized header (format-specific)
        codec_->template write_message_header<T>(message);

        return codec_->writer();
      }

      // Commit a message started with begin_send. Writes length prefix and flushes.
      void commit_send() {
        codec_->finalize_send();
        uint32_t payload_len = codec_->writer().write_offset_ - sizeof(uint32_t);
        circular_buffer_poke(&socket_->output_buffer, 0, &payload_len, sizeof(payload_len));
        circular_buffer_commit_write(&socket_->output_buffer, codec_->writer().write_offset_);
        waos_socket_flush(socket_id_);
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
          codec_->reset_for_dispatch();

          // Dispatch to the correct handler. On unknown index, codec throws —
          // skip the message bytes before rethrowing so the stream stays aligned.
          try {
            codec_->dispatch(msg_len);
          } catch(...) {
            circular_buffer_skip_read(&socket_->input_buffer, msg_len);
            throw;
          }

          // Skip entire message (including any unread payload)
          circular_buffer_skip_read(&socket_->input_buffer, msg_len);

          if(!circular_buffer_readable_bytes(&socket_->input_buffer)) {
            waos_socket_input_drain(socket_id_);
            return;
          }
        }
      }

      auto& reader() { return codec_->reader(); }
      auto& writer() { return codec_->writer(); }

    private:
      uint32_t socket_id_;
      struct socket_info* socket_;
      std::unique_ptr<Codec> codec_;
  };

} // namespace serialization
