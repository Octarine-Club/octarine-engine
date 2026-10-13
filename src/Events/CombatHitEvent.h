#pragma once

#include "ECS/Entity.h"
#include "EventBus/Event.h"

struct CombatHitEvent : public Event {
  Entity weapon;
  Entity target;
  CombatHitEvent(Entity w, Entity t) : weapon(w), target(t) {}
};
