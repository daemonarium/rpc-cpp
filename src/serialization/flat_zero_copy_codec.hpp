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
  // Send path is identical to flat_materialized_codec (write_u32 index +
  // serialize + write_rest). Receive path differs: read_u32 index, then build
  // flat_view<T>(session, body_offset, msg_len) and call handler.
  //
  // v1 limitations (documented in refactoring.md):
  //  - No payload after the flat frame (handler gets only flat_view).
  //  - shared_ptr graph fields appear as nullptr (full graph zero-copy
  //    needs flat_root_shared_on_session with root delta at offset 4 — future).
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
          // body_offset = peek offset right after the 4-byte type index
          size_t body_offset = reader_->cursor();
          auto session = std::make_shared<flat_session>(input_buffer_, current_msg_len_);
          flat_view<T> view(session, body_offset, current_msg_len_);

          auto it = message_handlers_.find(std::type_index(typeid(T)));
          if(it != message_handlers_.end()) {
            it->second(&view);
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

      // Handler signature for zero-copy dispatch: void(flat_view<T>&).
      template <typename T>
      void on_message(std::function<void(flat_view<T>&)> handler) {
        message_handlers_[std::type_index(typeid(T))] = [h = std::move(handler)](void* ptr) {
          h(*reinterpret_cast<flat_view<T>*>(ptr));
        };
      }

      // Write type index + serialized header. Identical to flat_materialized_codec.
      template <typename T>
      void write_message_header(const T& message) {
        auto type = std::type_index(typeid(T));
        auto it = message_type_indexes_.find(type);
        if(it == message_type_indexes_.end()) {
          throw std::runtime_error("Message type not registered for sending: " + std::string(type.name()));
        }
        size_t index = it->second;
        writer_->write_u32(static_cast<std::uint32_t>(index));
        serialize(*writer_, message);
        writer_->write_rest();
      }

      // Reset reader for a new message frame. Called by framing before dispatch.
      void reset_for_dispatch() {
        reader_->reset();
      }

      // Read type index and dispatch to the registered handler.
      // Throws std::runtime_error on unknown index. Framing is responsible for
      // skipping msg_len bytes after dispatch (including on throw).
      void dispatch(size_t msg_len) {
        current_msg_len_ = msg_len;

        std::uint32_t index;
        reader_->read_u32(index);

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
      std::unordered_map<std::type_index, std::function<void(void*)>> message_handlers_;

      size_t current_msg_len_ = 0;
  };

} // namespace serialization
