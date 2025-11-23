#pragma once

#include <unordered_map>
#include <typeindex>
#include "type_description.hpp"

namespace serialization {

  class type_registry {
    public:

      type_registry()
        : types_({
          std::make_shared<type_description>("void", type_description::Kind::Void),
          std::make_shared<type_description>("uint", type_description::Kind::UnsignedInteger),
          std::make_shared<type_description>("sint", type_description::Kind::SignedInteger),
          std::make_shared<type_description>("float", type_description::Kind::FloatingPoint),
          std::make_shared<type_description>("bool", type_description::Kind::Boolean),
          std::make_shared<type_description>("string", type_description::Kind::String),
          std::make_shared<type_description>("binary", type_description::Kind::Binary),
          std::make_shared<type_description>("pointer", type_description::Kind::Pointer),
          std::make_shared<type_description>("map", type_description::Kind::Map)
        }) {

        for(const auto& type : types_) {
          types_by_index_.insert(std::make_pair(type->type_index_, type));
          types_by_name_.insert(std::make_pair(type->getName(), type));
        }           
      }
      ~type_registry() {}
      
      size_t register_type(std::shared_ptr<type_description> type) {
        size_t index = types_.size();                
        types_.push_back(type);
        types_by_index_.insert(std::make_pair(type->type_index_, type));
        types_by_name_.insert(std::make_pair(type->getName(), type));
        return index;
      }

      std::shared_ptr<type_description> get_type(size_t index) const {
        return types_.at(index);
      }

      std::shared_ptr<type_description> get_type(const std::type_index& type) const {
        return types_by_index_.at(type);
      }

      std::shared_ptr<type_description> get_type(const std::string_view& name) const {
        auto it = std::find_if(types_.begin(), types_.end(), [&](const std::shared_ptr<type_description>& type) {
          return type->getName() == name;
        });
        if(it == types_.end()) return nullptr;
        return *it;
      }

      const std::vector<std::shared_ptr<type_description>>& get_types() const {
        return types_;
      }

      template <typename W>
      requires(Writer<W>)
      void serialize(W writer) {
        writer.write_length(types_.size() - 9);
        for(int i = 9; i < types_.size(); ++i) {
          types_[i]->serialize(writer);
        }
      }

      template <typename R>
      requires(Reader<R>)
      void deserialize(R reader) {
        std::size_t size;
        reader.read_length(size);
        types_.resize(9 + size);
        for(std::size_t i = 0; i < size; ++i) {
          types_[9 + i] = std::make_shared<type_description>();
          types_[9 + i]->deserialize(reader);
        }
      }

    private:
      std::vector<std::shared_ptr<type_description>> types_;
      std::unordered_map<std::type_index, std::shared_ptr<type_description>> types_by_index_;
      std::unordered_map<std::string, std::shared_ptr<type_description>> types_by_name_;
  };

}