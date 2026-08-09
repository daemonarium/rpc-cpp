#pragma once

#include "serialize.hpp"
#include "deserialize.hpp"

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace serialization {

  // CRTP base for socket message codecs. Owns the format-agnostic parts
  // shared by compressed_codec, flat_materialized_codec and flat_zero_copy_codec:
  // the dispatch table, message/exception/dynamic-type registrations (which
  // delegate to the derived reader/writer), the dispatch skeleton (read type
  // index, bounds-check, invoke handler) and the no-op finalize_send.
  //
  // Derived classes provide format-specific hooks:
  //   - reader() / writer()              — concrete reader/writer accessors
  //   - read_type_index()                  — read the message type index (uvarint64 vs u32)
  //   - write_message_header<T>(msg)      — write type index + serialized body
  //   - make_dispatch_handler<T>()        — build the dispatch lambda (materialize
  //                                       or build flat_view) that invokes invoke_handler<T>
  //   - reset_for_dispatch()              — reset reader offset for a new message
  //   - on_message<T>(handler)           — register a handler (signature depends on
  //                                       materialized vs zero-copy)
  //
  // Compatible with socket_message_framing<Codec>: the public surface
  // (register_message_type, register_exception_type, register_dynamic_type,
  // dispatch, finalize_send) is provided by this base.
  template <typename Derived, typename Reader, typename Writer>
  class message_codec_base {
    protected:
      std::vector<std::function<void()>> dispatch_table_;
      std::unordered_map<std::type_index, size_t> message_type_indexes_;
      std::unordered_map<std::type_index, std::function<void(void*, size_t)>> message_handlers_;
      size_t current_msg_len_ = 0;

      Derived& derived() noexcept { return static_cast<Derived&>(*this); }

      // Look up the handler registered for T and invoke it with the deserialized
      // message pointer and payload size. Called by the derived
      // make_dispatch_handler lambda after it has materialized T (or built a
      // flat_view<T>) and positioned the reader over the payload.
      template <typename T>
      void invoke_handler(void* ptr, size_t payload_size) {
        auto it = message_handlers_.find(std::type_index(typeid(T)));
        if(it != message_handlers_.end()) {
          it->second(ptr, payload_size);
        }
        // Framing does circular_buffer_skip_read(msg_len) after dispatch returns,
        // which automatically skips any unread payload.
      }

    public:
      // Register exception serialization/deserialization on the derived
      // reader/writer. Identical across all codecs.
      template <typename T>
      void register_exception_type() {
        derived().writer().template register_serialize_exception_function<T>();
        derived().reader().template register_deserialize_exception_function<T>();
      }

      // Register a dynamic (polymorphic shared_ptr) type on the derived
      // reader/writer. Identical across all codecs.
      template <typename T>
      void register_dynamic_type() {
        derived().writer().template register_serialize_typed_function<T>();
        derived().reader().template register_deserialize_typed_function<T>();
      }

      // Register a message type: serialize fn on the writer + a dispatch
      // entry built by the derived make_dispatch_handler<T>(). The dispatch
      // lambda is format-specific (materialize T vs build flat_view<T>).
      template <typename T>
      void register_message_type() {
        derived().writer().template register_serialize_message_function<T>();

        size_t index = dispatch_table_.size();
        message_type_indexes_[std::type_index(typeid(T))] = index;

        dispatch_table_.push_back(derived().template make_dispatch_handler<T>());
      }

      // Dispatch one message: read the type index (format-specific), bounds-
      // check, invoke the registered dispatch handler. Framing is responsible
      // for skipping msg_len bytes after dispatch (including on throw).
      void dispatch(size_t msg_len) {
        current_msg_len_ = msg_len;

        auto index = derived().read_type_index();
        if(index >= dispatch_table_.size()) {
          throw std::runtime_error("Unknown message type index: " + std::to_string(index));
        }

        dispatch_table_[static_cast<std::size_t>(index)]();
      }

      // No-op for all codecs: payload_size is derived on read
      // (msg_len - cursor after the frame list). Kept for
      // socket_message_framing<Codec> compatibility.
      void finalize_send() {}
  };

} // namespace serialization
