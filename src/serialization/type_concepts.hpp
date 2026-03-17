#pragma once

#include <concepts>
#include <string>
#include <string_view>
#include <vector>
#include <type_traits>
#include <memory>
#include <exception>

namespace serialization {
  // Helper type traits to detect smart pointer specializations
  template <typename T>
  struct is_shared_ptr : std::false_type {};
  
  template <typename T>
  struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};
  
  template <typename T>
  struct is_unique_ptr : std::false_type {};
  
  template <typename T>
  struct is_unique_ptr<std::unique_ptr<T>> : std::true_type {};
  
  template <typename T>
  struct is_weak_ptr : std::false_type {};
  
  template <typename T>
  struct is_weak_ptr<std::weak_ptr<T>> : std::true_type {};

  // Helper type traits to detect map-like containers
  template <typename T>
  struct has_key_type_and_mapped_type {
    private:
      template <typename U>
      static auto test(int) -> decltype(
        std::declval<typename U::key_type>(),
        std::declval<typename U::mapped_type>(),
        std::true_type{}
      );
      
      template <typename>
      static std::false_type test(...);
      
    public:
      static constexpr bool value = decltype(test<T>(0))::value;
  };

  // Helper type trait to detect vector-like containers (has value_type but no key_type/mapped_type)
  template <typename T>
  struct is_vector_like {
    private:
      template <typename U>
      static auto test(int) -> decltype(
        std::declval<typename U::value_type>(),
        std::true_type{}
      );
      
      template <typename>
      static std::false_type test(...);
      
    public:
      static constexpr bool value = decltype(test<T>(0))::value && !has_key_type_and_mapped_type<T>::value;
  };

  // Concept that checks if a type is a container but not a string (because
  // strings handling must be handled differently) and not a map-like container
  template <typename T>
  concept ContainerButNotString =
    requires(T a) {
      { a.size() } -> std::convertible_to<std::size_t>;
      {
        a[std::declval<std::size_t>()]
      }; // check if elements are accessible for the subscript operator
    } && !std::is_same_v<T, std::string> &&
    !std::is_same_v<T, std::string_view> && !std::is_same_v<T, const char *> &&
    !has_key_type_and_mapped_type<T>::value;

  // Concept that checks if a type is a map-like container with key-value pairs
  // but not a vector-like container
  template <typename T>
  concept Map = requires(T m) {
    typename T::key_type;
    typename T::mapped_type;
    { m.begin() } -> std::same_as<typename T::iterator>;
    { m.end() } -> std::same_as<typename T::iterator>;
    { m.empty() } -> std::same_as<bool>;
    { m.size() } -> std::convertible_to<std::size_t>;
    { m.find(std::declval<typename T::key_type>()) } -> std::same_as<typename T::iterator>;
    { m.insert(std::declval<std::pair<const typename T::key_type, typename T::mapped_type>>()) };
  } && has_key_type_and_mapped_type<T>::value && !is_vector_like<T>::value;

  template <typename T>
  concept UserDefinedType = std::is_class<T>::value;

  template <typename T>
  concept Function = std::is_function_v<T> || std::is_invocable_v<T>;

  template <typename T>
  concept UniquePointer = is_unique_ptr<T>::value || std::is_pointer_v<T>;

  template <typename T>
  concept SharedPointer = is_shared_ptr<T>::value || is_weak_ptr<T>::value ;

  template <typename T>
  concept Pointer = UniquePointer<T> || SharedPointer<T>;

  template <typename T>
  concept UnsignedInteger = std::is_unsigned_v<T>;

  template <typename T>
  concept SignedInteger = std::is_signed_v<T>;

  template <typename T>
  concept FloatingPoint = std::is_floating_point_v<T>;

  template <typename T>
  concept String = std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> || std::is_same_v<T, const char *>;

  template <typename T>
  concept Enum = std::is_enum_v<T>;

  template <typename T>
  concept Boolean = std::is_same_v<T, bool>;

  template <typename T>
  concept Void = std::is_same_v<T, void>;

  template <typename T>
  concept Binary = std::is_same_v<T, std::vector<std::uint8_t>>;

  template <typename T>
  concept Polymorphic = std::is_polymorphic_v<T>;

  template <typename T>
  concept ExceptionLike = std::is_class_v<T> && std::is_base_of_v<std::exception, T>;

  template <typename T>
  concept SimpleException =
    ExceptionLike<T> &&
    requires(const T& e) {
      { e.what() } -> std::convertible_to<const char*>;
    } &&
    (std::is_constructible_v<T, const std::string&> || std::is_constructible_v<T, const char*>);

}