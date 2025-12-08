#pragma once

#include <type_traits>
#include <meta>
#include "meta_expand.hpp"

namespace serialization {

  enum class policy {
    ignore,
    include,
  };  

  struct dynamic_annotation {    
  };

  static consteval auto dynamic() {
    return dynamic_annotation{};
  }

  static consteval auto serialization_annotations_of(std::meta::info dm) {
    auto notes = std::meta::annotations_of(dm);
    std::erase_if(notes, [](std::meta::info  ann) {
      return std::meta::parent_of(std::meta::type_of(ann)) != ^^serialization; 
    });
    return notes;
  }
  
  static consteval bool is_member_ignored(auto&& member) {
    auto annotations = std::meta::annotations_of(member);
    auto it = std::find_if(annotations.begin(), annotations.end(), [&](auto&& annotation) {
      return (std::meta::type_of(annotation) == ^^serialization::policy) 
          && (std::meta::extract<serialization::policy>(annotation) == serialization::policy::ignore);
    });
    return it != annotations.end();        
  }

  static consteval bool is_dynamic(auto&& member) {
    auto annotations = std::meta::annotations_of(member);
    auto it = std::find_if(annotations.begin(), annotations.end(), [&](auto&& annotation) {
      return std::meta::type_of(annotation) == ^^serialization::dynamic_annotation;
    });
    return it != annotations.end();
  }

}