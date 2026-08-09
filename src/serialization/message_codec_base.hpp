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
    public:
      using reader_type = Reader;
      using writer_type = Writer;

      // Handler signature trait. Defaults to materialized (T&). A codec that
      // dispatches user messages zero-copy (flat_zero_copy_codec) hides this
      // alias with `template <typename T> using handler_signature = flat_view<T>&;`.
      // socket_rpc_server<Codec>::on_message uses Codec::handler_signature<T> to
      // expose the codec-appropriate user-message handler API, while control
      // messages (rpc_request/response/error) always use the materialized
      // on_control_message path below.
      template <typename T>
      using handler_signature = T&;

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

      // --- Control-message path (always materialized) ---------------------
      // Control messages (rpc_request/rpc_response/rpc_error) must always be
      // materialized regardless of codec, because RPC dispatch reads their
      // fields by value. flat_zero_copy_codec overrides
      // make_control_dispatch_handler<T> to use the shared materialized
      // dispatch builder (flat_message_codec_base::make_materialized_dispatch_handler)
      // instead of its flat_view make_dispatch_handler. For compressed and
      // flat_materialized codecs the default make_control_dispatch_handler
      // delegates to make_dispatch_handler (already materialized), so
      // control registration is equivalent to register_message_type there.

      // Register a control message type: like register_message_type but the
      // dispatch entry is built by make_control_dispatch_handler<T>().
      template <typename T>
      void register_control_message_type() {
        derived().writer().template register_serialize_message_function<T>();

        size_t index = dispatch_table_.size();
        message_type_indexes_[std::type_index(typeid(T))] = index;

        dispatch_table_.push_back(derived().template make_control_dispatch_handler<T>());
      }

      // Register a materialized handler for a control message. Always
      // void(T&, size_t), independent of the codec's user-message
      // handler_signature. Sets message_handlers_ directly (does not
      // delegate to the derived on_message, whose signature may differ).
      template <typename T>
      void on_control_message(std::function<void(T&, size_t)> handler) {
        message_handlers_[std::type_index(typeid(T))] =
          [h = std::move(handler)](void* ptr, size_t payload_size) {
            T& msg = *reinterpret_cast<T*>(ptr);
            h(msg, payload_size);
          };
      }

      // Build the dispatch lambda for a control message. Default: delegate to
      // the codec's make_dispatch_handler<T>() (already materialized for
      // compressed/flat_materialized). flat_zero_copy overrides this to use
      // the materialized dispatch builder instead of its flat_view one.
      template <typename T>
      std::function<void()> make_control_dispatch_handler() {
        return derived().template make_dispatch_handler<T>();
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
