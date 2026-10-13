#include "Systems/CombatSystem.h"

#include "Components/EnemySteeringComponent.h"
#include "Components/HomingComponent.h"
#include "Components/PositionComponent.h"
#include "ECS/Query.h"
#include "Events/CombatDamagePlayerEvent.h"
#include "Events/CombatDeathEvent.h"
#include "Events/CombatHitEvent.h"
#include "General/PerfUtils.h"

inline void CombatSystem::InvokeDamagePlayer(Entity target, ContactDamageComponent& contact) {
  if (!registry_->IsAlive(target)) {
    RemoveContactDamage(contact, target);
    return;
  }
  if (event_bus_) {
    event_bus_->EmitEvent<CombatDamagePlayerEvent>(target, static_cast<int>(contact.damage));
  }
  contact.current_cooldown = contact.cooldown;
}

inline void CombatSystem::RemoveWeaponContact(HealthComponent& health, Entity weaponA) {
  for (uint8_t i = 0; i < health.active_contacts_count; ++i) {
    if (health.active_contacts[i].weapon_entity == weaponA) {
      health.active_contacts[i] = health.active_contacts[health.active_contacts_count - 1];
      health.active_contacts[health.active_contacts_count - 1] = WeaponContact{};
      health.active_contacts_count--;
      break;
    }
  }
}

inline void CombatSystem::RemoveContactDamage(ContactDamageComponent& contact, Entity b) {
  for (uint8_t i = 0; i < contact.active_contacts_count; ++i) {
    if (contact.active_contacts[i] == b) {
      contact.active_contacts[i] = contact.active_contacts[contact.active_contacts_count - 1];
      contact.active_contacts[contact.active_contacts_count - 1] = Entity{};
      contact.active_contacts_count--;
      break;
    }
  }
}

void CombatSystem::Init(Registry* registry, const std::unique_ptr<EventBus>& eventBus) {
  registry_ = registry;
  event_bus_ = eventBus.get();
  enemy_tag_ = registry_->TagId("enemy");
  enemy_alt_tag_ = registry_->TagId("enemies");
  enter_subscription_ =
      eventBus->SubscribeEvent<CombatSystem, CollisionBatchEvent>(this, &CombatSystem::OnCollisionBatch);
  exit_subscription_ =
      eventBus->SubscribeEvent<CombatSystem, CollisionExitBatchEvent>(this, &CombatSystem::OnCollisionExitBatch);
}

void CombatSystem::OnCollisionBatch(const CollisionBatchEvent& event) {
  for (const auto& pair : event.pairs) {
    HandleEnter(pair.first, pair.second);
    HandleEnter(pair.second, pair.first);
  }
}

void CombatSystem::OnCollisionExitBatch(const CollisionExitBatchEvent& event) {
  for (const auto& pair : event.pairs) {
    HandleExit(pair.first, pair.second);
    HandleExit(pair.second, pair.first);
  }
}

inline void CombatSystem::TryAddWeaponContact(Entity weapon, Entity target) {
  if (!registry_->HasComponent<WeaponZoneComponent>(weapon) || !registry_->HasComponent<HealthComponent>(target))
    return;
  auto& health = registry_->GetComponent<HealthComponent>(target);

  for (uint8_t i = 0; i < health.active_contacts_count; ++i) {
    if (health.active_contacts[i].weapon_entity == weapon) {
      return;
    }
  }
  if (health.active_contacts_count < health.active_contacts.size()) {
    health.active_contacts[health.active_contacts_count++] = {weapon,
                                                              0.0f};  // Trigger damage immediately on next update
  }
}

inline void CombatSystem::TryAddContactDamage(Entity attacker, Entity player) {
  if (!registry_->HasComponent<ContactDamageComponent>(attacker)) return;
  auto& contact = registry_->GetComponent<ContactDamageComponent>(attacker);
  if (player != contact.target_entity || contact.target_entity == Entity{}) return;

  for (uint8_t i = 0; i < contact.active_contacts_count; ++i) {
    if (contact.active_contacts[i] == player) {
      return;
    }
  }
  if (contact.active_contacts_count < contact.active_contacts.size()) {
    contact.active_contacts[contact.active_contacts_count++] = player;
  }

  if (contact.current_cooldown <= 0.0f) {
    // Find world.damage_player in Lua
    InvokeDamagePlayer(player, contact);
  }
}

void CombatSystem::HandleEnter(Entity a, Entity b) {
  TryAddWeaponContact(a, b);
  TryAddContactDamage(a, b);
}

void CombatSystem::HandleExit(Entity a, Entity b) {
  if (registry_->HasComponent<WeaponZoneComponent>(a) && registry_->HasComponent<HealthComponent>(b)) {
    auto& health = registry_->GetComponent<HealthComponent>(b);
    RemoveWeaponContact(health, a);
  }

  if (registry_->HasComponent<ContactDamageComponent>(a)) {
    auto& contact = registry_->GetComponent<ContactDamageComponent>(a);
    if (b == contact.target_entity) {
      RemoveContactDamage(contact, b);
    }
  }
}

void CombatSystem::operator()(const ContextFacade& ctx) {
  if (ctx.IsPaused()) return;
  auto* registry = ctx.GetRegistry();
  float dt = ctx.GetDeltaTime();

  if (!health_query_) {
    health_query_ = registry->CreateQuery<HealthComponent>();
  }
  if (!contact_query_) {
    contact_query_ = registry->CreateQuery<ContactDamageComponent>();
  }

  contact_query_->Update();
  contact_query_->ForEach([&](Entity /*entity*/, ContactDamageComponent& contact) {
    if (contact.current_cooldown > 0.0f) {
      contact.current_cooldown -= dt;
    }

    if (contact.current_cooldown <= 0.0f && contact.active_contacts_count > 0) {
      Entity target = contact.active_contacts.front();
      InvokeDamagePlayer(target, contact);
    }
  });

  health_query_->Update();
  health_query_->ForEach([&](Entity entity, HealthComponent& health) {
    UpdateHitFlash(registry, entity, health, dt);
    ProcessWeaponContacts(registry, entity, health, dt);
  });
}

void CombatSystem::UpdateHitFlash(Registry* registry, Entity entity, HealthComponent& health, float dt) {
  if (health.hit_flash_timer > 0.0f) {
    health.hit_flash_timer -= dt;
    if (health.hit_flash_timer <= 0.0f) {
      if (registry->HasComponent<SquarePrimitiveComponent>(entity)) {
        auto& sq = registry->GetComponent<SquarePrimitiveComponent>(entity);
        sq.color = octarine::Color{health.base_color_r, health.base_color_g, health.base_color_b, health.base_color_a};
      }
    }
  }
}

void CombatSystem::ProcessWeaponContacts(Registry* registry, Entity entity, HealthComponent& health, float dt) {
  for (uint8_t i = 0; i < health.active_contacts_count;) {
    health.active_contacts[i].timer -= dt;
    if (health.active_contacts[i].timer > 0.0f) {
      ++i;
      continue;
    }

    if (!registry->IsAlive(health.active_contacts[i].weapon_entity) ||
        !registry->HasComponent<WeaponZoneComponent>(health.active_contacts[i].weapon_entity)) {
      health.active_contacts[i] = health.active_contacts[health.active_contacts_count - 1];
      health.active_contacts[health.active_contacts_count - 1] = WeaponContact{};
      health.active_contacts_count--;
      continue;
    }

    auto& weapon = registry->GetComponent<WeaponZoneComponent>(health.active_contacts[i].weapon_entity);
    if (weapon.consumed) {
      health.active_contacts[i] = health.active_contacts[health.active_contacts_count - 1];
      health.active_contacts[health.active_contacts_count - 1] = WeaponContact{};
      health.active_contacts_count--;
      continue;
    }

    if (TryApplyHit(registry, entity, health, weapon, health.active_contacts[i].weapon_entity)) {
      break;
    }

    health.active_contacts[i].timer = weapon.rehit_cooldown;
    ++i;
  }
}

bool CombatSystem::TryApplyHit(Registry* registry, Entity entity, HealthComponent& health, WeaponZoneComponent& weapon,
                               Entity weaponEntity) {
  // Dash-only enemies are immune to non-dash weapon hits (brief "clang" flash).
  if (health.dash_only && !weapon.is_dash_weapon) {
    health.hit_flash_timer = 0.07f;
    if (registry->HasComponent<SquarePrimitiveComponent>(entity)) {
      auto& sq = registry->GetComponent<SquarePrimitiveComponent>(entity);
      sq.color = octarine::Color{180, 200, 230, 255};
    }
    return false;
  }

  health.Damage(static_cast<int>(weapon.damage));
  health.hit_flash_timer = 0.1f;
  if (registry->HasComponent<SquarePrimitiveComponent>(entity)) {
    auto& sq = registry->GetComponent<SquarePrimitiveComponent>(entity);
    sq.color = octarine::Color{255, 255, 255, 255};
  }

  if (weapon.pierce > 0) {
    weapon.pierce--;
    if (weapon.pierce == 0) {
      weapon.consumed = true;
    }
  }

  if (!weapon.consumed) {
    RetargetHomingWeapon(registry, weaponEntity, entity);
  }

  InvokeHitCallbacks(registry, weaponEntity, entity);

  if (health.IsDead()) {
    HandleEntityDeath(registry, entity);
    return true;
  }
  return false;
}

void CombatSystem::RetargetHomingWeapon(Registry* registry, Entity weaponEntity, Entity currentTarget) {
  if (!registry->HasComponent<HomingComponent>(weaponEntity)) return;
  auto& homing = registry->GetComponent<HomingComponent>(weaponEntity);
  if (!homing.retarget_on_hit) return;
  if (!registry->HasComponent<PositionComponent>(weaponEntity)) return;

  const auto& wpos = registry->GetComponent<PositionComponent>(weaponEntity);
  Entity best_target{};
  float best_dist = std::numeric_limits<float>::max();

  health_query_->ForEach([&](Entity e, HealthComponent& h) {
    if (e == currentTarget || h.IsDead()) return;
    if (!registry->HasTag(e, enemy_tag_) && !registry->HasTag(e, enemy_alt_tag_)) return;
    if (!registry->HasComponent<PositionComponent>(e)) return;

    const auto& epos = registry->GetComponent<PositionComponent>(e);
    const float dx = epos.value.x - wpos.value.x;
    const float dy = epos.value.y - wpos.value.y;
    const float dist = dx * dx + dy * dy;
    if (dist < best_dist) {
      best_dist = dist;
      best_target = e;
    }
  });

  if (registry->IsAlive(best_target)) {
    homing.target = best_target;
  }
}

void CombatSystem::InvokeHitCallbacks(Registry* registry, Entity weaponEntity, Entity entity) {
  (void)registry;
  if (event_bus_) {
    event_bus_->EmitEvent<CombatHitEvent>(weaponEntity, entity);
  }
}

void CombatSystem::HandleEntityDeath(Registry* registry, Entity entity) {
  if (event_bus_) {
    event_bus_->EmitEvent<CombatDeathEvent>(entity);
  }
  registry->QueueDespawnEntity(entity);
}
