#pragma once

#include <map>
#include <string>
#include <vector>

class DbgKv {
  public:
    static void put(std::string key, std::string value) {
      store()[std::move(key)] = std::move(value);
    }

    static std::string get(std::string key) {
      const auto it = store().find(key);
      if (it == store().end()) {
        return {};
      }
      return it->second;
    }

    static std::vector<std::string> keys() {
      std::vector<std::string> out;
      out.reserve(store().size());
      for (const auto& [k, _] : store()) {
        out.push_back(k);
      }
      return out;
    }

  private:
    static std::map<std::string, std::string>& store() {
      static std::map<std::string, std::string> m;
      return m;
    }
};
