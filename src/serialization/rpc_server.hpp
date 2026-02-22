#pragma once
#include "writer.hpp"
#include "reader.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "type_concepts.hpp"
#include "promise.hpp"
#include "rpc_server_registry.hpp"
#include <tuple>
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
        std::uint64_t request_id;
        reader.read_u64(request_id);        
        std::uint64_t function_id;
        reader.read_u64(function_id);
        if (function_id >= this->functions_.size()) {
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string("Function not found");
          return serialization::promise<void>::resolved();
        }
        return this->functions_[function_id](reader, writer, request_id);
      }

    private:

      friend class rpc_server_registry<Rd, Wr, rpc_server<Rd, Wr>>;

      std::function<void()> before_send_;
      std::function<void()> sender_;

      template <typename Rt> 
      requires(!PromisePointer<Rt>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, Wr& writer, uint64_t request_id
      ) {
        try {
          Rt result = function();
          before_send_();
          writer.write_u64(request_id);
          writer.write_bool(true);
          serialize(writer, result);
          writer.write_rest();
          sender_();
        } catch(const std::exception& e) {          
          before_send_();
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(e.what());
          sender_();
        } catch(...) {            
          std::string error_msg = "Unknown non-standard exception";          
          before_send_();
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(error_msg);
          sender_();
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
          result->handle([&](Rt::element_type::promised_type& result) {
            before_send_();
            writer.write_u64(request_id);
            writer.write_bool(true);
            serialize(writer, result);
            sender_();
            p->resolve();
          }, [&](std::exception_ptr exception) {
            before_send_();
            writer.write_u64(request_id);
            try {
              std::rethrow_exception(exception);
            }  catch(const std::exception& e) {              
              writer.write_bool(false);
              writer.write_string(e.what());
            } catch(...) {
              std::string error_msg = "Unknown non-standard exception";          
              writer.write_bool(false);
              writer.write_string(error_msg);
            }
            sender_();
            p->resolve();
          });
        } catch(const std::exception& e) {
          before_send_();
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(e.what());
          sender_();
          p->resolve();
        } catch(...) {            
          std::string error_msg = "Unknown non-standard exception";          
          before_send_();
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(error_msg);
          sender_();
          p->resolve();
        }          
        return p;
      }
      
      template <>
      inline std::shared_ptr<serialization::promise<void>> process_call<void>(std::function<void()> function, Wr& writer, uint64_t request_id) {
        try {
          function();
          before_send_();
          writer.write_u64(request_id);
          writer.write_bool(true);
          sender_();
        } catch(const std::exception& e) {
          before_send_();
          writer.write_bool(false);
          writer.write_string(e.what());
          sender_();
        } catch(...) {            
          before_send_();
          std::string error_msg = "Unknown non-standard exception";          
          writer.write_bool(false);
          writer.write_string(error_msg);
          sender_();
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
            before_send_();
            writer.write_u64(request_id);
            writer.write_bool(true);
            sender_();
            p->resolve();
          }, [&](std::exception_ptr exception) {
            before_send_();
            writer.write_u64(request_id);
            try {
              std::rethrow_exception(exception);
            }  catch(const std::exception& e) {              
              writer.write_bool(false);
              writer.write_string(e.what());
            } catch(...) {
              std::string error_msg = "Unknown non-standard exception";          
              writer.write_bool(false);
              writer.write_string(error_msg);
            }
            sender_();
            p->resolve();
          });
        } catch(const std::exception& e) {
          before_send_();
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(e.what());
          sender_();
          p->resolve();
        } catch(...) {            
          std::string error_msg = "Unknown non-standard exception";          
          before_send_();
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(error_msg);
          sender_();
          p->resolve();
        }          
        return p;
      }


  };

}