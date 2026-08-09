#pragma once
#include "writer.hpp"
#include "reader.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "type_concepts.hpp"
#include "promise.hpp"
#include "rpc.hpp"
#include "rpc_call_mixin.hpp"
#include <tuple>
#include <functional>
#include <exception>

namespace serialization {

  template <Reader Rd, Writer Wr>
  class rpc_client : public rpc_call_mixin<rpc_client<Rd, Wr>> {

    public:
      rpc_client(Wr& writer, std::function<void()> before_send, std::function<void()> sender)
        : writer_(writer), before_send_(before_send), sender_(sender) {}
      ~rpc_client() {}

      void handle_response(Rd& reader) {
        current_reader_ = &reader;
        rpc_response header;
        unsigned char success;
        deserialize(reader, success);
        deserialize(reader, header);
        reader.read_rest();

        auto it = this->response_handlers_.find(header.request_id);
        if(it == this->response_handlers_.end()) {
          throw std::runtime_error("Response handler " + std::to_string(header.request_id) + " not found");
        }

        if(success) {
          it->second(true, nullptr);
        } else {
          std::exception_ptr eptr = reader.read_exception();
          reader.read_rest();
          it->second(false, eptr);
        }
      }

      Rd& reader() { return *current_reader_; }

    private:
      friend class rpc_call_mixin<rpc_client<Rd, Wr>>;

      Wr& writer_;
      std::function<void()> before_send_;
      std::function<void()> sender_;
      Rd* current_reader_ = nullptr;

      template <typename... Args>
      void send_request_impl(std::uint64_t function_id, std::uint64_t request_id, Args... args) {
        before_send_();
        serialize(writer_, rpc_request{request_id, function_id});
        std::tuple<Args...> args_tuple = std::make_tuple(args...);
        serialize(writer_, args_tuple);
        writer_.write_rest();
        sender_();
      }

      template <typename Id, typename... Args>
      void send_method_request_impl(std::uint64_t function_id, std::uint64_t request_id, Id id, Args... args) {
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
