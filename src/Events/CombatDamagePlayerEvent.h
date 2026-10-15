#pragma once

#include "ECS/Entity.h"
#include "EventBus/Event.h"

struct CombatDamagePlayerEvent : public Event {
  Entity player;
  int damage;
  // Entity that dealt the damage (contact attacker or projectile); Entity{} when unknown.
  // Forwarded to Lua world.damage_player(amount, nil, source_entity) for damage attribution.
  Entity source;
  CombatDamagePlayerEvent(Entity p, int d, Entity s = Entity{}) : player(p), damage(d), source(s) {}
};
