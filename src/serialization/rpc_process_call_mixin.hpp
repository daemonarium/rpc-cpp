#pragma once

#include "promise.hpp"
#include "rpc.hpp"
#include "type_concepts.hpp"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>

namespace serialization {

  // CRTP mixin shared by rpc_server<Rd,Wr> and socket_rpc_server<Codec>.
  // Factors out the 4 process_call variants (value / promise<T> / void /
  // promise<void>) that were copy-pasted between the two. The body is
  // identical across both except for how send_response/send_error obtain a
  // writer: rpc_server threads a Wr& param, socket_rpc_server uses its
  // channel. The mixin sidesteps that by calling derived().send_response(...)
  // / derived().send_error(...) with NO writer param — each derived class
  // implements those to obtain the writer its own way (rpc_server via a
  // stored current_writer_, socket_rpc_server via channel_.begin_send).
  //
  // All overloads are templated on <Rt, Wr> and dispatched via constraints so
  // that the call site `process_call<Rt>(fn, writer, id)` (explicitly
  // specifying only Rt, deducing Wr) resolves to exactly one overload.
  //
  // The Wr& writer parameter is kept for compatibility with
  // rpc_server_registry::functions_ (function<shared_ptr<promise<void>>(Rd&, Wr&, uint64_t)>)
  // but is intentionally unused.
  template <typename Derived>
  class rpc_process_call_mixin {
    protected:
      Derived& derived() noexcept { return static_cast<Derived&>(*this); }

    public:
      // process_call: non-void, non-promise return type (value path)
      template <typename Rt, typename Wr>
      requires(!PromisePointer<Rt> && !Void<Rt>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, Wr& /*writer*/, uint64_t request_id
      ) {
        try {
          Rt result = function();
          derived().send_response(request_id, result);
        } catch(const std::exception& e) {
          derived().send_error(request_id, e);
        } catch(...) {
          derived().send_error(request_id, unknown_exception("Unknown exception"));
        }
        return serialization::promise<void>::resolved();
      }

      // process_call: promise<T> return type (async value)
      template <typename Rt, typename Wr>
      requires(PromisePointer<Rt> && !Void<typename Rt::element_type::promised_type>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, Wr& /*writer*/, uint64_t request_id
      ) {
        auto p = std::make_shared<serialization::promise<void>>();
        try {
          Rt result = function();
          result->handle([this, request_id, p](typename Rt::element_type::promised_type& result) {
            derived().send_response(request_id, result);
            p->resolve();
          }, [this, request_id, p](std::exception_ptr exception) {
            try {
              std::rethrow_exception(exception);
            } catch(const std::exception& e) {
              derived().send_error(request_id, e);
            } catch(...) {
              derived().send_error(request_id, unknown_exception("Unknown exception"));
            }
            p->resolve();
          });
        } catch(const std::exception& e) {
          derived().send_error(request_id, e);
          p->resolve();
        } catch(...) {
          derived().send_error(request_id, unknown_exception("Unknown exception"));
          p->resolve();
        }
        return p;
      }

      // process_call: void return type (sync void)
      template <typename Rt, typename Wr>
      requires(Void<Rt>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, Wr& /*writer*/, uint64_t request_id
      ) {
        try {
          function();
          derived().send_response(request_id);
        } catch(const std::exception& e) {
          derived().send_error(request_id, e);
        } catch(...) {
          derived().send_error(request_id, unknown_exception("Unknown exception"));
        }
        return serialization::promise<void>::resolved();
      }

      // process_call: promise<void> return type (async void)
      template <typename Rt, typename Wr>
      requires(PromisePointer<Rt> && Void<typename Rt::element_type::promised_type>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, Wr& /*writer*/, uint64_t request_id
      ) {
        auto p = std::make_shared<serialization::promise<void>>();
        try {
          Rt result = function();
          result->handle([this, request_id, p]() {
            try {
              derived().send_response(request_id);
            } catch(const std::exception& e) {
              derived().send_error(request_id, e);
            } catch(...) {
              derived().send_error(request_id, std::runtime_error("uncaught exception sending RPC response"));
            }
            p->resolve();
          }, [this, request_id, p](std::exception_ptr exception) {
            try {
              std::rethrow_exception(exception);
            } catch(const std::exception& e) {
              derived().send_error(request_id, e);
            } catch(...) {
              derived().send_error(request_id, unknown_exception("Unknown exception"));
            }
            p->resolve();
          });
        } catch(const std::exception& e) {
          derived().send_error(request_id, e);
          p->resolve();
        } catch(...) {
          derived().send_error(request_id, unknown_exception("Unknown exception"));
          p->resolve();
        }
        return p;
      }
  };

} // namespace serialization
