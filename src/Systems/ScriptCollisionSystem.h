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
  void DispatchCollisionCallback(const sol::protected_function& callback, ScriptComponent& script, const Entity self,
                                 const Entity other) const {
    if (callback == sol::lua_nil) {
      return;
    }
    lua_State* L = callback.lua_state();
    sol::object selfObj = script.GetOrCacheEntity(L, self);
    if (registry_->HasComponent<ScriptComponent>(other)) {
      auto& otherScript = registry_->GetComponent<ScriptComponent>(other);
      sol::object otherObj = otherScript.GetOrCacheEntity(L, other);
      if (auto result = callback(script.script_table, selfObj, otherObj); !result.valid()) {
        const sol::error err = result;
        Logger::ErrorLua(std::string(err.what()));
      }
    } else {
      if (auto result = callback(script.script_table, selfObj, other); !result.valid()) {
        const sol::error err = result;
        Logger::ErrorLua(std::string(err.what()));
      }
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
