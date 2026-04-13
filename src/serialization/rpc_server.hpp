#pragma once
#include "writer.hpp"
#include "reader.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "promise.hpp"
#include "rpc_server_registry.hpp"
#include "rpc.hpp"
#include <functional>
#include <exception>

namespace serialization {

  template <Reader Rd, Writer Wr>
  class rpc_server : public rpc_server_registry<Rd, Wr, rpc_server<Rd, Wr>> {

    public:
      rpc_server(std::function<void()> before_send = []() { }, std::function<void()> sender = []() { })
        : before_send_(before_send), sender_(sender) {}
      ~rpc_server() {}

      std::shared_ptr<serialization::promise<void>> handle_call(Rd& reader, Wr& writer) {
        rpc_request req;
        deserialize(reader, req);
        reader.read_rest();

        if(req.function_id >= this->functions_.size()) {
          send_error(writer, req.request_id, std::runtime_error("Function not found"));
          return serialization::promise<void>::resolved();
        }
        return this->functions_[req.function_id](reader, writer, req.request_id);
      }

    private:

      friend class rpc_server_registry<Rd, Wr, rpc_server<Rd, Wr>>;

      std::function<void()> before_send_;
      std::function<void()> sender_;

      void send_response(Wr& writer, uint64_t request_id) {
        before_send_();
        unsigned char success = 1;
        serialize(writer, success);
        serialize(writer, rpc_response{request_id});
        writer.write_rest();
        sender_();
      }

      template <typename Rt>
      void send_response(Wr& writer, uint64_t request_id, Rt& result) {
        before_send_();
        unsigned char success = 1;
        serialize(writer, success);
        serialize(writer, rpc_response{request_id});
        serialize(writer, result);
        writer.write_rest();
        sender_();
      }

      void send_error(Wr& writer, uint64_t request_id, const std::exception& e) {
        before_send_();        
        unsigned char success = 0;
        serialize(writer, success);
        serialize(writer, rpc_error{request_id});
        try {
          writer.write_exception(e);
        } catch(unknown_exception& ex) {
          auto rt = unknown_exception(std::string("Unknown exception: ") + e.what());
          writer.write_exception(rt);
        }
        writer.write_rest();
        sender_();
      }

      template <typename Rt>
      requires(!PromisePointer<Rt>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, Wr& writer, uint64_t request_id
      ) {
        try {
          Rt result = function();
          send_response(writer, request_id, result);
        } catch(const std::exception& e) {
          send_error(writer, request_id, e);
        } catch(...) {
          send_error(writer, request_id, unknown_exception("Unknown exception"));
        }
        return serialization::promise<void>::resolved();
      }

      template <typename Rt>
      requires(PromisePointer<Rt>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, Wr& writer, uint64_t request_id
      ) {
        auto p = std::make_shared<serialization::promise<void>>();
        try {
          Rt result = function();
          result->handle([&](typename Rt::element_type::promised_type& result) {
            send_response(writer, request_id, result);
            p->resolve();
          }, [&](std::exception_ptr exception) {
            try {
              std::rethrow_exception(exception);
            } catch(const std::exception& e) {
              send_error(writer, request_id, e);
            } catch(...) {
              send_error(writer, request_id, unknown_exception("Unknown exception"));
            }
            p->resolve();
          });
        } catch(const std::exception& e) {
          send_error(writer, request_id, e);
          p->resolve();
        } catch(...) {
          send_error(writer, request_id, unknown_exception("Unknown exception"));
          p->resolve();
        }
        return p;
      }

      template <>
      inline std::shared_ptr<serialization::promise<void>> process_call<void>(
        std::function<void()> function, Wr& writer, uint64_t request_id
      ) {
        try {
          function();
          send_response(writer, request_id);
        } catch(const std::exception& e) {
          send_error(writer, request_id, e);
        } catch(...) {
          send_error(writer, request_id, unknown_exception("Unknown exception"));
        }
        return serialization::promise<void>::resolved();
      }

      template <>
      inline std::shared_ptr<serialization::promise<void>> process_call<std::shared_ptr<serialization::promise<void>>>(
        std::function<std::shared_ptr<serialization::promise<void>>()> function,
        Wr& writer,
        uint64_t request_id
      ) {
        auto p = std::make_shared<serialization::promise<void>>();
        try {
          std::shared_ptr<serialization::promise<void>> result = function();
          result->handle([&]() {
            send_response(writer, request_id);
            p->resolve();
          }, [&](std::exception_ptr exception) {
            try {
              std::rethrow_exception(exception);
            } catch(const std::exception& e) {              
              send_error(writer, request_id, e);
            } catch(...) {
              send_error(writer, request_id, unknown_exception("Unknown exception"));
            }
            p->resolve();
          });
        } catch(const std::exception& e) {
          send_error(writer, request_id, e);
          p->resolve();
        } catch(...) {
          send_error(writer, request_id, unknown_exception("Unknown exception"));
          p->resolve();
        }
        return p;
      }

  };

}
