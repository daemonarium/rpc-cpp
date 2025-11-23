#pragma once

#include <type_traits>
#include <meta>
#include "meta_expand.hpp"

namespace serialization {

  enum class policy {
    ignore,
    include,
  };  

  class member_annotation { // virtual class
    public:
      virtual std::string_view name() const = 0;
      virtual ~member_annotation() = default;
      virtual bool should_serialize() const = 0;
  };

  
  consteval bool is_member_ignored(auto&& member) {
    constexpr auto annotations = std::meta::annotations_of(member);
    constexpr auto it = std::find_if(annotations.begin(), annotations.end(), [&](auto&& annotation) {
      if constexpr(std::meta::type_of(annotation) == ^^serialization::policy) {
        return std::meta::constant_of(annotation) == serialization::policy::ignore;
      }
    });
    return it != annotations.end();        
  }



}