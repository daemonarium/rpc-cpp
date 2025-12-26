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
    std::uint64_t& u64, std::int64_t& i64, double& f64, bool& b, std::string& s, std::vector<std::uint8_t>& bin, std::size_t& l,
    std::shared_ptr<void>& data,
    std::weak_ptr<void>& weak_data
  ) {
    { reader.read_u64(u64) } -> std::same_as<void>;
    { reader.read_i64(i64) } -> std::same_as<void>;
    { reader.read_f64(f64) } -> std::same_as<void>;
    { reader.read_bool(b) } -> std::same_as<void>;
    { reader.read_string(s) } -> std::same_as<void>;
    { reader.read_binary(bin) } -> std::same_as<void>;
    { reader.read_length(l) } -> std::same_as<void>;
    { reader.is_indexed() } -> std::same_as<bool>;
    { reader.enter_frame() } -> std::same_as<void>;
    { reader.exit_frame() } -> std::same_as<void>;
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

      void read_u64(std::uint64_t& value) { throw std::runtime_error("Not implemented"); }
      void read_i64(std::int64_t& value) { throw std::runtime_error("Not implemented"); }
      void read_f64(double& value) { throw std::runtime_error("Not implemented"); }
      void read_string(std::string* out) { throw std::runtime_error("Not implemented"); }
      void read_bool(bool& value) { throw std::runtime_error("Not implemented"); }
      void read_binary(std::vector<std::uint8_t>& value) { throw std::runtime_error("Not implemented"); }
      
      std::size_t read_length() { throw std::runtime_error("Not implemented"); }

      using ReadLater = std::function<void(std::shared_ptr<void>&)>;
  };

}