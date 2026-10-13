#pragma once

#include <memory>
#include <vector>

#include "Components/ContactDamageComponent.h"
#include "Components/HealthComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "Components/WeaponZoneComponent.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Events/CollisionBatchEvent.h"
#include "Events/CollisionExitBatchEvent.h"

class CombatSystem {
 public:
  void Init(Registry* registry, const std::unique_ptr<EventBus>& eventBus);
  void operator()(const ContextFacade& ctx);

 private:
  inline void InvokeDamagePlayer(Entity target, ContactDamageComponent& contact);
  inline void RemoveWeaponContact(HealthComponent& health, Entity weaponA);
  inline void RemoveContactDamage(ContactDamageComponent& contact, Entity b);
  void OnCollisionBatch(const CollisionBatchEvent& event);
  void OnCollisionExitBatch(const CollisionExitBatchEvent& event);

  void HandleEnter(Entity a, Entity b);
  void HandleExit(Entity a, Entity b);

  inline void TryAddWeaponContact(Entity weapon, Entity target);
  inline void TryAddContactDamage(Entity attacker, Entity player);

  void ProcessWeaponContacts(Registry* registry, Entity entity, HealthComponent& health, float dt);
  static void UpdateHitFlash(Registry* registry, Entity entity, HealthComponent& health, float dt);

  bool TryApplyHit(Registry* registry, Entity entity, HealthComponent& health, WeaponZoneComponent& weapon,
                   Entity weaponEntity);
  void RetargetHomingWeapon(Registry* registry, Entity weaponEntity, Entity currentTarget);
  void InvokeHitCallbacks(Registry* registry, Entity weaponEntity, Entity entity);
  void HandleEntityDeath(Registry* registry, Entity entity);

  Registry* registry_ = nullptr;
  EventBus* event_bus_ = nullptr;
  Entity enemy_tag_{};
  Entity enemy_alt_tag_{};
  EventBus::SubscriptionHandle enter_subscription_;
  EventBus::SubscriptionHandle exit_subscription_;

  std::unique_ptr<ComponentQuery<HealthComponent>> health_query_;
  std::unique_ptr<ComponentQuery<ContactDamageComponent>> contact_query_;
};
