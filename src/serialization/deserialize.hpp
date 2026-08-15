#pragma once

#include "type_concepts.hpp"
#include "meta_expand.hpp"
#include "reader.hpp"
#include "annotations.hpp"

namespace serialization {

  template <Binary T, Reader R>
  requires(Binary<T> && Reader<R>)
  void deserialize(R& reader, T& value) {
    reader.read_binary(value);
    reader.save_offset();
  }

  template <ContainerButNotString T, Reader R>
  requires(ContainerButNotString<T> && !Binary<T> && Reader<R>)  
  void deserialize(R& reader, T& value) {
    reader.enter_frame();
    std::size_t length;
    reader.read_length(length);
    value.resize(length);
    for (std::size_t i = 0; i < length; ++i) {
      deserialize(reader, value[i]);
    }
    reader.exit_frame();
    reader.save_offset();
  }

  template <Map T, Reader R>
  requires(Reader<R> && Map<T>)
  void deserialize(R& reader, T& value) {
    reader.read_map(value,
      [&](auto& r, typename T::key_type& k) { deserialize(r, k); },
      [&](auto& r, typename T::mapped_type& v) { deserialize(r, v); });
    reader.save_offset();
  }

  template <UnsignedInteger T, Reader R>
  requires(Reader<R> && UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 1))
  void deserialize(R& reader, T& value) {
    std::uint8_t u8;
    reader.read_u8(u8);
    value = static_cast<T>(u8);
  }

  template <UnsignedInteger T, Reader R>
  requires(Reader<R> && UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 2))
  void deserialize(R& reader, T& value) {
    std::uint16_t u16;
    reader.read_u16(u16);
    value = static_cast<T>(u16);
  }

  template <UnsignedInteger T, Reader R>
  requires(Reader<R> && UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 4))
  void deserialize(R& reader, T& value) {
    std::uint32_t u32;
    reader.read_u32(u32);
    value = static_cast<T>(u32);
  }

  template <UnsignedInteger T, Reader R>
  requires(Reader<R> && UnsignedInteger<T> && !Boolean<T> && (sizeof(T) == 8))
  void deserialize(R& reader, T& value) {
    std::uint64_t u64;
    reader.read_u64(u64);
    value = static_cast<T>(u64);
  }

  template <SignedInteger T, Reader R>
  requires(Reader<R> && SignedInteger<T> && !Boolean<T> && !FloatingPoint<T> && (sizeof(T) == 1))
  void deserialize(R& reader, T& value) {
    std::int8_t i8;
    reader.read_i8(i8);
    value = static_cast<T>(i8);
  }

  template <SignedInteger T, Reader R>
  requires(Reader<R> && SignedInteger<T> && !Boolean<T> && !FloatingPoint<T> && (sizeof(T) == 2))
  void deserialize(R& reader, T& value) {
    std::int16_t i16;
    reader.read_i16(i16);
    value = static_cast<T>(i16);
  }

  template <SignedInteger T, Reader R>
  requires(Reader<R> && SignedInteger<T> && !Boolean<T> && !FloatingPoint<T> && (sizeof(T) == 4))
  void deserialize(R& reader, T& value) {
    std::int32_t i32;
    reader.read_i32(i32);
    value = static_cast<T>(i32);
  }

  template <SignedInteger T, Reader R>
  requires(Reader<R> && SignedInteger<T> && !Boolean<T> && !FloatingPoint<T> && (sizeof(T) == 8))
  void deserialize(R& reader, T& value) {
    std::int64_t i64;
    reader.read_i64(i64);
    value = static_cast<T>(i64);
  }

  template <FloatingPoint T, Reader R>
  requires(Reader<R> && FloatingPoint<T> && !Boolean<T> && (sizeof(T) == 4))
  void deserialize(R& reader, T& value) {
    float f32;
    reader.read_f32(f32);
    value = static_cast<T>(f32);
  }

  template <FloatingPoint T, Reader R>
  requires(Reader<R> && FloatingPoint<T> && !Boolean<T> && (sizeof(T) == 8))
  void deserialize(R& reader, T& value) {
    reader.read_f64(value);
  }

  template <Boolean T, Reader R>
  requires(Reader<R> && Boolean<T>)
  void deserialize(R& reader, T& value) {
    reader.read_bool(value);
  }

  template <Enum T, Reader R>
  requires(Reader<R> && Enum<T>)
  void deserialize(R& reader, T& value) {
    std::uint64_t u64;
    reader.read_u64(u64);
    value = static_cast<T>(u64);
  }

  template <String T, Reader R>
  requires(Reader<R> && String<T>)
  void deserialize(R& reader, T& value) {
    reader.read_string(value);
    reader.save_offset();
  }

  template <UniquePointer T, Reader R>
  requires(Reader<R> && UniquePointer<T>)
  void deserialize(R& reader, T& value) {
    deserialize(reader, *value);
  }

  template <SharedPointer T, Reader R>
  requires(Reader<R> && SharedPointer<T>)
  void deserialize(R& reader, T& value) {
    reader.read_pointer(value, reader.template make_shared_read_later<typename T::element_type>());
  }

  template <class T, Reader R, class B>
  requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && !SimpleException<T> && Reader<R>)
  void deserialize_members(R& reader, T& value) {
    [:expand(std::meta::bases_of(^^B, std::meta::access_context::unchecked())):] >> [&]<std::meta::info base> {      
      deserialize_members<T, R, typename [:std::meta::type_of(base):]>(reader, value);
    };
    [:expand(std::meta::nonstatic_data_members_of(^^B, std::meta::access_context::unchecked())):] >> [&]<auto dm> {           
      if constexpr(!is_member_ignored(dm)) {
        deserialize(reader, value.[:dm:]);        
      }
    };    
  }

  template <SimpleException T, Reader R>
  requires(SimpleException<T> && Reader<R>)
  void deserialize(R& reader, T& value) {
    reader.enter_frame();
    std::string msg;
    deserialize(reader, msg);
    reader.exit_frame();
    reader.save_offset();
    value = T(msg);
  }

  template <class T, Reader R>
  requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && !SimpleException<T> && Reader<R>)  
  void deserialize(R& reader, T& value) {
    reader.enter_frame();
    deserialize_members<T, R, T>(reader, value);
    reader.exit_frame();
    reader.save_offset();
  }

}