#pragma once

#include "flat_buffer_reader.hpp"
#include "flat_circular_buffer_writer.hpp"
#include "flat_source.hpp"
#include "message_codec_base.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <typeindex>

extern "C" {
#include <io/circular_buffer.h>
}

namespace serialization {

  // Flat-format CRTP base shared by flat_materialized_codec and
  // flat_zero_copy_codec. Both use the same wire format (per-frame size
  // prefix + 0 sentinel) and the same send path (write_message_header) and
  // type-index read path (read_type_index). This base factors out those
  // byte-identical pieces plus the poke_u32 helper and payload_start_.
  //
  // It does NOT provide on_message: the two flat codecs have different
  // handler signatures (void(T&) for materialized, void(flat_view<T>&) for
  // zero-copy). Inheriting a void(T&) overload into flat_zero_copy would be
  // dangerous — its dispatch passes a flat_view<T>* as void*, which a
  // void(T&) handler would reinterpret_cast to T* (UB). So on_message stays
  // in each derived codec.
  template <typename Derived>
  class flat_message_codec_base
    : public message_codec_base<Derived,
        flat_buffer_reader<circular_flat_source>, flat_circular_buffer_writer> {
    public:
      // Read type index (u32 LE) and consume the root frame's per-frame size
      // prefix (encoded_size_0). The materialized reader reads fields
      // sequentially via enter_frame/exit_frame, so the size itself is not
      // needed — only consumed to keep the cursor aligned with the root
      // frame body. flat_zero_copy re-reads it from body_offset via the
      // session, so advancing the reader past it here is for cursor
      // bookkeeping.
      std::uint32_t read_type_index() {
        std::uint32_t index;
        this->derived().reader().read_u32(index);
        std::uint32_t root_encoded_size;
        this->derived().reader().read_u32(root_encoded_size);
        (void)root_encoded_size;
        return index;
      }

      // Write type index + per-frame size prefix + serialized root + shared
      // sub-frames (each with its own per-frame prefix via write_rest) + 0
      // sentinel. Called by framing.begin_send<T>() after the length-prefix
      // placeholder is reserved and writer offset set.
      template <typename T>
      void write_message_header(const T& message) {
        auto type = std::type_index(typeid(T));
        auto it = this->message_type_indexes_.find(type);
        if(it == this->message_type_indexes_.end()) {
          throw std::runtime_error("Message type not registered for sending: " + std::string(type.name()));
        }
        size_t index = it->second;
        this->derived().writer().write_u32(static_cast<std::uint32_t>(index));

        // encoded_size_0 placeholder for the root frame (filled after serialize)
        std::size_t root_size_offset = this->derived().writer().write_offset_;
        this->derived().writer().write_u32(0);
        std::size_t root_frame_start = this->derived().writer().write_offset_;
        serialize(this->derived().writer(), message);
        std::uint32_t root_real_size =
          static_cast<std::uint32_t>(this->derived().writer().write_offset_ - root_frame_start);
        poke_u32(root_size_offset, root_real_size + 1);

      // Sub-frames: each wrapped with its own per-frame size prefix by
      // write_rest. Then a 0 sentinel terminates the frame list.
      this->derived().writer().write_rest();
      this->derived().writer().write_u32(0); // sentinel
      payload_start_ = this->derived().writer().write_offset_;
      }

      // Materialized dispatch handler shared by flat_materialized_codec (for
      // all messages) and flat_zero_copy_codec (for control messages only).
      // Deserializes T from the flat reader, consumes the 0 sentinel that
      // terminates the frame list, then invokes invoke_handler<T> with the
      // materialized T and the derived payload_size. This is the flat
      // counterpart of compressed_codec::make_dispatch_handler.
      template <typename T>
      std::function<void()> make_materialized_dispatch_handler() {
        return [this]() {
          T msg;
          deserialize(this->derived().reader(), msg);
          this->derived().reader().read_rest();
          // Consume the 0 sentinel that terminates the frame list.
          std::uint32_t sentinel = 1;
          this->derived().reader().read_u32(sentinel);
          if(sentinel != 0) {
            throw std::runtime_error("flat codec: expected frame sentinel, got " + std::to_string(sentinel));
          }
          size_t payload_size = this->current_msg_len_ - this->derived().reader().cursor();
          this->template invoke_handler<T>(&msg, payload_size);
        };
      }

    protected:
      // Write offset at which payload (if any) begins. Set by
      // write_message_header (after the sentinel). Kept for introspection;
      // payload_size is derived by dispatch as (msg_len - cursor).
      std::size_t payload_start_ = 0;

      // Poke a u32 LE value at a previously reserved writer position. Used
      // to back-patch the root frame's per-frame size prefix after the root
      // body is complete.
      void poke_u32(std::size_t abs_offset, std::uint32_t value) {
        circular_buffer_poke(this->derived().writer().buffer_, abs_offset, &value, sizeof(value));
      }
  };

} // namespace serialization
