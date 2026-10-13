#pragma once

#include "ECS/Entity.h"
#include "EventBus/Event.h"

struct CombatDamagePlayerEvent : public Event {
  Entity player;
  int damage;
  CombatDamagePlayerEvent(Entity p, int d) : player(p), damage(d) {}
};
