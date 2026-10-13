#pragma once

#include "ECS/Context.h"
#include "ECS/ECS.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Events/CombatHitEvent.h"

class RetaliationSystem {
 public:
  void Init(Registry& registry, EventBus* event_bus);
  void OnCombatHit(CombatHitEvent& event);

 private:
  Registry* registry_ = nullptr;
  ArchetypeID hazard_pool_id_{};
  EventBus::SubscriptionHandle subscription_{};
};
