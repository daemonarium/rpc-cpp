#pragma once

#include <cstdint>
#include <string_view>
#include "type_concepts.hpp"
#include <functional>
#include <typeindex>

namespace serialization {

  template <typename W>
  concept Writer = requires(W& writer) {
    { writer.write_u64(23) } -> std::same_as<void>;
    { writer.write_i64(-23) } -> std::same_as<void>;
    { writer.write_f64(2.3) } -> std::same_as<void>;
    { writer.write_string("test") } -> std::same_as<void>;
    { writer.write_bool(true) } -> std::same_as<void>;
    { writer.write_binary("test") } -> std::same_as<void>;
    { writer.write_length(10) } -> std::same_as<void>;
    { writer.save_offset() } -> std::same_as<void>;
    { writer.begin_frame() } -> std::same_as<void>;
    { writer.end_frame() } -> std::same_as<void>;
    { writer.write_pointer(std::shared_ptr<std::string>(nullptr), []{}) } -> std::same_as<void>;
    { writer.write_pointer(std::weak_ptr<std::string>(std::shared_ptr<std::string>(nullptr)), []{}) } -> std::same_as<void>;    
    { writer.write_typed(std::shared_ptr<void>(nullptr)) } -> std::same_as<void>;
  };

  class writer { // abstract class for all writers
    public:
      /*
      * start a serialization frame, that contains object or array. 
      * This information is needed for:
      * - writing indexes for fast access, counting binary length
      * - calculating length of whole object
      * - putting lengths and indexes in proper places
      */
      void begin_frame() { throw new std::runtime_error("beginFrame not implemented"); }; 

      /*
      * end a serialization frame, that contains object or array. 
      * when it happens, we know the length of the frame, and we can write it whole to stream.
      */
      void end_frame() { throw new std::runtime_error("endFrame not implemented"); };

      void write_u64(std::uint64_t value) { throw new std::runtime_error("writeU64 not implemented"); }
      void write_i64(std::int64_t value) { throw new std::runtime_error("writeI64 not implemented"); }
      void write_f64(double value) { throw new std::runtime_error("writeF64 not implemented"); }
      void write_string(std::string_view value) { throw new std::runtime_error("writeString not implemented"); }
      void write_bool(bool value) { throw new std::runtime_error("writeBool not implemented"); }
      void write_binary(std::string_view value) { throw new std::runtime_error("writeBinary not implemented"); }

      void write_typed(std::type_index value, void* data) { 
        throw new std::runtime_error("writeTyped not implemented");
      }
      void write_length(std::size_t length) { throw new std::runtime_error("writeLength not implemented"); }
      void save_offset() { throw new std::runtime_error("saveOffset not implemented"); }

      void register_serialization_function(std::type_index type, std::function<void(void*)> serialize) {
        throw new std::runtime_error("registerSerializationFunction not implemented");
      }

      template <typename T>
      void register_serialization_function() { 
        throw new std::runtime_error("registerSerializationFunction not implemented");
      }
      
      using WriteLater = std::function<void()>;

      /*
      * If object was already serialized, write it index, 
      * if not, write next index after this object, and push to pointers stack
      */
      template <typename T>
      requires(Pointer<T>)
      void write_pointer(T value, WriteLater write_later) { 
        throw new std::runtime_error("writePointer not implemented");
      }

  };

}