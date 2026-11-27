#pragma once

#include <memory>

#include "Components/ScriptComponent.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Events/CollisionBatchEvent.h"
#include "Events/CollisionExitBatchEvent.h"
#include "General/Logger.h"

class ScriptCollisionSystem {
 public:
  void Init(Registry* registry, const std::unique_ptr<EventBus>& eventBus) {
    registry_ = registry;
    enterSubscription_ = eventBus->SubscribeEvent<ScriptCollisionSystem, CollisionBatchEvent>(
        this, &ScriptCollisionSystem::OnCollisionBatch);
    exitSubscription_ = eventBus->SubscribeEvent<ScriptCollisionSystem, CollisionExitBatchEvent>(
        this, &ScriptCollisionSystem::OnCollisionExitBatch);
  }

  void OnCollisionBatch(const CollisionBatchEvent& event) {
    for (const auto& [a, b] : event.pairs) {
      FireEnterCallback(a, b);
      FireEnterCallback(b, a);
    }
  }

  void OnCollisionExitBatch(const CollisionExitBatchEvent& event) {
    for (const auto& [a, b] : event.pairs) {
      FireExitCallback(a, b);
      FireExitCallback(b, a);
    }
  }

 private:
  static sol::object GetOrCacheNonScriptEntity(lua_State* L, const Entity entity) {
    constexpr const char* kCacheKey = "__octarine_collision_entity_cache";
    constexpr int kMaxCachedEntities = 8192;
    sol::state_view sv(L);
    sol::table reg = sv.registry();
    sol::object tblObj = reg.raw_get<sol::object>(kCacheKey);
    sol::table cache = tblObj.is<sol::table>() ? tblObj.as<sol::table>() : sv.create_table();
    if (!tblObj.is<sol::table>()) {
      cache.raw_set("__count", 0);
      reg.raw_set(kCacheKey, cache);
    }
    const auto key = static_cast<lua_Integer>(entity.id);
    sol::object cached = cache.raw_get<sol::object>(key);
    if (cached.valid() && cached != sol::lua_nil) {
      return cached;
    }
    int count = cache.raw_get_or("__count", 0);
    if (count >= kMaxCachedEntities) {
      cache = sv.create_table();
      count = 0;
      reg.raw_set(kCacheKey, cache);
    }
    sol::object created = sol::make_object(sv, entity);
    cache.raw_set(key, created);
    cache.raw_set("__count", count + 1);
    return created;
  }

  void DispatchCollisionCallback(const sol::protected_function& callback, ScriptComponent& script, const Entity self,
                                 const Entity other) const {
    if (callback == sol::lua_nil) {
      return;
    }
    lua_State* L = callback.lua_state();
    sol::object selfObj = script.GetOrCacheEntity(L, self);
    sol::object otherObj = registry_->HasComponent<ScriptComponent>(other)
                               ? registry_->GetComponent<ScriptComponent>(other).GetOrCacheEntity(L, other)
                               : GetOrCacheNonScriptEntity(L, other);
    if (auto result = callback(script.script_table, selfObj, otherObj); !result.valid()) {
      const sol::error err = result;
      Logger::ErrorLua(std::string(err.what()));
    }
  }

  void FireEnterCallback(const Entity self, const Entity other) const {
    if (!registry_->HasComponent<ScriptComponent>(self)) {
      return;
    }
    auto& script = registry_->GetComponent<ScriptComponent>(self);
    DispatchCollisionCallback(script.onCollisionFunction, script, self, other);
  }

  void FireExitCallback(const Entity self, const Entity other) const {
    if (!registry_->HasComponent<ScriptComponent>(self)) {
      return;
    }
    auto& script = registry_->GetComponent<ScriptComponent>(self);
    DispatchCollisionCallback(script.onCollisionExitFunction, script, self, other);
  }

  Registry* registry_ = nullptr;
  EventBus::SubscriptionHandle enterSubscription_;
  EventBus::SubscriptionHandle exitSubscription_;
};
