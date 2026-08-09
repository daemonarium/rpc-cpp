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
    { writer.write_u32(23) } -> std::same_as<void>;
    { writer.write_i32(-23) } -> std::same_as<void>;
    { writer.write_u16(23) } -> std::same_as<void>;
    { writer.write_i16(-23) } -> std::same_as<void>;
    { writer.write_u8(std::uint8_t(23)) } -> std::same_as<void>;
    { writer.write_i8(std::int8_t(-23)) } -> std::same_as<void>;
    { writer.write_f64(2.3) } -> std::same_as<void>;
    { writer.write_f32(2.3f) } -> std::same_as<void>;
    { writer.write_string("test") } -> std::same_as<void>;
    { writer.write_bool(true) } -> std::same_as<void>;
    { writer.write_binary(std::vector<std::uint8_t>()) } -> std::same_as<void>;
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
      void write_u32(std::uint32_t value) { throw new std::runtime_error("writeU32 not implemented"); }
      void write_i32(std::int32_t value) { throw new std::runtime_error("writeI32 not implemented"); }
      void write_u16(std::uint16_t value) { throw new std::runtime_error("writeU16 not implemented"); }
      void write_i16(std::int16_t value) { throw new std::runtime_error("writeI16 not implemented"); }
      void write_u8(std::uint8_t value) { throw new std::runtime_error("writeU8 not implemented"); }
      void write_i8(std::int8_t value) { throw new std::runtime_error("writeI8 not implemented"); }
      void write_f64(double value) { throw new std::runtime_error("writeF64 not implemented"); }
      void write_f32(float value) { throw new std::runtime_error("writeF32 not implemented"); }
      void write_string(std::string_view value) { throw new std::runtime_error("writeString not implemented"); }
      void write_bool(bool value) { throw new std::runtime_error("writeBool not implemented"); }
      void write_binary(std::vector<std::uint8_t>&& value) { throw new std::runtime_error("writeBinary not implemented"); }

      void write_typed(std::type_index value, void* data) { 
        throw new std::runtime_error("writeTyped not implemented");
      }
      void write_length(std::size_t length) { throw new std::runtime_error("writeLength not implemented"); }
      void save_offset() { throw new std::runtime_error("saveOffset not implemented"); }

      void register_serialization_function(std::type_index type, std::function<void(void*)> serialize) {
        throw new std::runtime_error("registerSerializationFunction not implemented");
      }

      template <typename T>
      void register_serialize_typed_function() { 
        throw new std::runtime_error("registerSerializationFunction not implemented");
      }

      template <typename T>
      void register_serialize_message_function() { 
        throw new std::runtime_error("registerSerializeMessageFunction not implemented");
      }
      
      using WriteLater = std::function<void()>;

      /*
      * Build the deferred-serialization callback for a shared_ptr field.
      * Default throws — concrete writers override: compressed writers use
      * write_typed (polymorphic, with type index); flat writers serialize
      * the pointee directly (homogeneous graph, no type index per sub-frame).
      */
      template <typename T>
      WriteLater make_shared_write_later(std::shared_ptr<T> value) {
        throw std::runtime_error("make_shared_write_later not implemented");
      }

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