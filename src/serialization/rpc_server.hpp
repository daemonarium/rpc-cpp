#pragma once
#include "writer.hpp"
#include "reader.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "type_concepts.hpp"
#include "promise.hpp"
#include <tuple>
#include <functional>
#include <exception>
#include <set>

//#define RPC_SERVER_DEBUG

namespace serialization {

  template <Reader Rd, Writer Wr>
  class rpc_server {    

    public:
      rpc_server() {}
      ~rpc_server() {}    

      template <typename T, typename Srv>    
      requires(ContainerButNotString<T> && !Binary<T>)
      static void register_recursive(T value, Srv& server) {
        register_recursive(typename T::value_type(), server);
      }

      template <typename T, typename Srv>   
      requires(Map<T>) 
      static void register_recursive(T value, Srv& server) {
        register_recursive(typename T::key_type(), server);
        register_recursive(typename T::value_type(), server);
      }

      template <typename T, typename Srv>
      requires(UnsignedInteger<T> || SignedInteger<T> || FloatingPoint<T> || Boolean<T> || Void<T> || String<T>
                                  || Binary<T> || Enum<T>)
      static void register_recursive(T value, Srv& server) {
        /// ignore
      }

      template <typename T, typename Srv>
      requires(UniquePointer<T> && !Polymorphic<T>)
      static void register_recursive(T value, Srv& server) {
        register_recursive(*value, server);
      }

      template <typename T, typename Srv>
      requires(SharedPointer<T>)
      static void register_recursive(T value, Srv& server) {
        /// ignore
      }

      std::set<std::string> registered_types;

      template <std::meta::info member, typename Srv>
      static void register_parameters_and_return(Srv& server) {
        [:serialization::expand(std::meta::parameters_of(member)):] >> [&]<auto param> {
          auto param_value = typename [:std::meta::type_of(param):]();
          register_recursive(param_value, server);
        };            
        constexpr std::meta::info return_type = std::meta::return_type_of(member);
        // Use display_string to check if it's a reference or void type
        constexpr std::string_view return_type_str = std::meta::display_string_of(return_type);
        if constexpr (return_type_str.find('&') == std::string_view::npos && return_type_str != "void" 
                      && return_type_str != "shared_ptr<promise<void>>") {
          // Not a reference and not void - safe to create object
          auto return_value = typename [:return_type:]();
          auto depromisified_value = depromisify(return_value);
          register_recursive(depromisified_value, server);
        } else if constexpr (return_type_str.find('&') != std::string_view::npos) {
          // Is a reference - remove reference and create object
          constexpr std::meta::info referenced_type = std::meta::remove_reference(return_type);
          auto return_value = typename [:referenced_type:]();
          auto depromisified_value = depromisify(return_value);
          register_recursive(depromisified_value, server);
        } 
      }

      template <typename T, typename Srv>
      requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T>)
      static void register_recursive(T value, Srv& server) {
        std::string_view name;
        if constexpr (std::meta::has_identifier(^^T)) {
          name = std::meta::identifier_of(^^T);
        } else {
          name = std::meta::display_string_of(^^T);
        }   
        if(server.registered_types.find(std::string(name)) != server.registered_types.end()) return;
        server.registered_types.insert(std::string(name));
        server.debug("rpc registering type %s", std::string(name).c_str());
        server.debug_indent(1);

        [:expand(std::meta::bases_of(^^T, std::meta::access_context::unchecked())):] >> [&]<std::meta::info base> {
          register_recursive(typename [:std::meta::type_of(base):](), server);
        };

        [:expand(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())):] >> [&]<auto dm> {
          bool ignore = false;
          [:serialization::expand(std::meta::annotations_of(dm)):] >> [&]<auto a> {
            if constexpr(std::meta::type_of(a) == ^^serialization::policy) {
              ignore = true;
            }
          }; 
          if(ignore) return;        
          register_recursive(value.[:dm:], server);
        };

        [:serialization::expand(
          std::meta::members_of(^^T, std::meta::access_context::unchecked())
        ):] >> [&]<std::meta::info member> {  
          if constexpr (std::meta::is_function(member)
                     && std::meta::is_static_member(member) 
                     && std::meta::has_identifier(member) 
                     && std::meta::is_public(member)
                     && !is_member_ignored(member)) {           
            register_parameters_and_return<member>(server);

            server.debug("rpc static method %s", std::string(std::meta::identifier_of(member)).c_str());
            constexpr auto static_func_ptr = &[:member:];
            server.register_static_method(&[:member:]);
          }
        };

        constexpr bool non_static_rpc_callable = []() constexpr -> bool {
          bool found = false;
          [:serialization::expand(
            std::meta::members_of(^^T, std::meta::access_context::unchecked())
          ):] >> [&]<std::meta::info member> { 
            if constexpr (std::meta::is_function(member)
                       && std::meta::is_static_member(member) 
                       && std::meta::has_identifier(member)) {
              constexpr std::string_view name = std::meta::identifier_of(member);
              if constexpr (name == "get_rpc_object") {
                found = true;
              }
            }
          };
          return found;
        }();        

        constexpr bool singleton_rpc_callable = []() constexpr -> bool {
          bool found = false;
          [:serialization::expand(
            std::meta::members_of(^^T, std::meta::access_context::unchecked())
          ):] >> [&]<std::meta::info member> {  
            if constexpr (std::meta::is_function(member) 
                       && std::meta::is_static_member(member)
                       && std::meta::has_identifier(member)) {
              constexpr std::string_view name = std::meta::identifier_of(member);
              if constexpr (name == "get_rpc_instance") {
                found = true;
              }
            }
          };
          return found;
        }();

        if(non_static_rpc_callable && singleton_rpc_callable) {
          throw std::runtime_error("Class " + std::string(std::meta::display_string_of(^^T))
            + " has both non-static and singleton rpc callables");
        }

        if(non_static_rpc_callable) {
          server.debug("rpc non-static rpc callable %s", std::string(name).c_str());
        }
        if(singleton_rpc_callable) {
          server.debug("rpc singleton rpc callable %s", std::string(name).c_str());
        }

        if constexpr (non_static_rpc_callable) {
          [:serialization::expand(
            std::meta::members_of(^^T, std::meta::access_context::unchecked())
          ):] >> [&]<std::meta::info member> {
            if constexpr (!std::meta::is_static_member(member)
                       && std::meta::is_public(member) 
                       && std::meta::has_identifier(member) 
                       && std::meta::is_function(member)
                       && !is_member_ignored(member)) {              
              register_parameters_and_return<member>(server);
              // Convert reflection info to member function pointer
              server.debug("rpc dynamic method %s", std::string(std::meta::identifier_of(member)).c_str());
              constexpr auto member_func_ptr = &[:member:];            
              server.register_dynamic_method(T::get_rpc_object, member_func_ptr);                
            }
          };
        }

        if constexpr (singleton_rpc_callable) {           
          [:serialization::expand(
            std::meta::members_of(^^T, std::meta::access_context::unchecked())
          ):] >> [&]<std::meta::info member> {
            if constexpr (!std::meta::is_static_member(member) 
                        && std::meta::is_public(member)
                        && std::meta::has_identifier(member)
                        && std::meta::is_function(member)
                        && !is_member_ignored(member)) {              
              register_parameters_and_return<member>(server);
              // Convert reflection info to member function pointer
              server.debug("rpc singleton method %s", std::string(std::meta::identifier_of(member)).c_str());
              constexpr auto member_func_ptr = &[:member:];
              server.register_singleton_method(member_func_ptr);                
            }
          };
        }
        server.debug_indent(-1);
      }

    private: 

      std::vector<std::function<std::shared_ptr<serialization::promise<void>>(Rd&, Wr&, uint64_t)>> functions_;

    public:

/*       template <typename Rt, typename... Args>
      void register_function(std::function<Rt(Args...)> callable) {
        functions_.push_back([this, callable](Rd& reader, Wr& writer) {
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          this->process_call([&]() { return std::apply(callable, args); }, writer);
        });
      }

      template <typename Rt, typename... Args>
      void register_function(Rt(*callable)(Args...)) {
        functions_.push_back([this, callable](Rd& reader, Wr& writer) {
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          this->process_call<Rt>([&]() -> Rt { return std::apply(callable, args); }, writer);
        });
      }
 */
      template<typename Rt, typename... Args>
      void register_static_method(Rt(*callable)(Args...)) {
        functions_.push_back([this, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          return this->process_call<Rt>([&]() -> Rt { return std::apply(callable, args); }, writer, request_id);
        });
      }

      template <typename Id, typename Class, typename Rt, typename... Args> 
      void register_dynamic_method(Class&(fetch_object)(Id), Rt(Class::*callable)(Args...)) {
        functions_.push_back([this, fetch_object, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          Id id;
          deserialize(reader, id);
          Class& object = fetch_object(id);
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          return this->process_call<Rt>([&]() -> Rt { 
            return std::apply([&](auto&&... args) { return (object.*callable)(args...); }, args); 
          }, writer, request_id);
        });
      }

      // Overload for const member functions
      template <typename Id, typename Class, typename Rt, typename... Args> 
      void register_dynamic_method(Class&(fetch_object)(Id), Rt(Class::*callable)(Args...) const) {
        functions_.push_back([this, fetch_object, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          Id id;
          deserialize(reader, id);
          Class& object = fetch_object(id);
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          return this->process_call<Rt>([&]() -> Rt {
            return std::apply([&](auto&&... args) { return (object.*callable)(args...); }, args); 
          }, writer, request_id);
        });
      }

      template <typename Class, typename Rt, typename... Args>
      void register_object_method(Class& object, Rt(Class::*callable)(Args...)) {
        functions_.push_back([this, object, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          return this->process_call<Rt>([&]() -> Rt { 
            return std::apply([&](auto&&... args) { return (object.*callable)(args...); }, args); 
          }, writer, request_id);
        });
      }

      // Overload for const member functions
      template <typename Class, typename Rt, typename... Args>
      void register_object_method(Class& object, Rt(Class::*callable)(Args...) const) {
        functions_.push_back([this, object, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          return this->process_call<Rt>([&]() -> Rt { 
            return std::apply([&](auto&&... args) { return (object.*callable)(args...); }, args); 
          }, writer, request_id);
        });
      }

      template <typename Class, typename Rt, typename... Args>
      void register_singleton_method(Rt(Class::*callable)(Args...)) {
        functions_.push_back([this, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          Class& instance = Class::get_rpc_instance();
          return this->process_call<Rt>([&]() -> Rt { return std::apply([&](auto&&... args) {
            return (instance.*callable)(args...); }, args); 
          }, writer, request_id);
        });
      }

      // Overload for const member functions
      template <typename Class, typename Rt, typename... Args>
      void register_singleton_method(Rt(Class::*callable)(Args...) const) {
        functions_.push_back([this, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          Class& instance = Class::get_rpc_instance();
          return this->process_call<Rt>([&]() -> Rt { return std::apply([&](auto&&... args) {
            return (instance.*callable)(args...); }, args); 
          }, writer, request_id);
        });
      }
      
      std::shared_ptr<serialization::promise<void>> handle_call(Rd& reader, Wr& writer) {
        std::uint64_t request_id;
        reader.read_u64(request_id);        
        std::uint64_t function_id;
        reader.read_u64(function_id);
        if (function_id >= functions_.size()) {
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string("Function not found");
          return serialization::promise<void>::resolved();
        }
        return functions_[function_id](reader, writer, request_id);
      }

    private:

      template<std::size_t I, typename... Args>
      void deserialize_args(Rd& reader, std::tuple<Args...>& args) {
        if constexpr (I < sizeof...(Args)) {
          deserialize(reader, std::get<I>(args));
          deserialize_args<I + 1>(reader, args);
          reader.read_rest();
        }
      }

      template <typename Rt> 
      requires(!PromisePointer<Rt>)
      inline std::shared_ptr<serialization::promise<void>> process_call(
        std::function<Rt()> function, Wr& writer, uint64_t request_id
      ) {
        try {
          Rt result = function();
          writer.write_u64(request_id);
          writer.write_bool(true);
          serialize(writer, result);
          writer.write_rest();
        } catch(const std::exception& e) {          
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(e.what());
        } catch(...) {            
          std::string error_msg = "Unknown non-standard exception";          
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(error_msg);
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
            writer.write_u64(request_id);
            writer.write_bool(true);
            serialize(writer, result);
            p->resolve();
          }, [&](std::exception_ptr exception) {
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
            p->resolve();
          });
        } catch(const std::exception& e) {
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(e.what());
          p->resolve();
        } catch(...) {            
          std::string error_msg = "Unknown non-standard exception";          
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(error_msg);
          p->resolve();
        }          
        return p;
      }
      
      template <>
      inline std::shared_ptr<serialization::promise<void>> process_call<void>(std::function<void()> function, Wr& writer, uint64_t request_id) {
        try {
          function();
          writer.write_u64(request_id);
          writer.write_bool(true);
        } catch(const std::exception& e) {
          writer.write_bool(false);
          writer.write_string(e.what());
        } catch(...) {            
          std::string error_msg = "Unknown non-standard exception";          
          writer.write_bool(false);
          writer.write_string(error_msg);
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
            writer.write_u64(request_id);
            writer.write_bool(true);
            p->resolve();
          }, [&](std::exception_ptr exception) {
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
            p->resolve();
          });
        } catch(const std::exception& e) {
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(e.what());
          p->resolve();
        } catch(...) {            
          std::string error_msg = "Unknown non-standard exception";          
          writer.write_u64(request_id);
          writer.write_bool(false);
          writer.write_string(error_msg);
          p->resolve();
        }          
        return p;
      }

      #ifdef RPC_SERVER_DEBUG
      uint32_t indent_ = 0;
      #endif

      inline void debug_indent(int delta) {
        #ifdef RPC_SERVER_DEBUG
        indent_ += delta;        
        #endif
      }

      inline void debug(const char* format, ...) {
        #ifdef RPC_SERVER_DEBUG
        va_list args;
        va_start(args, format);
        fprintf(stderr, "%s", std::string(indent_*2, ' ').c_str());
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
        va_end(args);
        #endif
      }

  };

}