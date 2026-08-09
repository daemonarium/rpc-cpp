#pragma once

#include "flat_view.hpp"
#include "flat_buffer_reader.hpp"
#include "flat_circular_buffer_writer.hpp"
#include "flat_source.hpp"
#include "flat_message_codec_base.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

extern "C" {
#include <io/circular_buffer.h>
}

namespace serialization {

  // Flat-format codec with zero-copy dispatch: instead of materializing a T,
  // dispatch builds a flat_view<T> graph over the socket input buffer (via
  // flat_root_shared_on_session, which walks per-frame size prefixes) and
  // hands it to the handler. Compatible with socket_message_framing<Codec>.
  //
  // Flat wire format (body, after length prefix) — shared with
  // flat_materialized_codec so the two are interoperable:
  //   [type index 4B u32 LE]
  //   [encoded_size_0 = N0+1 4B u32 LE][root T frame]
  //   [encoded_size_1 = N1+1][sub-frame 1] ...
  //   [0 sentinel 4B]
  //   [payload P B]
  // encoded_size_i = real_size_i + 1 (0 = sentinel). dispatch walks the
  // prefixes via flat_root_shared_on_session to build the full shared_ptr
  // graph (root + all sub-frames) as flat_view objects — zero materialization.
  // payload_size is derived as (msg_len - cursor) after the sentinel.
  //
  // Send path (read_type_index / write_message_header / poke_u32 /
  // payload_start_) comes from flat_message_codec_base. Receive path differs:
  // builds the flat_view graph, skips the reader to the payload, and invokes
  // the handler with (flat_view<T>&, payload_size).
  //
  // flat_view is valid only during dispatch (before framing.skip_read).
  class flat_zero_copy_codec
    : public flat_message_codec_base<flat_zero_copy_codec> {
    public:
      // User messages are dispatched zero-copy as flat_view<T>&. Control
      // messages (rpc_request/response/error) bypass this via
      // on_control_message / register_control_message_type, which use the
      // materialized dispatch builder (make_materialized_dispatch_handler)
      // through the make_control_dispatch_handler override below.
      template <typename T>
      using handler_signature = flat_view<T>&;

      flat_zero_copy_codec(struct circular_buffer* input_buffer, struct circular_buffer* output_buffer)
        : input_buffer_(input_buffer),
          reader_(std::make_unique<flat_buffer_reader<circular_flat_source>>(circular_flat_source(input_buffer))),
          writer_(std::make_unique<flat_circular_buffer_writer>(output_buffer, 0)) {}

      template <typename T>
      std::function<void()> make_dispatch_handler() {
        return [this]() {
          // dispatch already read: type index + encoded_size_0 (root prefix).
          // cursor is now at body_offset = start of the root frame body.
          size_t body_offset = reader_->cursor() - sizeof(std::uint32_t);

          auto session = std::make_shared<flat_session>(input_buffer_, this->current_msg_len_);
          // Walk per-frame prefixes to build the full shared_ptr graph (root
          // + all sub-frames) as flat_view objects — zero materialization.
          auto view = flat_detail::flat_root_shared_on_session<T>(
            session, body_offset, this->current_msg_len_);

          // Position the reader over the payload (after the sentinel).
          // flat_root_shared_on_session walked prefixes+frames via session->peek
          // (reader cursor unaffected). The reader already consumed type index
          // + encoded_size_0, so it sits at body_offset + 4. Walk the same frame
          // layout to find the sentinel, then skip the reader to just past it.
          std::size_t reader_pos = reader_->cursor(); // body_offset + 4
          std::size_t cursor = body_offset;
          while(cursor + 4 <= this->current_msg_len_) {
            std::uint8_t sbuf[4];
            session->peek(cursor, sbuf, 4);
            std::uint32_t encoded_size = flat_load_le<std::uint32_t>(sbuf);
            if(encoded_size == 0) break; // sentinel
            cursor += 4 + (encoded_size - 1);
          }
          cursor += 4; // skip the sentinel
          if(cursor > reader_pos) {
            reader_->skip(cursor - reader_pos);
          }

          std::size_t payload_size = this->current_msg_len_ - cursor;
          this->template invoke_handler<T>(view.get(), payload_size);
          // Framing does circular_buffer_skip_read(msg_len) after dispatch
          // returns — flat_view/session must not outlive the handler.
        };
      }

      // Control messages must be materialized even in the zero-copy codec.
      // Override the base default (which would delegate to the flat_view
      // make_dispatch_handler) to use the shared materialized dispatch builder.
      template <typename T>
      std::function<void()> make_control_dispatch_handler() {
        return this->template make_materialized_dispatch_handler<T>();
      }

      // Handler without payload: payload (if any) is ignored, framing skips it.
      template <typename T>
      void on_message(std::function<void(flat_view<T>&)> handler) {
        this->message_handlers_[std::type_index(typeid(T))] =
          [h = std::move(handler)](void* ptr, size_t) {
            h(*reinterpret_cast<flat_view<T>*>(ptr));
          };
      }

      // Handler with payload: reads raw payload bytes via codec.reader()
      // (positioned at the payload by dispatch).
      template <typename T>
      void on_message(std::function<void(flat_view<T>&, size_t payload_size)> handler) {
        this->message_handlers_[std::type_index(typeid(T))] =
          [h = std::move(handler)](void* ptr, size_t payload_size) {
            h(*reinterpret_cast<flat_view<T>*>(ptr), payload_size);
          };
      }

      void reset_for_dispatch() {
        reader_->reset();
      }

      flat_buffer_reader<circular_flat_source>& reader() { return *reader_; }
      flat_circular_buffer_writer& writer() { return *writer_; }

    private:
      struct circular_buffer* input_buffer_;
      std::unique_ptr<flat_buffer_reader<circular_flat_source>> reader_;
      std::unique_ptr<flat_circular_buffer_writer> writer_;
  };

} // namespace serialization
