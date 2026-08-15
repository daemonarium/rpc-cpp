#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <type_traits>

#include "socket_transport.hpp"

extern "C" {
#include <io/circular_buffer.h>
}

namespace serialization {

  // Format-agnostic socket message framing. Owns socket I/O (length-prefix,
  // flush, drain) and delegates format-specific body (type index, serialize,
  // dispatch) to Codec.
  //
  // Transport policy (default socket_c_transport) provides buffer access and
  // flush/drain via static polymorphism — no lambdas / std::function on the
  // hot path.
  //
  // Codec must provide:
  //   - Codec(input_buffer*, output_buffer*) ctor
  //   - register_message_type<T>(), register_exception_type<T>(), register_dynamic_type<T>()
  //   - on_message<T>(handler) (two overloads)
  //   - write_message_header<T>(const T&) — write type index + serialize + write_rest
  //   - reset_for_dispatch() — reset reader offset to 0
  //   - dispatch(size_t msg_len) — read type index, call handler
  //   - reader(), writer() — accessors
  template <typename Codec, typename Transport = socket_c_transport>
  class socket_message_framing {
    public:
      explicit socket_message_framing(Transport transport)
        : transport_(std::move(transport)) {
        codec_ = std::make_unique<Codec>(&transport_.input_buffer(), &transport_.output_buffer());
      }

      // Backward-compatible ctor for socket_c_transport (socket_id).
      explicit socket_message_framing(uint32_t socket_id)
        requires std::is_same_v<Transport, socket_c_transport>
        : socket_message_framing(socket_c_transport(socket_id)) {}

      ~socket_message_framing() {}

      template <typename T>
      void register_message_type() { codec_->template register_message_type<T>(); }

      // Control-message path: always materialized regardless of codec.
      // Forwards to the codec's register_control_message_type / on_control_message.
      template <typename T>
      void register_control_message_type() { codec_->template register_control_message_type<T>(); }

      template <typename T, typename Handler>
      void on_control_message(Handler handler) { codec_->template on_control_message<T>(std::move(handler)); }

      template <typename T>
      void register_exception_type() { codec_->template register_exception_type<T>(); }

      template <typename T>
      void register_dynamic_type() { codec_->template register_dynamic_type<T>(); }

      // Generic on_message: forwards the handler to the codec, which validates
      // the signature (compressed: void(T&)/void(T&,size_t); zero_copy: void(flat_view<T>&)).
      template <typename T, typename Handler>
      void on_message(Handler handler) { codec_->template on_message<T>(std::move(handler)); }

      template <typename WireT, typename SendT>
      void register_message_send_alias() {
        codec_->template register_message_send_alias<WireT, SendT>();
      }

      // Refuse to start a new message when writable bytes drop below this.
      // 0 = disabled. Once a message starts, the writer may still grow the
      // buffer so the whole message fits.
      void set_min_writable_bytes(std::size_t n) { min_writable_bytes_ = n; }
      std::size_t min_writable_bytes() const { return min_writable_bytes_; }

      bool can_queue() {
        if(min_writable_bytes_ == 0) {
          return true;
        }
        return circular_buffer_writable_bytes(&transport_.output_buffer()) >= min_writable_bytes_;
      }

      // Send a complete message (no payload) and flush immediately.
      template <typename T>
      void send_message(const T& message) {
        begin_send(message);
        commit_send();
      }

      // Queue a complete message without flushing. Caller must flush().
      template <typename T>
      void queue_message(const T& message) {
        begin_send(message);
        commit_send_no_flush();
      }

      template <typename T>
      bool try_queue_message(const T& message) {
        if(!can_queue()) {
          return false;
        }
        queue_message(message);
        return true;
      }

      template <typename T>
      bool try_send_message(const T& message) {
        if(!can_queue()) {
          return false;
        }
        send_message(message);
        return true;
      }

      void flush() { transport_.flush(); }

      // Begin sending a message with optional payload.
      // After this call, write payload data to writer(), then call commit_send().
      template <typename T>
      auto& begin_send(const T& message) {
        // Reserve space for length prefix
        uint32_t placeholder = 0;
        circular_buffer_poke(&transport_.output_buffer(), 0, &placeholder, sizeof(placeholder));
        codec_->writer().write_offset_ = sizeof(placeholder);

        // Write message type index + serialized header (format-specific)
        codec_->template write_message_header<T>(message);

        return codec_->writer();
      }

      // Commit a message started with begin_send. Writes length prefix; no flush.
      void commit_send_no_flush() {
        codec_->finalize_send();
        uint32_t payload_len = codec_->writer().write_offset_ - sizeof(uint32_t);
        circular_buffer_poke(&transport_.output_buffer(), 0, &payload_len, sizeof(payload_len));
        circular_buffer_commit_write(&transport_.output_buffer(), codec_->writer().write_offset_);
      }

      // Commit a message started with begin_send. Writes length prefix and flushes.
      void commit_send() {
        commit_send_no_flush();
        transport_.flush();
      }

      void handle_data() {
        while(true) {
          // Read one length-prefixed message from socket input
          uint32_t msg_len = 0;
          if(circular_buffer_readable_bytes(&transport_.input_buffer()) < sizeof(msg_len)) {
            transport_.drain();
            return;
          }
          circular_buffer_peek(&transport_.input_buffer(), 0, &msg_len, sizeof(msg_len));
          size_t available = circular_buffer_readable_bytes(&transport_.input_buffer());
          if(available < sizeof(msg_len) + msg_len) {
            circular_buffer_reserve_for_reading(&transport_.input_buffer(), msg_len + sizeof(msg_len));
            transport_.drain();
            return;
          }

          // Consume length prefix
          circular_buffer_skip_read(&transport_.input_buffer(), sizeof(msg_len));
          codec_->reset_for_dispatch();

          // Dispatch to the correct handler. On unknown index, codec throws —
          // skip the message bytes before rethrowing so the stream stays aligned.
          try {
            codec_->dispatch(msg_len);
          } catch(...) {
            circular_buffer_skip_read(&transport_.input_buffer(), msg_len);
            throw;
          }

          // Skip entire message (including any unread payload)
          circular_buffer_skip_read(&transport_.input_buffer(), msg_len);

          if(!circular_buffer_readable_bytes(&transport_.input_buffer())) {
            transport_.drain();
            return;
          }
        }
      }

      auto& reader() { return codec_->reader(); }
      auto& writer() { return codec_->writer(); }
      Transport& transport() { return transport_; }

    private:
      Transport transport_;
      std::unique_ptr<Codec> codec_;
      std::size_t min_writable_bytes_ = 0;
  };

} // namespace serialization
