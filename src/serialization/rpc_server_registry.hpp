#pragma once
#include "writer.hpp"
#include "reader.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"
#include "type_concepts.hpp"
#include "promise.hpp"
#include "annotations.hpp"
#include "examples/structures/stream_protocol.hpp"
#include "observable/observable.hpp"
#include <functional>
#include <vector>
#include <set>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <tuple>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <utility>

//#define RPC_SERVER_DEBUG

namespace serialization {

  template <Reader Rd, Writer Wr, typename Derived>
  class rpc_server_registry {
    public:
      rpc_server_registry() {}
      ~rpc_server_registry() {}

    protected:
      Derived* derived() {
        return static_cast<Derived*>(this);
      }

      const Derived* derived() const {
        return static_cast<const Derived*>(this);
      }

    public:
      std::vector<std::function<std::shared_ptr<serialization::promise<void>>(Rd&, Wr&, uint64_t)>> functions_;
      std::set<std::string> registered_types;

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
        using Pointee = std::remove_cv_t<typename T::element_type>;
        register_recursive(Pointee{}, server);
      }

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
      requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && !SimpleException<T>)
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
            if constexpr (is_high_level_observable<member>()) {
              server.register_high_level_observable_method(&[:member:]);
            } else {
              server.register_static_method(&[:member:]);
            }
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
              if constexpr (is_high_level_observable<member>()) {
                server.register_high_level_observable_method(T::get_rpc_object, member_func_ptr);
              } else {
                server.register_dynamic_method(T::get_rpc_object, member_func_ptr);
              }                
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
              if constexpr (is_high_level_observable<member>()) {
                server.register_high_level_observable_method(member_func_ptr);
              } else {
                server.register_singleton_method(member_func_ptr);
              }                
            }
          };
        }
        server.debug_indent(-1);
      }

      template <typename T, typename Srv>
      requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && SimpleException<T>)
      static void register_recursive(T value, Srv& server) {
        std::string_view name;
        if constexpr (std::meta::has_identifier(^^T)) {
          name = std::meta::identifier_of(^^T);
        } else {
          name = std::meta::display_string_of(^^T);
        }   
        if(server.registered_types.find(std::string(name)) != server.registered_types.end()) return;
        server.registered_types.insert(std::string(name));
        server.debug("rpc registering simple exception type %s", std::string(name).c_str());
      }

      template<typename Rt, typename... Args>
      void register_static_method(Rt(*callable)(Args...)) {
        functions_.push_back([this, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          return derived()->template process_call<Rt>([&]() -> Rt { return std::apply(callable, args); }, writer, request_id);
        });
      }

      template<typename Rt, typename... Args>
      void register_high_level_observable_method(Rt(*callable)(Args...)) {
        functions_.push_back([this, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          (void)writer;
          std::tuple<waos::stream_handle, Args...> wire;
          deserialize_args<0>(reader, wire);
          waos::stream_handle stream = std::get<0>(wire);
          try {
            auto obs = std::apply(callable, drop_first(wire));
            derived()->attach_high_level_observable(stream, std::move(obs));
            derived()->send_response(request_id);
          } catch(const std::exception& e) {
            derived()->send_error(request_id, e);
          } catch(...) {
            derived()->send_error(request_id, std::runtime_error("uncaught exception in high-level observable"));
          }
          return serialization::promise<void>::resolved();
        });
      }

      template <typename Id, typename Class, typename Rt, typename... Args>
      void register_high_level_observable_method(Class&(fetch_object)(Id), Rt(Class::*callable)(Args...)) {
        functions_.push_back([this, fetch_object, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          (void)writer;
          Id id;
          deserialize(reader, id);
          Class& object = fetch_object(id);
          std::tuple<waos::stream_handle, Args...> wire;
          deserialize_args<0>(reader, wire);
          waos::stream_handle stream = std::get<0>(wire);
          try {
            auto obs = std::apply([&](auto&&... a) { return (object.*callable)(a...); }, drop_first(wire));
            derived()->attach_high_level_observable(stream, std::move(obs));
            derived()->send_response(request_id);
          } catch(const std::exception& e) {
            derived()->send_error(request_id, e);
          } catch(...) {
            derived()->send_error(request_id, std::runtime_error("uncaught exception in high-level observable"));
          }
          return serialization::promise<void>::resolved();
        });
      }

      template <typename Id, typename Class, typename Rt, typename... Args>
      void register_high_level_observable_method(Class&(fetch_object)(Id), Rt(Class::*callable)(Args...) const) {
        functions_.push_back([this, fetch_object, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          (void)writer;
          Id id;
          deserialize(reader, id);
          Class& object = fetch_object(id);
          std::tuple<waos::stream_handle, Args...> wire;
          deserialize_args<0>(reader, wire);
          waos::stream_handle stream = std::get<0>(wire);
          try {
            auto obs = std::apply([&](auto&&... a) { return (object.*callable)(a...); }, drop_first(wire));
            derived()->attach_high_level_observable(stream, std::move(obs));
            derived()->send_response(request_id);
          } catch(const std::exception& e) {
            derived()->send_error(request_id, e);
          } catch(...) {
            derived()->send_error(request_id, std::runtime_error("uncaught exception in high-level observable"));
          }
          return serialization::promise<void>::resolved();
        });
      }

      template <typename Class, typename Rt, typename... Args>
      void register_high_level_observable_method(Rt(Class::*callable)(Args...)) {
        functions_.push_back([this, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          (void)writer;
          std::tuple<waos::stream_handle, Args...> wire;
          deserialize_args<0>(reader, wire);
          waos::stream_handle stream = std::get<0>(wire);
          try {
            Class& instance = Class::get_rpc_instance();
            auto obs = std::apply([&](auto&&... a) { return (instance.*callable)(a...); }, drop_first(wire));
            derived()->attach_high_level_observable(stream, std::move(obs));
            derived()->send_response(request_id);
          } catch(const std::exception& e) {
            derived()->send_error(request_id, e);
          } catch(...) {
            derived()->send_error(request_id, std::runtime_error("uncaught exception in high-level observable"));
          }
          return serialization::promise<void>::resolved();
        });
      }

      template <typename Class, typename Rt, typename... Args>
      void register_high_level_observable_method(Rt(Class::*callable)(Args...) const) {
        functions_.push_back([this, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          (void)writer;
          std::tuple<waos::stream_handle, Args...> wire;
          deserialize_args<0>(reader, wire);
          waos::stream_handle stream = std::get<0>(wire);
          try {
            Class& instance = Class::get_rpc_instance();
            auto obs = std::apply([&](auto&&... a) { return (instance.*callable)(a...); }, drop_first(wire));
            derived()->attach_high_level_observable(stream, std::move(obs));
            derived()->send_response(request_id);
          } catch(const std::exception& e) {
            derived()->send_error(request_id, e);
          } catch(...) {
            derived()->send_error(request_id, std::runtime_error("uncaught exception in high-level observable"));
          }
          return serialization::promise<void>::resolved();
        });
      }

      template <typename Class, typename Rt, typename... Args>
      void register_high_level_observable_method(Class& object, Rt(Class::*callable)(Args...)) {
        functions_.push_back([this, &object, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          (void)writer;
          std::tuple<waos::stream_handle, Args...> wire;
          deserialize_args<0>(reader, wire);
          waos::stream_handle stream = std::get<0>(wire);
          try {
            auto obs = std::apply([&](auto&&... a) { return (object.*callable)(a...); }, drop_first(wire));
            derived()->attach_high_level_observable(stream, std::move(obs));
            derived()->send_response(request_id);
          } catch(const std::exception& e) {
            derived()->send_error(request_id, e);
          } catch(...) {
            derived()->send_error(request_id, std::runtime_error("uncaught exception in high-level observable"));
          }
          return serialization::promise<void>::resolved();
        });
      }

      template <typename Class, typename Rt, typename... Args>
      void register_high_level_observable_method(Class& object, Rt(Class::*callable)(Args...) const) {
        functions_.push_back([this, &object, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          (void)writer;
          std::tuple<waos::stream_handle, Args...> wire;
          deserialize_args<0>(reader, wire);
          waos::stream_handle stream = std::get<0>(wire);
          try {
            auto obs = std::apply([&](auto&&... a) { return (object.*callable)(a...); }, drop_first(wire));
            derived()->attach_high_level_observable(stream, std::move(obs));
            derived()->send_response(request_id);
          } catch(const std::exception& e) {
            derived()->send_error(request_id, e);
          } catch(...) {
            derived()->send_error(request_id, std::runtime_error("uncaught exception in high-level observable"));
          }
          return serialization::promise<void>::resolved();
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
          return derived()->template process_call<Rt>([&]() -> Rt { 
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
          return derived()->template process_call<Rt>([&]() -> Rt {
            return std::apply([&](auto&&... args) { return (object.*callable)(args...); }, args); 
          }, writer, request_id);
        });
      }

      template <typename Class, typename Rt, typename... Args>
      void register_object_method(Class& object, Rt(Class::*callable)(Args...)) {
        functions_.push_back([this, &object, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          return derived()->template process_call<Rt>([&]() -> Rt { 
            return std::apply([&](auto&&... args) { return (object.*callable)(args...); }, args); 
          }, writer, request_id);
        });
      }

      // Overload for const member functions
      template <typename Class, typename Rt, typename... Args>
      void register_object_method(Class& object, Rt(Class::*callable)(Args...) const) {
        functions_.push_back([this, &object, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          return derived()->template process_call<Rt>([&]() -> Rt { 
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
          return derived()->template process_call<Rt>([&]() -> Rt { return std::apply([&](auto&&... args) {
            return (instance.*callable)(args...); }, args); 
          }, writer, request_id);
        });
      }

      // Registers every public instance method of ApiClass, in declaration order.
      // Ignored members and the destructor are skipped.
      template <typename ApiClass>
      void register_object_methods(ApiClass& object) {
        [:serialization::expand(std::meta::members_of(^^ApiClass, std::meta::access_context::unchecked())):] >> [&]<std::meta::info member> {
          if constexpr (std::meta::is_function(member)
                     && !std::meta::is_static_member(member)
                     && std::meta::is_public(member)
                     && std::meta::has_identifier(member)
                     && !is_member_ignored(member)) {
            constexpr std::string_view name = std::meta::identifier_of(member);
            if constexpr (name.find('~') == std::string_view::npos) {
            constexpr auto member_func_ptr = &[:member:];
            if constexpr (is_high_level_observable<member>()) {
              register_high_level_observable_method(object, member_func_ptr);
            } else {
              register_object_method(object, member_func_ptr);
            }
            }
          }
        };
      }

      // Overload for const member functions
      template <typename Class, typename Rt, typename... Args>
      void register_singleton_method(Rt(Class::*callable)(Args...) const) {
        functions_.push_back([this, callable](Rd& reader, Wr& writer, uint64_t request_id) {
          std::tuple<Args...> args;
          deserialize_args<0>(reader, args);
          Class& instance = Class::get_rpc_instance();
          return derived()->template process_call<Rt>([&]() -> Rt { return std::apply([&](auto&&... args) {
            return (instance.*callable)(args...); }, args); 
          }, writer, request_id);
        });
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

      template <typename Tuple>
      static auto drop_first(Tuple& t) {
        using Bare = std::remove_cvref_t<Tuple>;
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
          return std::tuple<std::tuple_element_t<I + 1, Bare>...>(
            std::get<I + 1>(std::move(t))...);
        }(std::make_index_sequence<std::tuple_size_v<Bare> - 1>{});
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
