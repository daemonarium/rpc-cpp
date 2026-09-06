#include "serialization/annotations.hpp"
#include "serialization/type_description.hpp"
#include "serialization/type_registry.hpp"
#include "serialization/type_registry_blob.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace odb {

  // Payload cannot be const char* in [[=table("records")]]: clang rejects
  // pointers to string-literal subobjects as annotation template arguments.
  // Names live in the tag type as NTTP char arrays instead:
  //   [[=odb::table<"records">{}]]
  template <std::size_t N>
  struct nttp_string {
    char value[N]{};
    consteval nttp_string(const char (&s)[N]) {
      for(std::size_t i = 0; i < N; ++i) {
        value[i] = s[i];
      }
    }
  };

  class [[=serialization::dynamic()]] table_annotation : public serialization::type_annotation {
  public:
    std::string name;

    table_annotation() = default;
    explicit table_annotation(std::string n) : name(std::move(n)) {}

    std::string to_string() const override {
      return "table(" + name + ")";
    }
  };

  template <nttp_string Name>
  struct table {
    serialization::type_annotation_ptr annotation_ptr() const {
      return std::make_shared<table_annotation>(Name.value);
    }
  };

  class [[=serialization::dynamic()]] index_annotation : public serialization::type_annotation {
  public:
    std::string name;
    std::string source;

    index_annotation() = default;
    index_annotation(std::string n, std::string s)
      : name(std::move(n)), source(std::move(s)) {}

    std::string to_string() const override {
      return "index(" + name + ", " + source + ")";
    }
  };

  template <nttp_string Name, nttp_string Source>
  struct index {
    serialization::type_annotation_ptr annotation_ptr() const {
      return std::make_shared<index_annotation>(Name.value, Source.value);
    }
  };

}

struct [[=odb::table<"records">{}]] example_record {
  std::string id;
  std::uint8_t priority;

  [[=serialization::read()]]
  static std::string hello() {
    return "hello";
  }
};

struct [[=odb::index<"by_priority", "records">{}]] by_priority_key {
  std::uint8_t priority;
  std::string object;
};

static std::string dump_type(const serialization::type_description& type) {
  std::ostringstream os;
  os << type;
  return os.str();
}

static const serialization::type_description* find_type(
  const serialization::type_registry& registry,
  std::string_view name
) {
  for(const auto& type : registry.get_types()) {
    if(type && type->getName() == name) {
      return type.get();
    }
  }
  return nullptr;
}

static bool dump_contains(const serialization::type_description* type, std::string_view needle) {
  if(!type) {
    return false;
  }
  const std::string text = dump_type(*type);
  return text.find(needle) != std::string::npos;
}

static int check_registry(const serialization::type_registry& registry, const char* label) {
  std::cout << "=== " << label << " ===\n";
  for(const auto& type : registry.get_types()) {
    std::cout << *type << std::endl;
  }

  const auto* record = find_type(registry, "example_record");
  const auto* key = find_type(registry, "by_priority_key");

  bool ok = true;
  if(!dump_contains(record, "table(records)")) {
    std::cerr << label << ": missing table(records) on example_record\n";
    ok = false;
  }
  if(!dump_contains(key, "index(by_priority, records)")) {
    std::cerr << label << ": missing index(by_priority, records) on by_priority_key\n";
    ok = false;
  }
  if(!dump_contains(record, "[[=read]")) {
    std::cerr << label << ": missing [[=read] on example_record.hello\n";
    ok = false;
  }
  return ok ? 0 : 1;
}

int main() {
  serialization::type_registry registry;
  serialization::type_description::create(odb::table_annotation{}, registry);
  serialization::type_description::create(odb::index_annotation{}, registry);
  serialization::type_description::create(serialization::read_annotation{}, registry);
  serialization::type_description::create(example_record{}, registry);
  serialization::type_description::create(by_priority_key{}, registry);

  if(check_registry(registry, "harvest") != 0) {
    return 1;
  }

  const std::string blob = serialization::serialize_type_registry_blob<
    odb::table_annotation,
    odb::index_annotation
  >(registry);

  serialization::type_registry restored(false);
  serialization::deserialize_type_registry_blob<
    odb::table_annotation,
    odb::index_annotation
  >(blob, restored);

  if(check_registry(restored, "blob") != 0) {
    return 1;
  }

  std::cout << "generic annotations round-trip OK\n";
  return 0;
}
