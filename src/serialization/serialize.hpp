#pragma once

#include "meta_expand.hpp"
#include "type_concepts.hpp"
#include <memory>
#include <typeindex>
#include "writer.hpp"
#include "annotations.hpp"

namespace serialization {

  template <class T, Writer W>
  requires(Binary<T> && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_binary(std::move(value));
  }

  template <class T, Writer W>
  requires(ContainerButNotString<T> && !Binary<T> && Writer<W>)  
  void serialize(W& writer, const T& value) {
    writer.begin_frame();
    writer.write_length(value.size());    
    for (const auto& element : value) {
      serialize(writer, element);
    }
    writer.end_frame();
    writer.save_offset();
  }

  template <class T, Writer W>
  requires(Map<T> && !Binary<T> && Writer<W>)  
  void serialize(W& writer, const T& value) {
    writer.begin_frame();
    writer.write_length(value.size());    
    for (const auto& element : value) {
      serialize(writer, element.first);
      serialize(writer, element.second);
    }
    writer.end_frame();
    writer.save_offset();
  }

  template <class T, Writer W>
  requires(UnsignedInteger<T> && !Boolean<T> && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_u64(value);
  }

  template <class T, Writer W>
  requires(SignedInteger<T> && !Boolean<T> && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_i64(value);
  }

  template <class T, Writer W>
  requires(FloatingPoint<T> && !Boolean<T> && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_f64(value);
  }

  template <class T, Writer W>
  requires(Boolean<T> && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_bool(value);
  }

  template <class T, Writer W>
  requires(Enum<T> && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_u64(static_cast<std::uint64_t>(value));
  }

  template <class T, Writer W>
  requires(String<T> && Writer<W>)
  void serialize(W& writer, const T& value) {    
    writer.write_string(value);
    writer.save_offset();
  }

  template <class T, Writer W>
  requires(UniquePointer<T> && Writer<W>)
  void serialize(W& writer, const T& value) {
    serialize(writer, *value);
  }

  template <class T, Writer W>
  requires(SharedPointer<T> && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_pointer(value, [value, &writer]{
      writer.write_typed(value);      
    });
  }

  template <class T, Writer W, class B>
  requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && Writer<W>)
  void serialize_members(W& writer, const T& value) {
    [:expand(std::meta::bases_of(^^B, std::meta::access_context::unchecked())):] >> [&]<std::meta::info base> {
      serialize_members<T, W, typename [:std::meta::type_of(base):]>(writer, value);
    };
    [:expand(std::meta::nonstatic_data_members_of(^^B, std::meta::access_context::unchecked())):] >> [&]<auto dm> {
      if constexpr(!is_member_ignored(dm)) {
        serialize(writer, value.[:dm:]);        
      }
    };    
  }

  template <class T, class W>  
  requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && Writer<W>)
  void serialize(W& writer, const T& value) {    
    writer.begin_frame();
    serialize_members<T, W, T>(writer, value);
    writer.end_frame();
    writer.save_offset();
  }

}