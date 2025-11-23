#include <iostream>
#include <string>
#include <meta>
#include "serialization/meta_expand.hpp"


template <typename T>
void printClass(size_t indent = 0) {
  std::cout << std::string(indent, ' ') << "Class: " << std::meta::identifier_of(^^T) << std::endl;
  indent += 2;  
  [:serialization::expand(std::meta::bases_of(^^T, std::meta::access_context::unchecked())):] >> [&]<std::meta::info base> {
    std::cout << "Base class: " << std::meta::identifier_of(base) << std::endl;
    printClass<typename [:std::meta::type_of(base):]>(indent + 2);
  };
  
  [:serialization::expand(std::meta::members_of(^^T, std::meta::access_context::unchecked())):] >> [&]<std::meta::info member> {  

    if constexpr (!std::meta::is_public(member)) {
      return;
    }

    std::cout << std::string(indent+2, ' ');

    if(std::meta::is_const(member)) std::cout << "const ";
    if(std::meta::is_volatile(member)) std::cout << "volatile ";
    if(std::meta::is_static_member(member)) std::cout << "static ";
    if(std::meta::is_constructor(member)) std::cout << "constructor ";
    if(std::meta::is_destructor(member)) std::cout << "destructor ";
    
    if constexpr (std::meta::is_function(member)) {
      if constexpr (!std::meta::is_constructor(member) && !std::meta::is_destructor(member)) {
        if constexpr (std::meta::has_identifier(std::meta::return_type_of(member))) {
          std::cout << std::meta::identifier_of(std::meta::return_type_of(member)) << " ";
        } else {
          std::cout << std::meta::display_string_of(std::meta::return_type_of(member)) << " ";
        }
      }
    }

    if constexpr (std::meta::is_static_member(member)) {
      std::cout << std::meta::identifier_of(std::meta::type_of(member)) << " ";
    }
    if constexpr (std::meta::is_nonstatic_data_member(member)) {
      if constexpr (std::meta::has_identifier(std::meta::type_of(member))) {
        std::cout << std::meta::identifier_of(std::meta::type_of(member)) << " ";
      } else {
        std::cout << std::meta::display_string_of(std::meta::type_of(member)) << " ";
      }
    }   

    if constexpr (std::meta::has_identifier(member)) {
      std::cout << std::meta::identifier_of(member) << " ";
    } else if(!std::meta::is_constructor(member) && !std::meta::is_destructor(member)) {
      std::cout << "unknown ";
    }  

    if constexpr (std::meta::is_function(member)) {

      std::cout << "(";
      [:serialization::expand(std::meta::parameters_of(member)):] >> [&]<auto param> {
        if constexpr (std::meta::has_identifier(param)) {
          if constexpr (std::meta::has_identifier(std::meta::type_of(param))) {
            std::cout << std::meta::identifier_of(std::meta::type_of(param)) << " ";            
          } else {
            std::cout << std::meta::display_string_of(std::meta::type_of(param)) << " ";
          } 
          std::cout << std::meta::identifier_of(param) << "";
          std::cout << " , ";
        } else {
          std::cout << "unknown-parameter ";
        }
      };
      std::cout << ") ";
    }

    std::cout << std::endl;
  };
  indent -= 2;
}


template <typename T, typename R, typename... Args>
R callMethodByName(T& obj, const std::string& methodName, Args... args) {
  R result{};
  bool found = false;
  [:serialization::expand(std::meta::members_of(^^T, std::meta::access_context::unchecked())):] >> [&]<std::meta::info member> {
    if constexpr (std::meta::is_function(member)) {
      if constexpr (std::meta::has_identifier(member)) {
        if (std::meta::identifier_of(member) == methodName && !found) {
          if constexpr (requires { obj.[:member:](args...); }) {
            result = obj.[:member:](args...);
            found = true;
          }
          /* if constexpr (requires { std::invoke([:member:], obj, args...); }) {
            std::cout << "Invoking method" << std::endl;
            result = std::invoke([:member:], obj, args...);
            found = true;
          } */
        }
      }
    }
  };
  return result;
}