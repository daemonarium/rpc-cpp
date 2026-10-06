#pragma once

#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <sstream>
#include <functional>
#include <typeindex>

#include "meta"
#include "meta_expand.hpp"
#include "type_concepts.hpp"
#include "serialize.hpp"
#include "deserialize.hpp"

#include "annotations.hpp"
#include "type_name.hpp"

#include "promise.hpp"
#include "examples/structures/stream_protocol.hpp"

namespace serialization {

  class type_description;

  template<typename R>
  concept TypeRegistry = requires(R r) {
    { r.register_type(std::shared_ptr<type_description>(nullptr)) } -> std::same_as<size_t>;
    { r.get_type(std::type_index(typeid(int))) } -> std::same_as<std::shared_ptr<type_description>>;
  };
  
  class [[=serialization::dynamic()]] type_description {
    public:

      enum class Kind {
        Void = 0,
        Boolean = 1,
        UnsignedInteger8 = 2,
        SignedInteger8 = 3,
        UnsignedInteger16 = 4,
        SignedInteger16 = 5,
        UnsignedInteger32 = 6,
        SignedInteger32 = 7,
        UnsignedInteger64 = 8,
        SignedInteger64 = 9,
        FloatingPoint32 = 10,
        FloatingPoint64 = 11,
        Enum = 12,
        Array = 13,
        String = 14,
        Binary = 15,
        Map = 16,
        Object = 17,
        DynamicObject = 18,
        Message = 19,
        Exception = 20,
        Pointer = 21,
        Function = 22,
        Method = 23
      };

      struct member {
        std::string name;
        std::shared_ptr<type_description> type;
        std::vector<member_annotation_ptr> annotations;

        member() : name(""), type(nullptr), annotations({}) {}
        member(std::string name, std::shared_ptr<type_description> type, std::vector<member_annotation_ptr> annotations)
          : name(std::move(name)), type(std::move(type)), annotations(std::move(annotations)) {
        }

        bool operator==(const member& other) const {
          return name == other.name && type == other.type && annotations == other.annotations;
        }
      };      
      using enumMember = std::pair<std::string, std::uint64_t>;

      type_description(type_description&& other) noexcept 
        : name(std::move(other.name)), hash_(std::move(other.hash_)), 
          kind(std::move(other.kind)), members(std::move(other.members)), 
          item(std::move(other.item)), enum_members(std::move(other.enum_members)),
          type_index_(std::move(other.type_index_)) {
      }

      type_description() : name(""), kind(static_cast<std::uint8_t>(Kind::Void)), type_index_(std::type_index(typeid(void))) {      
      }
    
      type_description(const std::string& namep, Kind kindp) 
        : name(namep), kind(static_cast<std::uint8_t>(kindp)), type_index_(std::type_index(typeid(void))) {
        compute_hash();
      }

      type_description(std::string&& namep, Kind kindp) 
        : name(std::move(namep)), kind(static_cast<std::uint8_t>(kindp)), type_index_(std::type_index(typeid(void))) {
        compute_hash();
      }

      type_description(std::string&& namep, Kind kindp, std::vector<member>&& membersp) 
          : name(std::move(namep)), kind(static_cast<std::uint8_t>(kindp)), members(std::move(membersp)), type_index_(std::type_index(typeid(void))) {
        compute_hash();
      }

      type_description(std::string&& namep, Kind kindp, std::shared_ptr<type_description>&& itemp) 
        : name(std::move(namep)), kind(static_cast<std::uint8_t>(kindp)),
          item(std::move(itemp)), type_index_(std::type_index(typeid(void))) {
        compute_hash();
      }

      type_description(std::string&& namep, Kind kindp, std::vector<enumMember>&& enum_membersp) 
        : name(std::move(namep)), kind(static_cast<std::uint8_t>(kindp)),
          enum_members(std::move(enum_membersp)), type_index_(std::type_index(typeid(void))) {
        compute_hash();
      }
    
      const std::string_view getName() const { return name; }

      const Kind getKind() const { return static_cast<Kind>(kind); }

      const std::vector<member> getMembers() const {
        if(static_cast<Kind>(kind) == Kind::Object) {
          return members;
        }
        return {};
      }

      const std::shared_ptr<type_description> getItem() const {
        if(static_cast<Kind>(kind) == Kind::Pointer) {
          return item;
        }
        if(static_cast<Kind>(kind) == Kind::Array) {
          if(item) {
            return item;
          }
          if(!members.empty()) {
            return members[0].type;
          }
        }
        return nullptr;
      }

      const std::shared_ptr<type_description> getMember(const std::string_view name) const {
        for(const auto& member : members) {
          if(member.name == name) {
            return member.type;
          }
        }
        return nullptr;
      }

      void compute_hash() {        
        std::uint32_t hash = 0x9747b28c;        
        hash = mix_hash(hash, kind);
        for (const auto& member : members) {
          for(auto& c : member.name) {
            hash = mix_hash(hash, c);
          }
          hash = mix_hash(hash, member.type->compute_hash_if_needed());
        }
        if(item) {
          hash = mix_hash(hash, item->compute_hash_if_needed());
        }
        if(enum_members.size() > 0) {
          hash = mix_hash(hash, enum_members.size());
        }
        for(const auto& member : enum_members) {
          for(auto& c : member.first) {
            hash = mix_hash(hash, c);
          }
          hash = mix_hash(hash, member.second);
        }
        hash_ = hash;
      }

      const size_t hash() const { 
        if(!hash_.has_value())  throw std::runtime_error("Hash not computed");
        return hash_.value();
      }

      const size_t compute_hash_if_needed() {
        if(!hash_.has_value()) compute_hash();
        return hash_.value();
      }

      bool operator==(const type_description& other) const {
        if(hash_.has_value() && other.hash_.has_value()) {
          if(hash_.value() != other.hash_.value()) return false;
        }
        return name == other.name && kind == other.kind && members == other.members;
      } 

      friend std::ostream& operator<<(std::ostream& os, type_description const & tc) {
        os << "type '" << tc.name << "' = ";        
        [:expand(std::meta::enumerators_of(^^type_description::Kind)):] >> [&]<auto e> {
          if(tc.kind == static_cast<std::uint8_t>([:e:])) {
            os << std::meta::identifier_of(e);
          }
        };
        if(tc.annotations.size() > 0) os << std::endl << " with annotations:" << std::endl;
        for(const auto& annotation : tc.annotations) {
          os << "  " << annotation->to_string() << std::endl;
        }
        if(tc.kind == static_cast<std::uint8_t>(type_description::Kind::Object) 
           || tc.kind == static_cast<std::uint8_t>(type_description::Kind::DynamicObject)) {
          if(tc.bases.size() > 0) os << std::endl<< " with bases:" << std::endl;
          for(const auto& base : tc.bases) {
            os << "  " << base->getName() << std::endl;
          }
          if(tc.members.size() > 0) os << std::endl<< " with members:" << std::endl;
          for(const auto& member : tc.members) {
            os << "  " << member.name << " of type " << member.type->getName();
            if(member.annotations.size() > 0) os << " with annotations:" << std::endl;
            for(const auto& annotation : member.annotations) {
              os << "    " << annotation->to_string() << std::endl;
            }
            os << std::endl;
          }
        }
        if(tc.kind == static_cast<std::uint8_t>(type_description::Kind::Array)) {
          os << std::endl << " with item type " << tc.item->getName();
        }
        if(tc.kind == static_cast<std::uint8_t>(type_description::Kind::Enum)) {
          if(tc.enum_members.size() > 0) os << std::endl << " with enum members:" << std::endl;
          for(const auto& member : tc.enum_members) {
            os << "  " << member.first << " = " << member.second << std::endl;
          }
        }
        if(tc.kind == static_cast<std::uint8_t>(type_description::Kind::Function)) {
          if(tc.members.size() > 0) os << std::endl << " with parameters:" << std::endl;
          for(const auto& member : tc.members) {
            os << "  " << member.name << " of type " << member.type->getName() << std::endl;
          }
          os << " with return type " << tc.item->getName() << std::endl;
        }
        if(tc.kind == static_cast<std::uint8_t>(type_description::Kind::Method)) {
          if(tc.members.size() > 0) os << std::endl << " with parameters:" << std::endl;
          for(const auto& member : tc.members) {
            os << "  " << member.name << " of type " << member.type->getName() << std::endl;
          }
          os << " with return type " << tc.item->getName() << std::endl;
        }
        return os;
      }

    private:
      std::string name; 
      std::uint8_t kind;
      std::vector<member> members;
      std::shared_ptr<type_description> item;
      std::shared_ptr<type_description> key;
      std::vector<enumMember> enum_members;
      std::vector<std::shared_ptr<type_description>> bases;
      std::vector<type_annotation_ptr> annotations;


      friend class type_registry;
      [[=serialization::policy::ignore]]
      std::optional<std::uint32_t> hash_;
      [[=serialization::policy::ignore]]
      std::type_index type_index_;

      inline uint32_t mix_hash(uint32_t hash, uint32_t value) {
        uint32_t k = value;
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
      
        hash ^= k;
        hash = (hash << 13) | (hash >> 19);
        hash = hash * 5 + 0xe6546b64;
      
        return hash;
      }

    public:

    template <typename T, TypeRegistry R>
    requires(ContainerButNotString<T> && !Binary<T>)
    std::shared_ptr<type_description> static create(T value, R& registry) {      
      std::string_view name;
      if constexpr (std::meta::has_identifier(^^T)) {
        name = std::meta::identifier_of(^^T);
      } else {
        name = std::meta::display_string_of(^^T);
      }
      std::shared_ptr<type_description> type = registry.get_type(name);
      if(type) return type;
      type = std::make_shared<type_description>(
        std::string(name), 
        type_description::Kind::Array, 
        nullptr
      );
      type->type_index_ = std::type_index(typeid(T));
      registry.register_type(type);
      type->item = create(typename T::value_type(), registry);
      return type;
    }

    template <typename T, TypeRegistry R>
    requires(Map<T>)
    std::shared_ptr<type_description> static create(T value, R& registry) {
      std::string_view name;
      if constexpr (std::meta::has_identifier(^^T)) {
        name = std::meta::identifier_of(^^T);
      } else {
        name = std::meta::display_string_of(^^T);
      }     
      std::shared_ptr<type_description> type = registry.get_type(name);
      if(type) return type;
      type = std::make_shared<type_description>(std::string(name), type_description::Kind::Map);
      type->type_index_ = std::type_index(typeid(T));
      registry.register_type(type);
      type->key = create(typename T::key_type(), registry);
      type->item = create(typename T::mapped_type(), registry);      
      return type;
    }
  
    template <typename T, TypeRegistry R>
    requires(UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 1))
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(2);
    }
    template <typename T, TypeRegistry R>
    requires(UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 2))
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(4);
    }
    template <typename T, TypeRegistry R>
    requires(UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 4))
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(6);
    }
    template <typename T, TypeRegistry R>
    requires(UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 8))
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(8);
    }

    template <typename T, TypeRegistry R>
    requires(SignedInteger<T> && !Boolean<T> && (sizeof(T) == 1))
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(3);
    }
    template <typename T, TypeRegistry R>
    requires(SignedInteger<T> && !Boolean<T> && (sizeof(T) == 2))
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(5);
    }
    template <typename T, TypeRegistry R>
    requires(SignedInteger<T> && !Boolean<T> && (sizeof(T) == 4))
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(7);
    }
    template <typename T, TypeRegistry R>
    requires(SignedInteger<T> && !Boolean<T> && (sizeof(T) == 8))
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(9);
    }

    template <typename T, TypeRegistry R>
    requires(FloatingPoint<T> && !Boolean<T> && (sizeof(T) == 4))
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(10);
    }
    template <typename T, TypeRegistry R>
    requires(FloatingPoint<T> && !Boolean<T> && (sizeof(T) == 8))
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(11);
    }

    template <typename T, TypeRegistry R>
    requires(Boolean<T>)
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(1);
    }

    template <typename T, TypeRegistry R>
    requires(Void<T>)
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(0);
    }

    template <typename T, TypeRegistry R>
    requires(String<T>)
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(12);
    }

    template <typename T, TypeRegistry R>
    requires(Binary<T>)
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return registry.get_type(13);
    }
  
    template <typename T, TypeRegistry R>
    requires(Enum<T>)
    std::shared_ptr<type_description> static create(T value, R& registry) {
      std::string_view name;
      if constexpr (std::meta::has_identifier(^^T)) {
        name = std::meta::identifier_of(^^T);
      } else {
        name = std::meta::display_string_of(^^T);
      }     
      std::shared_ptr<type_description> type = registry.get_type(name);
      if(type) return type;
      //std::vector<type_description::enumMember> enum_members;
      type = std::make_shared<type_description>(std::string(name), type_description::Kind::Enum);
      type->type_index_ = std::type_index(typeid(T));
      registry.register_type(type);
      [:expand(std::meta::enumerators_of(^^T)):] >> [&]<auto e> {
        type->enum_members.push_back(std::make_pair(std::string(std::meta::identifier_of(e)), static_cast<std::uint64_t>([:e:])));
      };
      return type;
    }  
    
    template <typename T, TypeRegistry R>
    requires(UniquePointer<T> && !Polymorphic<T>)
    std::shared_ptr<type_description> static create(T value, R& registry) {
      return create(*value, registry);    
    }
  
    template <typename T, TypeRegistry R>
    requires(SharedPointer<T>)
    std::shared_ptr<type_description> static create(T value, R& registry) {
      using Pointee = std::remove_cv_t<typename T::element_type>;
      auto pointeeType = create(Pointee{}, registry);
      std::string name = std::string(pointeeType->getName()) + "*";
      auto existing = registry.get_type(name);
      if(existing) return existing;
      auto ptrType = std::make_shared<type_description>(
        std::move(name),
        type_description::Kind::Pointer,
        std::shared_ptr<type_description>(pointeeType)
      );
      ptrType->type_index_ = std::type_index(typeid(T));
      registry.register_type(ptrType);
      return ptrType;
    }

    template <std::meta::info member, TypeRegistry R>
    static void register_parameters_and_return(R& registry, std::shared_ptr<type_description> method_type) {
      if constexpr (is_high_level_observable<member>()) {
        // Wire shape matches low-level: stream_handle first, void return.
        waos::stream_handle handle{};
        method_type->members.push_back(type_description::member(
          "stream", create(handle, registry), {}));
      }
      [:serialization::expand(std::meta::parameters_of(member)):] >> [&]<auto param> {
        std::string_view name = std::meta::identifier_of(param);
        auto param_value = typename [:std::meta::type_of(param):]();
        std::shared_ptr<type_description> param_type = create(param_value, registry);
        method_type->members.push_back(type_description::member(std::string(name), param_type, {}));
      };
      if constexpr (is_high_level_observable<member>()) {
        method_type->item = registry.get_type(0);  // void on the wire
        return;
      }
      constexpr std::meta::info return_type = std::meta::return_type_of(member);
      // Use display_string to check if it's a reference or void type
      constexpr std::string_view return_type_str = std::meta::display_string_of(return_type);
      if constexpr (return_type_str.find('&') == std::string_view::npos && return_type_str != "void"
                    && return_type_str != "shared_ptr<promise<void>>") {
        // Not a reference and not void - safe to create object
        auto return_value = typename [:return_type:]();
        auto depromisified_value = depromisify(return_value);
        method_type->item = create(depromisified_value, registry);
      } else if constexpr (return_type_str.find('&') != std::string_view::npos) {
        // Is a reference - remove reference and create object
        constexpr std::meta::info referenced_type = std::meta::remove_reference(return_type);
        auto return_value = typename [:referenced_type:]();
        auto depromisified_value = depromisify(return_value);
        method_type->item = create(depromisified_value, registry);
      } else {
        method_type->item = registry.get_type(0);  // void
      }
    }    
  
    template <typename T, TypeRegistry R>
    requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && !SimpleException<T>)
    std::shared_ptr<type_description> static create(T value, R& registry) {
      std::string_view name;
      if constexpr (std::meta::has_identifier(^^T)) {
        name = std::meta::identifier_of(^^T);
      } else {
        name = std::meta::display_string_of(^^T);
      }      
      std::shared_ptr<type_description> type = registry.get_type(name);
      if(type) return type;
      type = std::make_shared<type_description>(std::string(name), 
        is_message(^^T) 
          ? type_description::Kind::Message 
          : (is_dynamic(^^T) 
              ? type_description::Kind::DynamicObject 
              : (is_exception<T>()
                  ? type_description::Kind::Exception 
                  : type_description::Kind::Object)
            )
      );
      type->type_index_ = std::type_index(typeid(T));
      registry.register_type(type);
      type->annotations = annotations_of<^^T, type_annotation_ptr>();
      //describe_members<T, T, R>(type, value, registry); 

      [:expand(std::meta::bases_of(^^T, std::meta::access_context::unchecked())):] >> [&]<std::meta::info base> {
        type->bases.push_back(create(typename [:std::meta::type_of(base):](), registry));
      };

      [:expand(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())):] >> [&]<auto dm> {        
        if constexpr(!is_member_ignored(dm)) {  
          std::string_view memberName = std::meta::identifier_of(dm);
          type_description::member m = type_description::member(
            std::string(memberName),
            create(value.[:dm:], registry),
            annotations_of<dm, member_annotation_ptr>()
          );
          type->members.push_back(m);    
        }
      };

      [:serialization::expand(std::meta::members_of(^^T, std::meta::access_context::unchecked())):] >> [&]<std::meta::info member> {  
        if constexpr (std::meta::is_function(member) 
                    && std::meta::is_static_member(member) 
                    && std::meta::has_identifier(member) 
                    && std::meta::is_public(member)
                    && !is_member_ignored(member)) {
          std::string_view member_name = std::meta::identifier_of(member);
          std::string full_name = std::string(name) + "." + std::string(member_name);
  
          std::shared_ptr<type_description> method_type = 
            std::make_shared<type_description>(full_name, type_description::Kind::Function);
          registry.register_type(method_type);
          register_parameters_and_return<member>(registry, method_type);
          
          auto annotations = annotations_of<member, member_annotation_ptr>();
          type->members.push_back(type_description::member(full_name, method_type, annotations ));
        }
      };

      constexpr bool non_static_rpc_callable = []() constexpr -> bool {
        bool found = false;
        [:serialization::expand(std::meta::members_of(^^T, std::meta::access_context::unchecked())):] >> [&]<std::meta::info member> {  
          if constexpr (std::meta::is_function(member) 
                     && std::meta::is_static_member(member) 
                     && std::meta::has_identifier(member) 
                     && std::meta::is_public(member)) {
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
        [:serialization::expand(std::meta::members_of(^^T, std::meta::access_context::unchecked())):] >> [&]<std::meta::info member> {  
          if constexpr (std::meta::is_function(member)
                     && std::meta::is_static_member(member) 
                     && std::meta::has_identifier(member) 
                     && std::meta::is_public(member)) {
            constexpr std::string_view name = std::meta::identifier_of(member);
            if constexpr (name == "get_rpc_instance") {
              found = true;
            }
          }
        };
        return found;
      }();

      if constexpr (non_static_rpc_callable && singleton_rpc_callable) {
        throw std::runtime_error("Class " + std::string(name) + " has both non-static and singleton rpc callables");
      }

      if constexpr (non_static_rpc_callable || singleton_rpc_callable) {
        [:serialization::expand(std::meta::members_of(^^T, std::meta::access_context::unchecked())):] >> [&]<std::meta::info member> {  
          if constexpr (std::meta::is_function(member)
                     && !std::meta::is_static_member(member)
                     && std::meta::has_identifier(member)
                     && std::meta::is_public(member)
                     && !is_member_ignored(member)) {
            std::string_view member_name = std::meta::identifier_of(member);
            std::string full_name = std::string(name) + "." + std::string(member_name);
  
            std::shared_ptr<type_description> method_type = 
              std::make_shared<type_description>(full_name, non_static_rpc_callable ? type_description::Kind::Method : type_description::Kind::Function);
            registry.register_type(method_type);
            register_parameters_and_return<member>(registry, method_type);
            type->members.push_back(type_description::member(full_name, method_type, annotations_of<member, member_annotation_ptr>()));
          }
        };
      }
  
      return type;
    }

    template <typename T, TypeRegistry R>
    requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && SimpleException<T>)
    std::shared_ptr<type_description> static create(T value, R& registry) {
      std::string_view name;
      if constexpr (std::meta::has_identifier(^^T)) {
        name = std::meta::identifier_of(^^T);
      } else {
        name = std::meta::display_string_of(^^T);
      }      
      std::shared_ptr<type_description> type = registry.get_type(name);
      if(type) return type;
      type = std::make_shared<type_description>(std::string(name), type_description::Kind::Exception);
      type->type_index_ = std::type_index(typeid(T));
      registry.register_type(type);
      std::vector<member_annotation_ptr> annotations;
      type_description::member m = type_description::member(
        std::string("what"),
        create(std::string("what"), registry),
        annotations
      );
      type->members.push_back(m); 
      return type;
    }
      
  };

  
}