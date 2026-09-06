#pragma once

#include <algorithm>
#include <concepts>
#include <memory>
#include <meta>
#include <string>
#include <type_traits>
#include <vector>
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

  struct exception_annotation {    
  };

  static consteval auto exception() {
    return exception_annotation{};
  }

  template <typename T>
  requires(std::is_class_v<T> && !(std::derived_from<T, std::exception> || std::derived_from<T, std::runtime_error>))
  static consteval bool is_exception() {
    auto annotations = std::meta::annotations_of(^^T);
    auto it = std::find_if(annotations.begin(), annotations.end(), [&](auto&& annotation) {
      return std::meta::type_of(annotation) == ^^serialization::exception_annotation;
    });
    return it != annotations.end();
  }

  template <typename T>
  requires(std::is_class_v<T> && (std::derived_from<T, std::exception> || std::derived_from<T, std::runtime_error>))
  static consteval bool is_exception() {
    return true;
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
      static member_annotation_ptr annotation_ptr() {
        return std::make_shared<read_annotation>();
      }
    };
  }

  static consteval auto read() {
    return member_annotations::read{};
  }

  template <std::meta::info entity, typename Ptr>
  std::vector<Ptr> annotations_of() {
    std::vector<Ptr> out;
    [:expand(std::meta::annotations_of(entity)):] >> [&]<auto annotationInfo> {
      using Tag = typename [:std::meta::type_of(annotationInfo):];
      if constexpr (requires(Tag t) {
        { t.annotation_ptr() } -> std::convertible_to<Ptr>;
      }) {
        Tag tag = std::meta::extract<Tag>(annotationInfo);
        out.push_back(tag.annotation_ptr());
      }
    };
    return out;
  }

  template <std::meta::info dm>
  std::vector<member_annotation_ptr> member_annotations_of() {
    return annotations_of<dm, member_annotation_ptr>();
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