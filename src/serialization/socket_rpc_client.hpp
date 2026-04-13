#pragma once

#include "socket_message_channel.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "promise.hpp"
#include "rpc.hpp"
#include <memory>
#include <unordered_map>
#include <functional>
#include <tuple>
#include <exception>
#include <string>

namespace serialization {

  class socket_rpc_client {
    public:
      socket_rpc_client(uint32_t socket_id)
        : channel_(socket_id) {

        channel_.on_message<rpc_response>([this](rpc_response& resp, size_t payload_size) {
          auto it = response_handlers_.find(resp.request_id);
          if(it == response_handlers_.end()) {
            throw std::runtime_error("Response handler " + std::to_string(resp.request_id) + " not found");
          }
          it->second(true, payload_size, nullptr);
          response_handlers_.erase(it);
        });

        channel_.on_message<rpc_error>([this](rpc_error& err, size_t) {
          //printf("on_message<rpc_error>: %lu\n", err.request_id);
          auto it = response_handlers_.find(err.request_id);
          if(it == response_handlers_.end()) {
            throw std::runtime_error("Response handler " + std::to_string(err.request_id) + " not found");
          }
          std::exception_ptr eptr = channel_.reader().read_exception();          
          channel_.reader().read_rest();
          //printf("on_message<rpc_error>: read exception: %s %lu\n", typeid(eptr).name(), typeid(eptr).hash_code());
          it->second(false, 0, eptr);
          response_handlers_.erase(it);
        });
      }
      ~socket_rpc_client() {}

      void register_basic_types() {
        channel_.register_message_type<rpc_request>();
        channel_.register_message_type<rpc_response>();
        channel_.register_message_type<rpc_error>();
        channel_.register_exception_type<unknown_exception>();
      }

      template <typename Rt, typename... Args>
      requires(!Void<Rt>)
      std::shared_ptr<promise<Rt>> call(std::uint64_t function_id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;

        response_handlers_[request_id] = [p, this](bool success, size_t payload_size, std::exception_ptr eptr) {
          if(success) {
            Rt result;
            deserialize(channel_.reader(), result);
            channel_.reader().read_rest();
            p->resolve(result);
          } else {
            p->reject(eptr);
          }
        };

        auto& writer = channel_.begin_send(rpc_request{request_id, function_id});
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(writer, args_tuple);
        writer.write_rest();
        channel_.commit_send();
        return p;
      }

      template <typename Rt, typename... Args>
      requires(Void<Rt>)
      std::shared_ptr<promise<void>> call(std::uint64_t function_id, Args... args) {
        auto p = std::make_shared<promise<void>>();
        std::uint64_t request_id = last_request_id_++;

        response_handlers_[request_id] = [p](bool success, size_t, std::exception_ptr eptr) {
          if(success) {
            p->resolve();
          } else {
            p->reject(eptr);
          }
        };

        auto& writer = channel_.begin_send(rpc_request{request_id, function_id});
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(writer, args_tuple);
        writer.write_rest();
        channel_.commit_send();
        return p;
      }

      template <typename Id, typename Rt, typename... Args>
      requires(!Void<Rt>)
      std::shared_ptr<promise<Rt>> call_method(std::uint64_t function_id, Id id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;

        response_handlers_[request_id] = [p, this](bool success, size_t payload_size, std::exception_ptr eptr) {
          if(success) {
            Rt result;
            deserialize(channel_.reader(), result);
            channel_.reader().read_rest();
            p->resolve(result);
          } else {
            p->reject(eptr);
          }
        };

        auto& writer = channel_.begin_send(rpc_request{request_id, function_id});
        serialize(writer, id);
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(writer, args_tuple);
        writer.write_rest();
        channel_.commit_send();
        return p;
      }

      template <typename Id, typename Rt, typename... Args>
      requires(Void<Rt>)
      std::shared_ptr<promise<void>> call_method(std::uint64_t function_id, Id id, Args... args) {
        auto p = std::make_shared<promise<void>>();
        std::uint64_t request_id = last_request_id_++;

        response_handlers_[request_id] = [p](bool success, size_t, std::exception_ptr eptr) {
          if(success) {
            p->resolve();
          } else {
            p->reject(eptr);
          }
        };

        auto& writer = channel_.begin_send(rpc_request{request_id, function_id});
        serialize(writer, id);
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(writer, args_tuple);
        writer.write_rest();
        channel_.commit_send();
        return p;
      }

      void handle_data() {
        channel_.handle_data();
      }

      // Expose channel for user-defined message types
      template <typename T> void register_message_type() { channel_.register_message_type<T>(); }
      template <typename T> void on_message(std::function<void(T&)> handler) { channel_.on_message<T>(handler); }
      template <typename T> void on_message(std::function<void(T&, size_t)> handler) { channel_.on_message<T>(handler); }
      template <typename T> void send_message(const T& msg) { channel_.send_message(msg); }

      socket_message_channel& channel() { return channel_; }

    private:
      socket_message_channel channel_;
      std::unordered_map<std::uint64_t, std::function<void(bool, size_t, std::exception_ptr)>> response_handlers_;
      std::uint64_t last_request_id_ = 0;
  };

}
