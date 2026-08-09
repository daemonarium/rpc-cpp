#pragma once

#include "flat_view.hpp"
#include "flat_buffer_reader.hpp"
#include "flat_circular_buffer_writer.hpp"
#include "flat_source.hpp"
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
  // dispatch builds a flat_view<T> over the socket input buffer and hands it
  // to the handler. Compatible with socket_message_framing<Codec>.
  //
  // Flat wire format (body, after length prefix) — shared with
  // flat_materialized_codec so the two are interoperable:
  //   [type index 4B u32 LE][root_frame_size 4B u32 LE][payload_size 4B u32 LE]
  //   [root T frame][shared sub-frames][payload]
  // root_frame_size lets dispatch construct flat_view<T> with the exact frame
  // end (flat_view reads its CPS trailer from `end`). payload_size lets the
  // handler locate the payload (it sits at the tail: [msg_len - payload_size,
  // msg_len)) regardless of the shared sub-frames length.
  //
  // Send path is identical to flat_materialized_codec. Receive path differs:
  // reads the 12-byte header, builds flat_view<T>(session, body_offset,
  // body_offset + root_frame_size), skips the reader to the payload, and
  // invokes the handler with (flat_view<T>&, payload_size).
  //
  // v1 limitations (documented in refactoring.md):
  //  - shared_ptr graph fields appear as nullptr (full graph zero-copy needs
  //    flat_root_shared_on_session with a body_offset variant — future krok 5c).
  //  - flat_view is valid only during dispatch (before framing.skip_read).
  class flat_zero_copy_codec {
    public:
      flat_zero_copy_codec(struct circular_buffer* input_buffer, struct circular_buffer* output_buffer)
        : input_buffer_(input_buffer),
          reader_(std::make_unique<flat_buffer_reader<circular_flat_source>>(circular_flat_source(input_buffer))),
          writer_(std::make_unique<flat_circular_buffer_writer>(output_buffer, 0)) {}

      template <typename T>
      void register_message_type() {
        writer_->register_serialize_message_function<T>();

        size_t index = dispatch_table_.size();
        message_type_indexes_[std::type_index(typeid(T))] = index;

        dispatch_table_.push_back([this]() {
          // After dispatch read the 3 u32s: index, root_frame_size, payload_size.
          // cursor is now at body_offset (= 12).
          size_t body_offset = reader_->cursor();

          auto session = std::make_shared<flat_session>(input_buffer_, current_msg_len_);
          // flat_view must span exactly the root T frame: load_checkpoints reads
          // the CPS trailer from `end`, so end = body_offset + root_frame_size.
          flat_view<T> view(session, body_offset, body_offset + current_root_frame_size_);

          // Position the reader over the payload (tail of the message).
          // payload starts at (msg_len - payload_size); skip from body_offset
          // past the root frame + any shared sub-frames.
          std::size_t payload_offset = current_msg_len_ - current_payload_size_;
          if(payload_offset >= body_offset) {
            reader_->skip(payload_offset - body_offset);
          }

          auto it = message_handlers_.find(std::type_index(typeid(T)));
          if(it != message_handlers_.end()) {
            it->second(&view, current_payload_size_);
          }
          // Framing does circular_buffer_skip_read(msg_len) after dispatch
          // returns — flat_view/session must not outlive the handler.
        });
      }

      template <typename T>
      void register_exception_type() {
        writer_->register_serialize_exception_function<T>();
        reader_->register_deserialize_exception_function<T>();
      }

      template <typename T>
      void register_dynamic_type() {
        writer_->register_serialize_typed_function<T>();
        reader_->register_deserialize_typed_function<T>();
      }

      // Handler without payload: payload (if any) is ignored, framing skips it.
      template <typename T>
      void on_message(std::function<void(flat_view<T>&)> handler) {
        message_handlers_[std::type_index(typeid(T))] =
          [h = std::move(handler)](void* ptr, size_t) {
            h(*reinterpret_cast<flat_view<T>*>(ptr));
          };
      }

      // Handler with payload: reads raw payload bytes via codec.reader()
      // (positioned at the payload by dispatch).
      template <typename T>
      void on_message(std::function<void(flat_view<T>&, size_t payload_size)> handler) {
        message_handlers_[std::type_index(typeid(T))] =
          [h = std::move(handler)](void* ptr, size_t payload_size) {
            h(*reinterpret_cast<flat_view<T>*>(ptr), payload_size);
          };
      }

      // Write type index + serialized header. Identical to flat_materialized_codec:
      // 12-byte header (index + root_frame_size placeholder + payload_size
      // placeholder) + serialize + write_rest. root_frame_size is stamped here,
      // payload_size is stamped by finalize_send (called from framing.commit_send).
      template <typename T>
      void write_message_header(const T& message) {
        auto type = std::type_index(typeid(T));
        auto it = message_type_indexes_.find(type);
        if(it == message_type_indexes_.end()) {
          throw std::runtime_error("Message type not registered for sending: " + std::string(type.name()));
        }
        size_t index = it->second;
        writer_->write_u32(static_cast<std::uint32_t>(index));

        root_frame_size_field_offset_ = writer_->write_offset_;
        writer_->write_u32(0);
        payload_size_field_offset_ = writer_->write_offset_;
        writer_->write_u32(0);

        std::size_t frame_start = writer_->write_offset_;
        serialize(*writer_, message);
        std::size_t root_frame_size = writer_->write_offset_ - frame_start;
        poke_u32(root_frame_size_field_offset_, static_cast<std::uint32_t>(root_frame_size));

        writer_->write_rest();
        payload_start_ = writer_->write_offset_;
      }

      // Stamp payload_size into the header once payload (if any) is written.
      // Called by framing.commit_send before the length prefix is committed.
      void finalize_send() {
        std::uint32_t payload_size =
          static_cast<std::uint32_t>(writer_->write_offset_ - payload_start_);
        poke_u32(payload_size_field_offset_, payload_size);
      }

      // Reset reader for a new message frame. Called by framing before dispatch.
      void reset_for_dispatch() {
        reader_->reset();
      }

      // Read the 12-byte header (index + root_frame_size + payload_size) and
      // dispatch to the registered handler. Throws std::runtime_error on unknown
      // index. Framing is responsible for skipping msg_len bytes after dispatch
      // (including on throw).
      void dispatch(size_t msg_len) {
        current_msg_len_ = msg_len;

        std::uint32_t index;
        reader_->read_u32(index);
        reader_->read_u32(current_root_frame_size_);
        reader_->read_u32(current_payload_size_);

        if(index >= dispatch_table_.size()) {
          throw std::runtime_error("Unknown message type index: " + std::to_string(index));
        }

        dispatch_table_[static_cast<std::size_t>(index)]();
      }

      flat_buffer_reader<circular_flat_source>& reader() { return *reader_; }
      flat_circular_buffer_writer& writer() { return *writer_; }

    private:
      struct circular_buffer* input_buffer_;
      std::unique_ptr<flat_buffer_reader<circular_flat_source>> reader_;
      std::unique_ptr<flat_circular_buffer_writer> writer_;

      std::vector<std::function<void()>> dispatch_table_;
      std::unordered_map<std::type_index, size_t> message_type_indexes_;
      std::unordered_map<std::type_index, std::function<void(void*, size_t)>> message_handlers_;

      size_t current_msg_len_ = 0;
      std::uint32_t current_root_frame_size_ = 0;
      std::uint32_t current_payload_size_ = 0;

      std::size_t root_frame_size_field_offset_ = 0;
      std::size_t payload_size_field_offset_ = 0;
      std::size_t payload_start_ = 0;

      void poke_u32(std::size_t abs_offset, std::uint32_t value) {
        circular_buffer_poke(writer_->buffer_, abs_offset, &value, sizeof(value));
      }
  };

} // namespace serialization
