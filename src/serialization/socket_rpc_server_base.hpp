#pragma once

#include "socket_message_channel.hpp"
#include "socket_message_framing.hpp"
#include "socket_transport.hpp"
#include "rpc_server_registry.hpp"
#include "rpc_process_call_mixin.hpp"
#include "serialize.hpp"
#include "promise.hpp"
#include "rpc.hpp"
#include "examples/structures/stream_protocol.hpp"
#include "observable/observable.hpp"
#include "observable/stream_publisher.hpp"
#include <memory>
#include <functional>
#include <exception>
#include <type_traits>

namespace serialization {

  // Format-agnostic CRTP base for socket RPC servers. Owns the control /
  // registry / send machinery that is identical across all codecs:
  //   - the socket_message_framing<Codec, Transport> channel,
  //   - registration of control messages (rpc_request/response/error) via
  //     register_control_message_type (always materialized, regardless of
  //     the codec's user-message handler_signature),
  //   - the handle_rpc_request / send_response / send_error implementation,
  //   - the rpc_server_registry and rpc_process_call_mixin bases.
  //
  // The thin derived socket_rpc_server<Codec, Transport> only adds the public
  // on_message<T> overloads, whose signature is adapted to the codec via
  // Codec::handler_signature<T> (T& for compressed/flat_materialized,
  // flat_view<T>& for flat_zero_copy). Control messages always go through
  // the materialized on_control_message path provided here.
  template <typename Codec, typename Transport = socket_c_transport>
  class socket_rpc_server_base
    : public rpc_server_registry<typename Codec::reader_type, typename Codec::writer_type, socket_rpc_server_base<Codec, Transport>>,
      public rpc_process_call_mixin<socket_rpc_server_base<Codec, Transport>> {
    public:
      using reader_type = typename Codec::reader_type;
      using writer_type = typename Codec::writer_type;
      using transport_type = Transport;
      using channel_type = socket_message_framing<Codec, Transport>;
      using stream_publisher_type = waos::observable::StreamPublisher<channel_type>;

      explicit socket_rpc_server_base(Transport transport)
        : channel_(std::move(transport)) {
        // rpc_request is a control message: always materialized, independent
        // of the codec's user-message handler_signature.
        channel_.template on_control_message<rpc_request>(
          [this](rpc_request& req, size_t payload_size) {
            handle_rpc_request(req, payload_size);
          });
      }

      // Backward-compatible ctor for socket_c_transport (socket_id).
      explicit socket_rpc_server_base(uint32_t socket_id)
        requires std::is_same_v<Transport, socket_c_transport>
        : socket_rpc_server_base(socket_c_transport(socket_id)) {}

      ~socket_rpc_server_base() {}

      void register_basic_types() {
        channel_.template register_control_message_type<rpc_request>();
        channel_.template register_control_message_type<rpc_response>();
        channel_.template register_control_message_type<rpc_error>();
        channel_.template register_exception_type<unknown_exception>();
      }

      // Forward registration methods from rpc_server_registry
      template <typename T>
      void register_recursive(T value) {
        rpc_server_registry<reader_type, writer_type, socket_rpc_server_base<Codec, Transport>>::register_recursive(value, *this);
      }

      void register_stream_message_types() {
        channel_.template register_message_type<waos::socket_opened>();
        channel_.template register_message_type<waos::stream_item>();
        channel_.template register_message_type<waos::stream_key>();
        channel_.template register_message_type<waos::stream_done>();
        channel_.template register_message_type<waos::stream_error>();
        channel_.template register_message_type<waos::stream_reset>();
        channel_.template register_message_type<waos::stream_cancel>();
        channel_.template register_message_type<waos::stream_opened>();
      }

      void handle_data() {
        channel_.handle_data();
      }

      // Expose channel for user-defined message types (used by the derived
      // socket_rpc_server<Codec>::on_message, which adapts the handler
      // signature via Codec::handler_signature<T>).
      template <typename T> void register_message_type() { channel_.template register_message_type<T>(); }
      template <typename T> void send_message(const T& msg) { channel_.template send_message<T>(msg); }

      auto& channel() { return channel_; }

      stream_publisher_type& stream_publisher() {
        ensure_stream_publisher();
        return *stream_publisher_;
      }

      void on_stream_writable() {
        if(stream_publisher_) {
          stream_publisher_->onWritable();
        }
      }

      void attach_high_level_observable(
          const waos::stream_handle& stream,
          std::shared_ptr<waos::observable::Observable<waos::observable::BinaryEntry>> obs) {
        if(!obs) {
          throw std::runtime_error("high-level observable returned null");
        }
        if(stream.since_timestamp > 0) {
          obs->bumpMaxTs(stream.since_timestamp);
        }
        auto& publisher = stream_publisher();
        const bool observe =
          (stream.flags & static_cast<std::uint8_t>(waos::stream_flags::observe)) != 0;
        if(observe) {
          publisher.attach(stream.stream_id, std::move(obs), stream.since_timestamp);
        } else {
          publisher.attach_oneshot(stream.stream_id, std::move(obs), stream.since_timestamp);
        }
      }

    protected:
      void handle_rpc_request(rpc_request& req, size_t payload_size) {
        try {
          if(req.function_id >= this->functions_.size()) {
            send_error(req.request_id, std::runtime_error("Function not found"));
            return;
          }
          this->functions_[req.function_id](channel_.reader(), channel_.writer(), req.request_id);
        } catch(const std::exception& e) {
          send_error(req.request_id, e);
        } catch(...) {
          send_error(req.request_id, std::runtime_error("uncaught exception in RPC handler"));
        }
      }

      void send_response(uint64_t request_id) {
        channel_.send_message(rpc_response{request_id});
      }

      template <typename Rt>
      void send_response(uint64_t request_id, Rt& result) {
        auto& writer = channel_.begin_send(rpc_response{request_id});
        serialize(writer, result);
        writer.write_rest();
        channel_.commit_send();
      }

      void send_error(uint64_t request_id, const std::exception& e) {
        auto& writer = channel_.begin_send(rpc_error{request_id});
        try {
          writer.write_exception(e);
        } catch(unknown_exception& ex) {
          auto rt = unknown_exception(std::string("Unknown exception: ") + e.what());
          writer.write_exception(rt);
        }
        writer.write_rest();
        channel_.commit_send();
      }

    private:
      friend class rpc_server_registry<reader_type, writer_type, socket_rpc_server_base<Codec, Transport>>;
      friend class rpc_process_call_mixin<socket_rpc_server_base<Codec, Transport>>;

      void ensure_stream_publisher() {
        if(stream_publisher_) return;
        if constexpr (std::is_same_v<
            typename Codec::template handler_signature<waos::stream_cancel>,
            waos::stream_cancel&>) {
          stream_publisher_ = std::make_unique<stream_publisher_type>(channel_);
        } else {
          throw std::runtime_error(
            "high-level observables require a materialized codec");
        }
      }

      socket_message_framing<Codec, Transport> channel_;
      std::unique_ptr<stream_publisher_type> stream_publisher_;
  };

} // namespace serialization
