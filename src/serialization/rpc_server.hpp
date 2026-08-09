#pragma once
#include "writer.hpp"
#include "reader.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "promise.hpp"
#include "rpc_server_registry.hpp"
#include "rpc_process_call_mixin.hpp"
#include "rpc.hpp"
#include <functional>
#include <exception>

namespace serialization {

  template <Reader Rd, Writer Wr>
  class rpc_server
    : public rpc_server_registry<Rd, Wr, rpc_server<Rd, Wr>>,
      public rpc_process_call_mixin<rpc_server<Rd, Wr>> {

    public:
      rpc_server(std::function<void()> before_send = []() { }, std::function<void()> sender = []() { })
        : before_send_(before_send), sender_(sender) {}
      ~rpc_server() {}

      std::shared_ptr<serialization::promise<void>> handle_call(Rd& reader, Wr& writer) {
        current_writer_ = &writer;
        rpc_request req;
        deserialize(reader, req);
        reader.read_rest();

        if(req.function_id >= this->functions_.size()) {
          send_error(req.request_id, std::runtime_error("Function not found"));
          return serialization::promise<void>::resolved();
        }
        return this->functions_[req.function_id](reader, writer, req.request_id);
      }

    private:

      friend class rpc_server_registry<Rd, Wr, rpc_server<Rd, Wr>>;
      friend class rpc_process_call_mixin<rpc_server<Rd, Wr>>;

      std::function<void()> before_send_;
      std::function<void()> sender_;
      Wr* current_writer_ = nullptr;

    protected:
      void send_response(uint64_t request_id) {
        before_send_();
        unsigned char success = 1;
        serialize(*current_writer_, success);
        serialize(*current_writer_, rpc_response{request_id});
        current_writer_->write_rest();
        sender_();
      }

      template <typename Rt>
      void send_response(uint64_t request_id, Rt& result) {
        before_send_();
        unsigned char success = 1;
        serialize(*current_writer_, success);
        serialize(*current_writer_, rpc_response{request_id});
        serialize(*current_writer_, result);
        current_writer_->write_rest();
        sender_();
      }

      void send_error(uint64_t request_id, const std::exception& e) {
        before_send_();
        unsigned char success = 0;
        serialize(*current_writer_, success);
        serialize(*current_writer_, rpc_error{request_id});
        try {
          current_writer_->write_exception(e);
        } catch(unknown_exception& ex) {
          auto rt = unknown_exception(std::string("Unknown exception: ") + e.what());
          current_writer_->write_exception(rt);
        }
        current_writer_->write_rest();
        sender_();
      }

  };

}
