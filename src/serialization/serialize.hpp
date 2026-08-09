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
    writer.write_map(value,
      [&](auto& w, const typename T::key_type& k) { serialize(w, k); },
      [&](auto& w, const typename T::mapped_type& v) { serialize(w, v); });
    writer.save_offset();
  }

  template <class T, Writer W>
  requires(UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 1) && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_u8(static_cast<std::uint8_t>(value));
  }

  template <class T, Writer W>
  requires(UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 2) && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_u16(static_cast<std::uint16_t>(value));
  }

  template <class T, Writer W>
  requires(UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 4) && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_u32(static_cast<std::uint32_t>(value));
  }

  template <class T, Writer W>
  requires(UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 8) && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_u64(value);
  }

  template <class T, Writer W>
  requires(SignedInteger<T> && !Boolean<T> && !FloatingPoint<T> && (sizeof(T) == 1) && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_i8(static_cast<std::int8_t>(value));
  }

  template <class T, Writer W>
  requires(SignedInteger<T> && !Boolean<T> && !FloatingPoint<T> && (sizeof(T) == 2) && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_i16(static_cast<std::int16_t>(value));
  }

  template <class T, Writer W>
  requires(SignedInteger<T> && !Boolean<T> && !FloatingPoint<T> && (sizeof(T) == 4) && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_i32(static_cast<std::int32_t>(value));
  }

  template <class T, Writer W>
  requires(SignedInteger<T> && !Boolean<T> && !FloatingPoint<T> && (sizeof(T) == 8) && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_i64(value);
  }

  template <class T, Writer W>
  requires(FloatingPoint<T> && !Boolean<T> && (sizeof(T) == 4) && Writer<W>)
  void serialize(W& writer, const T& value) {
    writer.write_f32(static_cast<float>(value));
  }

  template <class T, Writer W>
  requires(FloatingPoint<T> && !Boolean<T> && (sizeof(T) == 8) && Writer<W>)
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
    writer.write_pointer(value, writer.template make_shared_write_later<typename T::element_type>(value));
  }

  template <class T, Writer W>
  requires(SimpleException<T> && Writer<W>)
  void serialize(W& writer, const T& value) {
    std::string msg(value.what());
    serialize(writer, msg);
  }

  template <class T, Writer W, class B>
  requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && !SimpleException<T> && Writer<W>)
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
  requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && !SimpleException<T> && Writer<W>)
  void serialize(W& writer, const T& value) {    
    writer.begin_frame();
    serialize_members<T, W, T>(writer, value);
    writer.end_frame();
    writer.save_offset();
  }

}