#pragma once

#include "deserialize.hpp"
#include "promise.hpp"
#include "rpc.hpp"
#include "type_concepts.hpp"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <tuple>
#include <unordered_map>

namespace serialization {

  // CRTP mixin shared by rpc_client<Rd,Wr> and socket_rpc_client<Codec>.
  // Factors out the 4 call/call_method overloads (value+void x call+method)
  // that were copy-pasted between the two. The bodies are identical except
  // for how they obtain a reader (to deserialize the response) and how they
  // send the request. The mixin sidesteps that by:
  //   - response handler lambdas calling derived().reader() to deserialize
  //   - request send via derived().send_request_impl(...) /
  //     derived().send_method_request_impl(...)
  //
  // Owns the response handler table and the request id counter, which are
  // identical across both derived classes.
  //
  // Derived classes provide:
  //   - reader()                          — accessor for the current reader
  //   - send_request_impl(function_id, request_id, args...)  — send a call
  //   - send_method_request_impl(function_id, request_id, id, args...) — send a method call
  template <typename Derived>
  class rpc_call_mixin {
    public:
      using response_handler = std::function<void(bool, std::exception_ptr)>;

      // call: non-void return type
      template <typename Rt, typename... Args>
      requires(!Void<Rt>)
      std::shared_ptr<promise<Rt>> call(std::uint64_t function_id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p, this](bool success, std::exception_ptr eptr) {
          if(success) {
            Rt result;
            deserialize(derived().reader(), result);
            derived().reader().read_rest();
            p->resolve(result);
          } else {
            p->reject(eptr);
          }
        };
        derived().send_request_impl(function_id, request_id, args...);
        return p;
      }

      // call: void return type
      template <typename Rt, typename... Args>
      requires(Void<Rt>)
      std::shared_ptr<promise<void>> call(std::uint64_t function_id, Args... args) {
        auto p = std::make_shared<promise<void>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p](bool success, std::exception_ptr eptr) {
          if(success) {
            p->resolve();
          } else {
            p->reject(eptr);
          }
        };
        derived().send_request_impl(function_id, request_id, args...);
        return p;
      }

      // call_method: non-void return type
      template <typename Id, typename Rt, typename... Args>
      requires(!Void<Rt>)
      std::shared_ptr<promise<Rt>> call_method(std::uint64_t function_id, Id id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p, this](bool success, std::exception_ptr eptr) {
          if(success) {
            Rt result;
            deserialize(derived().reader(), result);
            derived().reader().read_rest();
            p->resolve(result);
          } else {
            p->reject(eptr);
          }
        };
        derived().send_method_request_impl(function_id, request_id, id, args...);
        return p;
      }

      // call_method: void return type
      template <typename Id, typename Rt, typename... Args>
      requires(Void<Rt>)
      std::shared_ptr<promise<void>> call_method(std::uint64_t function_id, Id id, Args... args) {
        auto p = std::make_shared<promise<void>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p](bool success, std::exception_ptr eptr) {
          if(success) {
            p->resolve();
          } else {
            p->reject(eptr);
          }
        };
        derived().send_method_request_impl(function_id, request_id, id, args...);
        return p;
      }

    protected:
      Derived& derived() noexcept { return static_cast<Derived&>(*this); }

      std::unordered_map<std::uint64_t, response_handler> response_handlers_;
      std::uint64_t last_request_id_ = 0;
  };

} // namespace serialization
