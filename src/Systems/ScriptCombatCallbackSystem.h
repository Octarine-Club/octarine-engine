#pragma once

#include <memory>
#include <sol/sol.hpp>
#include <string>
#include <vector>

#include "Components/ScriptCallbackComponent.h"
#include "Components/ScriptComponent.h"
#include "ECS/Context.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Events/CombatDamagePlayerEvent.h"
#include "Events/CombatDeathEvent.h"
#include "Events/CombatHitEvent.h"
#include "General/Logger.h"

class ScriptCombatCallbackSystem {
 public:
  void Init(Registry* registry, const std::unique_ptr<EventBus>& eventBus) {
    registry_ = registry;
    hit_subscription_ =
        eventBus->SubscribeEvent<ScriptCombatCallbackSystem, CombatHitEvent>(this, &ScriptCombatCallbackSystem::OnHit);
    death_subscription_ = eventBus->SubscribeEvent<ScriptCombatCallbackSystem, CombatDeathEvent>(
        this, &ScriptCombatCallbackSystem::OnDeath);
    damage_subscription_ = eventBus->SubscribeEvent<ScriptCombatCallbackSystem, CombatDamagePlayerEvent>(
        this, &ScriptCombatCallbackSystem::OnDamagePlayer);
  }

  void OnHit(const CombatHitEvent& event) { hit_events_.push_back(event); }

  void OnDeath(const CombatDeathEvent& event) { death_events_.push_back(event); }

  void OnDamagePlayer(const CombatDamagePlayerEvent& event) { damage_events_.push_back(event); }

  void operator()(const ContextFacade& /*ctx*/) {
    if (registry_ != nullptr && registry_->IsPaused()) return;
    for (const auto& event : damage_events_) {
      if (!registry_->IsAlive(event.player) || !registry_->HasComponent<ScriptComponent>(event.player)) continue;
      auto& script = registry_->GetComponent<ScriptComponent>(event.player);
      if (script.script_table == sol::lua_nil) continue;
      sol::state_view lua(script.script_table.lua_state());
      sol::table world = lua["world"];
      if (!world.valid()) continue;
      sol::function dmgFn = world["damage_player"];
      if (!dmgFn.valid()) continue;
      if (auto result = dmgFn(event.damage); !result.valid()) {
        Logger::ErrorLua(std::string(result.get<sol::error>().what()));
      }
    }
    damage_events_.clear();

    for (const auto& event : hit_events_) {
      if (registry_->HasComponent<ScriptCallbackComponent>(event.weapon)) {
        auto& weaponCb = registry_->GetComponent<ScriptCallbackComponent>(event.weapon);
        if (weaponCb.on_hit_function != sol::lua_nil && weaponCb.script_table != sol::lua_nil) {
          if (auto result =
                  weaponCb.on_hit_function.as<sol::function>()(weaponCb.script_table, event.weapon, event.target);
              !result.valid()) {
            Logger::ErrorLua(std::string(result.get<sol::error>().what()));
          }
        }
      }

      if (registry_->HasComponent<ScriptCallbackComponent>(event.target)) {
        auto& healthCb = registry_->GetComponent<ScriptCallbackComponent>(event.target);
        if (healthCb.on_hit_function != sol::lua_nil && healthCb.script_table != sol::lua_nil) {
          if (auto result =
                  healthCb.on_hit_function.as<sol::function>()(healthCb.script_table, event.target, event.weapon);
              !result.valid()) {
            Logger::ErrorLua(std::string(result.get<sol::error>().what()));
          }
        }
      }
    }
    hit_events_.clear();

    for (const auto& event : death_events_) {
      if (registry_->HasComponent<ScriptCallbackComponent>(event.entity)) {
        auto& healthCb = registry_->GetComponent<ScriptCallbackComponent>(event.entity);
        if (healthCb.on_death_function != sol::lua_nil && healthCb.script_table != sol::lua_nil) {
          if (auto result = healthCb.on_death_function.as<sol::function>()(healthCb.script_table, event.entity);
              !result.valid()) {
            Logger::ErrorLua(std::string(result.get<sol::error>().what()));
          }
        }
      }
    }
    death_events_.clear();
  }

 private:
  Registry* registry_ = nullptr;
  EventBus::SubscriptionHandle hit_subscription_;
  EventBus::SubscriptionHandle death_subscription_;
  EventBus::SubscriptionHandle damage_subscription_;

  std::vector<CombatHitEvent> hit_events_{};
  std::vector<CombatDeathEvent> death_events_{};
  std::vector<CombatDamagePlayerEvent> damage_events_{};
};
