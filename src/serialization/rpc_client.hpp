#pragma once
#include "writer.hpp"
#include "reader.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "type_concepts.hpp"
#include "promise.hpp"
#include <tuple>
#include <functional>
#include <sstream>
#include <exception>
#include <set>

//#define RPC_SERVER_DEBUG

namespace serialization {

  class rpc_exception : public std::exception {
    public:
      rpc_exception(const std::string& message) : message_(message) {}
      ~rpc_exception() {}
      const char* what() const noexcept override { return message_.c_str(); }
    private:
      std::string message_;
  };

  template <Reader Rd, Writer Wr>
  class rpc_client {

    public:      
      rpc_client(Wr& writer, std::function<void()> before_send, std::function<void()> sender) 
        : writer_(writer), before_send_(before_send), sender_(sender) {}
      ~rpc_client() {}    

/*       template <typename Rt, typename... Args>
      void call_callback(std::function<void(bool, Rt, std::string)> callback, std::uint64_t function_id, Args... args) {
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [callback, request_id, this](Rd& reader) {
          Rt result;
          std::string error_message;
          bool success = read_result(reader, result, error_message);
          callback(success, result, error_message);
          response_handlers_.erase(request_id);
        };
        writer_.write_u64(request_id);
        write_call(writer_, function_id, args...);
        sender_();
      } */

      template <typename Rt, typename... Args>
      requires(!Void<Rt>)
      std::shared_ptr<promise<Rt>> call(std::uint64_t function_id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p, request_id, this](Rd& reader) {
          Rt result;
          std::string error_message;
          bool success = read_result(reader, result, error_message);
          if(success) {
            p->resolve(result);
          } else {
            p->reject(std::make_exception_ptr(rpc_exception(error_message)));
          }
          response_handlers_.erase(request_id);
        };
        before_send_();
        writer_.write_u64(request_id);
        write_call(writer_, function_id, args...);
        sender_();
        return p;
      }

      template <typename Rt, typename... Args>
      requires(Void<Rt>)
      std::shared_ptr<promise<void>> call(std::uint64_t function_id, Args... args) {
        auto p = std::make_shared<promise<void>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p, request_id, this](Rd& reader) {
          std::string error_message;
          bool success = read_result(reader, error_message);
          if(success) {
            p->resolve();
          } else {
            p->reject(std::make_exception_ptr(rpc_exception(error_message)));
          }
          response_handlers_.erase(request_id);
        };
        before_send_();
        writer_.write_u64(request_id);
        write_call(writer_, function_id, args...);
        sender_();
        return p;
      }

      template <typename Id, typename Rt, typename... Args>
      requires(!Void<Rt>)
      std::shared_ptr<promise<Rt>> call_method(std::uint64_t function_id, Id id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p, request_id, this](Rd& reader) {
          Rt result;
          std::string error_message;
          bool success = read_result(reader, result, error_message);
          if(success) {
            p->resolve(result);
          } else {
            p->reject(std::make_exception_ptr(rpc_exception(error_message)));
          }
          response_handlers_.erase(request_id);
        };
        before_send_();
        writer_.write_u64(request_id);
        write_method_call(writer_, function_id, id, args...);
        sender_();
        return p;
      }

      template <typename Id, typename Rt, typename... Args>
      requires(Void<Rt>)
      std::shared_ptr<promise<Rt>> call_method(std::uint64_t function_id, Id id, Args... args) {
        auto p = std::make_shared<promise<Rt>>();
        std::uint64_t request_id = last_request_id_++;
        response_handlers_[request_id] = [p, request_id, this](Rd& reader) {
          std::string error_message;
          bool success = read_result(reader, error_message);
          if(success) {
            p->resolve();
          } else {
            p->reject(std::make_exception_ptr(rpc_exception(error_message)));
          }
          response_handlers_.erase(request_id);
        };
        before_send_();
        writer_.write_u64(request_id);
        write_method_call(writer_, function_id, id, args...);
        sender_();
        return p;
      }

      template <typename... Args>
      static void write_call(Wr& writer, std::uint64_t function_id, Args... args) {
        writer.write_u64(function_id);
        /// convert args to tuple
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(writer, args_tuple);
        writer.write_rest();
      }

      template <typename Id, typename... Args>
      static void write_method_call(Wr& writer, std::uint64_t function_id, Id id, Args... args) {
        writer.write_u64(function_id);
        serialize(writer, id);
        /// convert args to tuple
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(writer, args_tuple);
        writer.write_rest();
      }

      template <typename Rt>
      static bool read_result(Rd& reader, Rt& result, std::string& error_message) {
        bool success;
        reader.read_bool(success);
        if(!success) {
          reader.read_string(error_message);
          return false;
        }
        deserialize(reader, result);
        reader.read_rest();
        return true;
      }

      static bool read_result(Rd& reader, std::string& error_message) {
        bool success;
        reader.read_bool(success);
        if(!success) {
          reader.read_string(error_message);
          return false;
        }
        return true;
      }

      void handle_response(Rd& reader) {
        std::uint64_t request_id;
        reader.read_u64(request_id);
        if(response_handlers_.find(request_id) != response_handlers_.end()) {
          response_handlers_[request_id](reader);
        } else {
          throw std::runtime_error("Response handler" + std::to_string(request_id) + " not found");
        }
      }

    private:   

      std::unordered_map<std::uint32_t, std::function<void(Rd&)>> response_handlers_;
      std::uint32_t last_request_id_ = 0;
      Wr& writer_;
      std::function<void()> before_send_;
      std::function<void()> sender_;      

  };

}