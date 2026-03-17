#pragma once
#include "writer.hpp"
#include "reader.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "type_concepts.hpp"
#include "promise.hpp"
#include "rpc.hpp"
#include <tuple>
#include <functional>
#include <exception>

namespace serialization {

  template <Reader Rd, Writer Wr>
  class rpc_client {

    public:
      rpc_client(Wr& writer, std::function<void()> before_send, std::function<void()> sender)
        : writer_(writer), before_send_(before_send), sender_(sender) {}
      ~rpc_client() {}

      template <typename Rt, typename... Args>
      requires(!Void<Rt>)
      std::shared_ptr<promise<Rt>> call(std::uint64_t function_id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p, request_id, this](bool success, Rd& reader, std::exception_ptr eptr) {
          if(success) {
            Rt result;
            deserialize(reader, result);
            reader.read_rest();
            p->resolve(result);
          } else {
            p->reject(eptr);
          }
          response_handlers_.erase(request_id);
        };
        send_request(function_id, request_id, args...);
        return p;
      }

      template <typename Rt, typename... Args>
      requires(Void<Rt>)
      std::shared_ptr<promise<void>> call(std::uint64_t function_id, Args... args) {
        auto p = std::make_shared<promise<void>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p, request_id, this](bool success, Rd&, std::exception_ptr eptr) {
          if(success) {
            p->resolve();
          } else {
            p->reject(eptr);
          }
          response_handlers_.erase(request_id);
        };
        send_request(function_id, request_id, args...);
        return p;
      }

      template <typename Id, typename Rt, typename... Args>
      requires(!Void<Rt>)
      std::shared_ptr<promise<Rt>> call_method(std::uint64_t function_id, Id id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p, request_id, this](bool success, Rd& reader, std::exception_ptr eptr) {
          if(success) {
            Rt result;
            deserialize(reader, result);
            reader.read_rest();
            p->resolve(result);
          } else {
            p->reject(eptr);
          }
          response_handlers_.erase(request_id);
        };
        send_method_request(function_id, request_id, id, args...);
        return p;
      }

      template <typename Id, typename Rt, typename... Args>
      requires(Void<Rt>)
      std::shared_ptr<promise<Rt>> call_method(std::uint64_t function_id, Id id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p, request_id, this](bool success, Rd&, std::exception_ptr eptr) {
          if(success) {
            p->resolve();
          } else {
            p->reject(eptr);
          }
          response_handlers_.erase(request_id);
        };
        send_method_request(function_id, request_id, id, args...);
        return p;
      }

      void handle_response(Rd& reader) {
        rpc_response header;
        unsigned char success;
        deserialize(reader, success);
        deserialize(reader, header);
        reader.read_rest();

        auto it = response_handlers_.find(header.request_id);
        if(it == response_handlers_.end()) {
          throw std::runtime_error("Response handler " + std::to_string(header.request_id) + " not found");
        }

        if(success) {
          it->second(true, reader, nullptr);
        } else {
          std::exception_ptr eptr = reader.read_exception();
          reader.read_rest();
          it->second(false, reader, eptr);
        }
      }

    private:

      std::unordered_map<std::uint32_t, std::function<void(bool, Rd&, std::exception_ptr)>> response_handlers_;
      std::uint32_t last_request_id_ = 0;
      Wr& writer_;
      std::function<void()> before_send_;
      std::function<void()> sender_;

      template <typename... Args>
      void send_request(std::uint64_t function_id, std::uint64_t request_id, Args... args) {
        before_send_();
        serialize(writer_, rpc_request{request_id, function_id});
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(writer_, args_tuple);
        writer_.write_rest();
        sender_();
      }

      template <typename Id, typename... Args>
      void send_method_request(std::uint64_t function_id, std::uint64_t request_id, Id id, Args... args) {
        before_send_();
        serialize(writer_, rpc_request{request_id, function_id});
        serialize(writer_, id);
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(writer_, args_tuple);
        writer_.write_rest();
        sender_();
      }

  };

}
