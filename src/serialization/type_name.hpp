#pragma once

namespace serialization {


  // Podstawowe typy - muszą być zdefiniowane przed kontenerami
  template <typename T>
  requires(UnsignedInteger<T>)
  constexpr std::string type_name(T v = T()) {
    return "uint";
  }

  template <typename T>
  requires(SignedInteger<T>)
  constexpr std::string type_name(T v = T()) {
    return "sint";
  }

  template <typename T>
  requires(FloatingPoint<T>)
  constexpr std::string type_name(T v = T()) {
    return "float";
  }

  template <typename T>
  requires(Boolean<T>)
  constexpr std::string type_name(T v = T()) {
    return "bool";
  }

  template <typename T>
  requires(Void<T>)
  constexpr std::string type_name(T v = T()) {
    return "void";
  }

  template <typename T>
  requires(String<T>)
  constexpr std::string type_name(T v = T()) {
    return "string";
  }

  template <typename T>
  requires(Enum<T>)
  constexpr std::string type_name(T v = T()) {
    return std::string(std::meta::identifier_of(^^T));
  }

  template <typename T>
  requires(UserDefinedType<T> && !Pointer<T> && !Enum<T> && !ContainerButNotString<T> && !String<T>)
  constexpr std::string type_name(T v = T()) {
    return std::string(std::meta::identifier_of(^^T));
  }

  template <typename T>
  requires(std::is_pointer_v<T>)
  constexpr std::string type_name(T v = T()) {    
    using Inner = std::remove_pointer_t<T>;
    Inner i;
    return type_name<Inner>(i) + "*";
  }

  template <typename T>
  requires(is_unique_ptr<T>::value)
  constexpr std::string type_name(T v = T()) {
    using Inner = typename T::element_type;
    Inner i;
    return type_name<Inner>(i);
  }

  template <typename T>
  requires(is_shared_ptr<T>::value)
  constexpr std::string type_name(T v = T()) {
    using Inner = typename T::element_type;
    Inner i;
    return type_name<Inner>(i) + "*";
  }

  template <typename T>
  requires(is_weak_ptr<T>::value)
  constexpr std::string type_name(T v = T()) {
    using Inner = typename T::element_type;
    Inner i;
    return type_name<Inner>(i) + "*";
  }

  template <typename T>
  requires(ContainerButNotString<T> && !Binary<T>)
  constexpr std::string type_name(T v = T()) {
    return type_name(typename T::value_type()) + "[]";
  }

}