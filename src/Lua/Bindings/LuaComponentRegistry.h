#pragma once

#include <functional>
#include <sol/sol.hpp>
#include <string>
#include <vector>

#include "ECS/Entity.h"
#include "ECS/Registry.h"
#include "Lua/Bindings/LuaBinding.h"

class LuaComponentRegistry {
 public:
  struct Entry {
    using AttachFn = void (*)(Registry*, Entity, const sol::object&);
    using BindUsertypeFn = void (*)(sol::state&);
    using BindRegistryAccessorsFn = void (*)(sol::table&, Registry*);

    std::string luaKey;
    std::string usertypeName;
    AttachFn attach = nullptr;
    BindUsertypeFn bindUsertype = nullptr;
    BindRegistryAccessorsFn bindRegistryAccessors = nullptr;
  };

  template <typename T>
  static void registerComponent() {
    using B = LuaBinding<T>;
    Entry entry;
    entry.luaKey = B::kLuaKey;
    entry.usertypeName = B::kUsertypeName;
    entry.attach = [](Registry* r, Entity e, const sol::object& d) { r->AddComponent(e, B::fromLua(d)); };
    entry.bindUsertype = [](sol::state& lua) { B::bindUsertype(lua); };
    entry.bindRegistryAccessors = [](sol::table& reg, Registry* r) {
      reg.set_function(std::string("has_") + B::kLuaKey, [r](Entity e) { return r->HasComponent<T>(e); });
      reg.set_function(std::string("get_") + B::kLuaKey, [r](Entity e) -> T& { return r->GetComponent<T>(e); });
    };
    entries().push_back(std::move(entry));
  }

  static const std::vector<Entry>& all() { return entries(); }

  static const Entry* find(const std::string& luaKey) {
    for (const auto& entry : entries()) {
      if (entry.luaKey == luaKey) return &entry;
    }
    return nullptr;
  }

  static void clear() { entries().clear(); }

 private:
  static std::vector<Entry>& entries() {
    static std::vector<Entry> instance;
    return instance;
  }
};
