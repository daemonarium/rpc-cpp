#pragma once

#include <type_traits>
#include <meta>
#include "meta_expand.hpp"
#include <memory>

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

  static consteval bool is_dynamic(auto&& member) {
    auto annotations = std::meta::annotations_of(member);
    auto it = std::find_if(annotations.begin(), annotations.end(), [&](auto&& annotation) {
      return std::meta::type_of(annotation) == ^^serialization::dynamic_annotation;
    });
    return it != annotations.end();
  }

  struct message_annotation {    
  };

  static consteval auto message() {
    return message_annotation{};
  }

  static consteval bool is_message(auto&& member) {
    auto annotations = std::meta::annotations_of(member);
    auto it = std::find_if(annotations.begin(), annotations.end(), [&](auto&& annotation) {
      return std::meta::type_of(annotation) == ^^serialization::message_annotation;
    });
    return it != annotations.end();
  }
  
  class [[=serialization::dynamic()]] type_annotation {
    public:
      virtual ~type_annotation() {}
      virtual std::string to_string() const { return ""; }
  };

  using type_annotation_ptr = std::shared_ptr<type_annotation>;

  class [[=serialization::dynamic()]] member_annotation {
    public:
      virtual ~member_annotation() {}
      virtual std::string to_string() const { return ""; }
  };

  using member_annotation_ptr = std::shared_ptr<member_annotation>;

  class [[=serialization::dynamic()]] read_annotation : public member_annotation {
    public:
      virtual ~read_annotation() {}
      virtual std::string to_string() const override { return "[[=read]"; }
  };

  using read_annotation_ptr = std::shared_ptr<read_annotation>;


  namespace member_annotations {
    struct read {
      static auto annotation_ptr() {
        return std::make_shared<read_annotation>();
      }
    };
  }

  static consteval auto read() {
    return member_annotations::read{};
  }

  template <std::meta::info dm>
  std::vector<member_annotation_ptr> member_annotations_of() {  
    /// convert annotationInfos to member_annotation_ptrs
    std::vector<member_annotation_ptr> annotation_ptrs;
    [:expand(std::meta::annotations_of(dm)):] >> [&]<auto annotationInfo> {      
      constexpr auto annotationType = std::meta::type_of(annotationInfo);
      if constexpr (std::meta::parent_of(annotationType) == ^^serialization::member_annotations) { 
        if constexpr (std::meta::type_of(annotationInfo) == ^^serialization::member_annotations::read) {
          annotation_ptrs.push_back(std::make_shared<read_annotation>());
        }
        /// TODO: create a generic function to convert annotationInfos to member_annotation_ptrs, or some hack for annotations
        //const auto annotationStruct = std::meta::extract(annotationInfo);
        //annotations.push_back(annotationStruct.annotation_ptr());            
      }
    };
    return annotation_ptrs;    
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


}