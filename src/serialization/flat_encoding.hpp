#pragma once

#include "type_concepts.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace serialization {

  inline constexpr std::uint8_t flat_format_width_u8 = 0;
  inline constexpr std::uint8_t flat_format_width_u16 = 1;
  inline constexpr std::uint8_t flat_format_width_u32 = 2;

  inline constexpr std::size_t flat_pointer_delta_size = 4;

  inline std::size_t flat_offset_width(std::uint32_t max_cp) {
    if(max_cp < 256u) return 1;
    if(max_cp < 65536u) return 2;
    return 4;
  }

  inline std::uint8_t flat_pack_format(std::size_t width) {
    if(width == 1) return flat_format_width_u8;
    if(width == 2) return flat_format_width_u16;
    return flat_format_width_u32;
  }

  inline std::size_t flat_unpack_width(std::uint8_t format) {
    switch(format & 0x3u) {
      case flat_format_width_u8: return 1;
      case flat_format_width_u16: return 2;
      default: return 4;
    }
  }

  inline std::size_t flat_trailer_size(std::size_t k, std::size_t width) {
    if(k == 0) return 0;
    return 2 + k * width;
  }

  template <typename T>
  inline void flat_store_le(void* dest, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::uint8_t* out = static_cast<std::uint8_t*>(dest);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    std::memcpy(out, &value, sizeof(T));
#else
    auto u = static_cast<std::make_unsigned_t<T>>(value);
    for(std::size_t i = 0; i < sizeof(T); ++i) {
      out[i] = static_cast<std::uint8_t>(u & 0xFFu);
      u >>= 8;
    }
#endif
  }

  template <>
  inline void flat_store_le<float>(void* dest, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    flat_store_le(dest, bits);
  }

  template <>
  inline void flat_store_le<double>(void* dest, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    flat_store_le(dest, bits);
  }

  template <typename T>
  inline T flat_load_le(const void* src) {
    static_assert(std::is_trivially_copyable_v<T>);
    const std::uint8_t* in = static_cast<const std::uint8_t*>(src);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    T value{};
    std::memcpy(&value, in, sizeof(T));
    return value;
#else
    using U = std::make_unsigned_t<T>;
    U u = 0;
    for(std::size_t i = 0; i < sizeof(T); ++i) {
      u |= static_cast<U>(in[i]) << (8 * i);
    }
    return static_cast<T>(u);
#endif
  }

  template <>
  inline float flat_load_le<float>(const void* src) {
    std::uint32_t bits = flat_load_le<std::uint32_t>(src);
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  template <>
  inline double flat_load_le<double>(const void* src) {
    std::uint64_t bits = flat_load_le<std::uint64_t>(src);
    double value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  inline void flat_store_cp(void* dest, std::uint32_t cp, std::size_t width) {
    if(width == 1) {
      *static_cast<std::uint8_t*>(dest) = static_cast<std::uint8_t>(cp);
    } else if(width == 2) {
      flat_store_le(dest, static_cast<std::uint16_t>(cp));
    } else {
      flat_store_le(dest, cp);
    }
  }

  inline std::uint32_t flat_load_cp(const void* src, std::size_t width) {
    if(width == 1) {
      return *static_cast<const std::uint8_t*>(src);
    }
    if(width == 2) {
      return flat_load_le<std::uint16_t>(src);
    }
    return flat_load_le<std::uint32_t>(src);
  }

  template <typename T>
  concept FlatVariable =
    (String<T> || Binary<T> || (ContainerButNotString<T> && !Binary<T>) || Map<T> ||
     (UserDefinedType<T> && !String<T> && !ContainerButNotString<T> && !Map<T> && !Pointer<T> && !SimpleException<T>));

  template <typename T>
  constexpr bool is_flat_variable_v = FlatVariable<T>;

  template <typename T>
  consteval std::size_t flat_fixed_size() {
    if constexpr (SharedPointer<T>) {
      return flat_pointer_delta_size;
    } else if constexpr (Boolean<T>) {
      return 1;
    } else if constexpr (Enum<T>) {
      return 8;
    } else if constexpr ((UnsignedInteger<T> || SignedInteger<T> || FloatingPoint<T>) && !Boolean<T>) {
      return sizeof(T);
    } else {
      return 0;
    }
  }

} // namespace serialization
