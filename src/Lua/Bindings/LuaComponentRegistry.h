#pragma once

#include <functional>
#include <sol/sol.hpp>
#include <string>
#include <vector>

#include "ECS/Entity.h"
#include "ECS/Registry.h"
#include "Lua/Bindings/LuaBinding.h"
#include "Lua/LuaSchemaRegistry.h"

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

  // Registers a component's sol2 usertype and `registry.<op>_<key>` accessors.
  // Borrow invalidation note: `get_<key>` and `try_get_<key>` return zero-copy references (`T&` / `T*`)
  // into SoA archetype chunks. Structural ECS mutations on the entity or same chunk (`add_<key>`,
  // `remove_<key>`, `create_entity`, `blam_entity`, `add_tag`, `remove_tag`) may relocate chunk
  // storage and invalidate previously obtained component references; re-fetch after structural changes.
  template <typename T>
  static void registerComponent() {
    using B = LuaBinding<T>;
    using octarine::lua_schema::LuaSchemaRegistry;
    LuaSchemaRegistry::QueueUsertypeIntrospection<T>(B::kUsertypeName);
    Entry entry;
    entry.luaKey = B::kLuaKey;
    entry.usertypeName = B::kUsertypeName;
    entry.attach = [](Registry* r, Entity e, const sol::object& d) { r->AddComponent(e, B::fromLua(d)); };
    entry.bindUsertype = [](sol::state& lua) { B::bindUsertype(lua); };
    entry.bindRegistryAccessors = [](sol::table& reg, Registry* r) {
      const std::string key(B::kLuaKey);
      LuaSchemaRegistry::SetFunction(reg, "registry", "has_" + key,
                                     [r](const Entity e) -> bool { return r != nullptr && r->HasComponent<T>(e); });
      LuaSchemaRegistry::SetFunction(reg, "registry", "get_" + key,
                                     [r](const Entity e) -> T& { return r->GetComponent<T>(e); });
      LuaSchemaRegistry::SetFunction(reg, "registry", "try_get_" + key, [r](const sol::optional<Entity> e) -> T* {
        if (r == nullptr || !e.has_value() || !r->HasComponent<T>(*e)) {
          return nullptr;
        }
        return &r->GetComponent<T>(*e);
      });
      LuaSchemaRegistry::SetFunction(
          reg, "registry", "add_" + key,
          [r](const Entity e, const sol::optional<sol::object>& data, const sol::this_state s) -> T* {
            if (r == nullptr || !r->IsAlive(e)) {
              return nullptr;
            }
            sol::state_view luaView(s);
            const sol::object effData = (data.has_value() && data->valid() && *data != sol::lua_nil)
                                            ? *data
                                            : sol::make_object(luaView, luaView.create_table());
            r->AddComponent(e, B::fromLua(effData));
            return r->HasComponent<T>(e) ? &r->GetComponent<T>(e) : nullptr;
          });
      LuaSchemaRegistry::SetFunction(reg, "registry", "remove_" + key, [r](const sol::optional<Entity> e) -> bool {
        if (r == nullptr || !e.has_value() || !r->HasComponent<T>(*e)) {
          return false;
        }
        r->RemoveComponent<T>(*e);
        return true;
      });
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
