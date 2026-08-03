#pragma once

#include <cstdint>
#include <string>
#include <typeindex>
#include <functional>
#include <memory>

namespace serialization {

  template<typename R> 
  concept Reader = requires(
    R reader, 
    std::uint64_t& u64, std::int64_t& i64, std::uint32_t& u32, std::int32_t& i32, std::uint16_t& u16, std::int16_t& i16,
    std::uint8_t& u8, std::int8_t& i8, double& f64, float& f32, bool& b, std::string& s, std::vector<std::uint8_t>& bin, std::size_t& l,
    std::shared_ptr<void>& data,
    std::weak_ptr<void>& weak_data
  ) {
    { reader.read_u64(u64) } -> std::same_as<void>;
    { reader.read_i64(i64) } -> std::same_as<void>;
    { reader.read_u32(u32) } -> std::same_as<void>;
    { reader.read_i32(i32) } -> std::same_as<void>;
    { reader.read_u16(u16) } -> std::same_as<void>;
    { reader.read_i16(i16) } -> std::same_as<void>;
    { reader.read_u8(u8) } -> std::same_as<void>;
    { reader.read_i8(i8) } -> std::same_as<void>;
    { reader.read_f64(f64) } -> std::same_as<void>;
    { reader.read_f32(f32) } -> std::same_as<void>;
    { reader.read_bool(b) } -> std::same_as<void>;
    { reader.read_string(s) } -> std::same_as<void>;
    { reader.read_binary(bin) } -> std::same_as<void>;
    { reader.read_length(l) } -> std::same_as<void>;
    { reader.is_indexed() } -> std::same_as<bool>;
    { reader.enter_frame() } -> std::same_as<void>;
    { reader.exit_frame() } -> std::same_as<void>;
    { reader.save_offset() } -> std::same_as<void>;
    { reader.enter_indexed_frame(0) } -> std::same_as<void>;
    { reader.read_typed(data) } -> std::same_as<void>;
    { reader.read_typed(weak_data) } -> std::same_as<void>;
    { reader.read_pointer(data, [](std::shared_ptr<void>&) { }) } -> std::same_as<void>;
    { reader.read_rest() } -> std::same_as<void>;
  };

  class reader { // abstract class for all readers
    public:

      consteval bool is_indexed() { throw std::runtime_error("Not implemented"); }

      void enter_indexed_frame(size_t id) { throw std::runtime_error("Not implemented"); }
      void enter_frame() { throw std::runtime_error("Not implemented"); }
      void exit_frame() { throw std::runtime_error("Not implemented"); }
      void save_offset() { throw std::runtime_error("Not implemented"); }

      void read_u64(std::uint64_t& value) { throw std::runtime_error("Not implemented"); }
      void read_i64(std::int64_t& value) { throw std::runtime_error("Not implemented"); }
      void read_u32(std::uint32_t& value) { throw std::runtime_error("Not implemented"); }
      void read_i32(std::int32_t& value) { throw std::runtime_error("Not implemented"); }
      void read_u16(std::uint16_t& value) { throw std::runtime_error("Not implemented"); }
      void read_i16(std::int16_t& value) { throw std::runtime_error("Not implemented"); }
      void read_u8(std::uint8_t& value) { throw std::runtime_error("Not implemented"); }
      void read_i8(std::int8_t& value) { throw std::runtime_error("Not implemented"); }
      void read_f64(double& value) { throw std::runtime_error("Not implemented"); }
      void read_f32(float& value) { throw std::runtime_error("Not implemented"); }
      void read_string(std::string& value) { throw std::runtime_error("Not implemented"); }
      void read_bool(bool& value) { throw std::runtime_error("Not implemented"); }
      void read_binary(std::vector<std::uint8_t>& value) { throw std::runtime_error("Not implemented"); }
      
      std::size_t read_length() { throw std::runtime_error("Not implemented"); }

      using ReadLater = std::function<void(std::shared_ptr<void>&)>;
  };

}