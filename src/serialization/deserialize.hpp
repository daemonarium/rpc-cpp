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
  }

  template <Map T, Reader R>
  requires(Reader<R> && Map<T>)
  void deserialize(R& reader, T& value) {
    reader.enter_frame();
    std::size_t length;
    reader.read_length(length);
    for (std::size_t i = 0; i < length; ++i) {
      typename T::key_type key;
      typename T::mapped_type item_value;
      deserialize(reader, key);
      deserialize(reader, item_value);
      value.insert(std::make_pair(key, item_value));
    }
    reader.exit_frame();
  }

  template <UnsignedInteger T, Reader R>
  requires(Reader<R> && UnsignedInteger<T>)
  void deserialize(R& reader, T& value) {
    std::uint64_t u64;
    reader.read_u64(u64);
    value = u64;
  }

  template <SignedInteger T, Reader R>
  requires(Reader<R> && SignedInteger<T>)
  void deserialize(R& reader, T& value) {
    std::int64_t i64;
    reader.read_i64(i64);
    value = i64;
  }

  template <FloatingPoint T, Reader R>
  requires(Reader<R> && FloatingPoint<T>)
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
  }

  template <UniquePointer T, Reader R>
  requires(Reader<R> && UniquePointer<T>)
  void deserialize(R& reader, T& value) {
    deserialize(reader, *value);
  }

  template <SharedPointer T, Reader R>
  requires(Reader<R> && SharedPointer<T>)
  void deserialize(R& reader, T& value) {
    reader.read_pointer(value, [&reader](std::shared_ptr<void>& data) {      
      reader.read_typed(data);
    });
  }

  template <class T, Reader R, class B>
  requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && Reader<R>)
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

  template <class T, Reader R>
  requires(UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && Reader<R>)  
  void deserialize(R& reader, T& value) {
    reader.enter_frame();
    deserialize_members<T, R, T>(reader, value);
    reader.exit_frame();
  }

}