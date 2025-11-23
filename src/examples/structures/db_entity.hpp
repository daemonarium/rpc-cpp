#pragma once

#include <map>
#include <string>
#include "serialization/annotations.hpp"
#include <memory>

namespace db {

  template <typename T>
  class Id {
    public:
      using value_type = T;

      Id() = default;
      Id(size_t id) : id_(id) {}

      size_t id() const { return id_; }

      std::shared_ptr<T> fetch() {
        return fetch<T>(id_);
      }

    private:
      size_t id_;
  };

  class Entity {
    public:
      Entity() = default;
      Entity(std::string name, Id<Entity> id) : name_(std::move(name)), id(std::move(id)) {}

      Id<Entity> getId() {
        return id;
      }

      std::string name() const { return name_; }

      static Entity& get_rpc_object(Id<Entity> id);

    private:
      std::string name_;
      Id<Entity> id;
  };
  
  class Db {
    public:      
      Db() = default;

      std::map<size_t, std::shared_ptr<Entity>> entities;

      std::shared_ptr<Entity> get_entity(Id<Entity> id) {
        auto it = entities.find(id.id());
        if(it == entities.end()) return nullptr;
        return it->second;
      }

      void put_entity(std::shared_ptr<Entity> entity) {
        entities.insert(std::make_pair(entity->getId().id(), entity));
      }

      static Entity empty_entity() {
        return Entity("empty", Id<Entity>(0));
      }

      static Db& get_rpc_instance();

    private:    
      static std::shared_ptr<Db> instance_;

  };

  

  Db& Db::get_rpc_instance() {
    if(instance_ == nullptr) {
      instance_ = std::make_shared<Db>();
    }
    return *instance_;
  }

  std::shared_ptr<Db> Db::instance_ = nullptr;

  Entity& Entity::get_rpc_object(Id<Entity> id) {
    return *Db::get_rpc_instance().get_entity(id);
  }
}