#pragma once

#include "ECS/Entity.h"
#include "EventBus/Event.h"

struct CombatDeathEvent : public Event {
  Entity entity;
  explicit CombatDeathEvent(Entity e) : entity(e) {}
};
