#include "RetaliationSystem.h"

#include "Components/BoxColliderComponent.h"
#include "Components/ContactDamageComponent.h"
#include "Components/EnemySteeringComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/LifetimeComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RetaliationComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "Components/WeaponZoneComponent.h"
#include "EntityPoolSystem.h"

void RetaliationSystem::Init(Registry& registry, EventBus* event_bus) {
  registry_ = &registry;
  subscription_ = event_bus->SubscribeEvent(this, &RetaliationSystem::OnCombatHit);

  hazard_pool_id_ = registry.Get<EntityPoolManager>().RegisterPool<ContactDamageComponent>(registry, [](Registry& reg) {
    reg.Tag<PoolableTag>();
    return reg.CreateEntityWithBundle(
        PositionComponent(),
        SquarePrimitiveComponent(glm::vec2(0, 0), 44, 44, 0.0f, octarine::Color{80, 200, 40, 160}, false),
        BoxColliderComponent(44, 44, glm::vec2(0, 0), false, EntityMask{1}), EntityMaskComponent{2},
        ContactDamageComponent{{}, {}, 6.0f, 0.5f, 0.0f, 0}, LifetimeComponent{6.0f}, PoolableTag{});
  });
}

void RetaliationSystem::OnCombatHit(CombatHitEvent& event) {
  if (registry_ != nullptr && registry_->IsPaused()) {
    return;
  }
  if (!registry_->IsAlive(event.target) || !registry_->HasComponent<RetaliationComponent>(event.target)) {
    return;
  }
  if (!registry_->IsAlive(event.weapon) || !registry_->HasComponent<WeaponZoneComponent>(event.weapon)) {
    return;
  }

  const auto& weapon = registry_->GetComponent<WeaponZoneComponent>(event.weapon);
  if (!weapon.is_dash_weapon) {
    return;
  }

  const auto& retaliation = registry_->GetComponent<RetaliationComponent>(event.target);

  glm::vec2 spawnPosition(0, 0);
  if (registry_->HasComponent<PositionComponent>(event.target)) {
    spawnPosition = registry_->GetComponent<PositionComponent>(event.target).value;

    // Attempt to center based on EnemySteeringComponent or BoxColliderComponent
    float enemy_half_width = 16.0f;
    float enemy_half_height = 16.0f;
    if (registry_->HasComponent<EnemySteeringComponent>(event.target)) {
      enemy_half_width = registry_->GetComponent<EnemySteeringComponent>(event.target).half_size;
      enemy_half_height = enemy_half_width;
    } else if (registry_->HasComponent<BoxColliderComponent>(event.target)) {
      enemy_half_width = static_cast<float>(registry_->GetComponent<BoxColliderComponent>(event.target).width) / 2.0f;
      enemy_half_height = static_cast<float>(registry_->GetComponent<BoxColliderComponent>(event.target).height) / 2.0f;
    }

    spawnPosition.x += enemy_half_width - (retaliation.size / 2.0f);
    spawnPosition.y += enemy_half_height - (retaliation.size / 2.0f);
  }

  const Entity hazard = registry_->Get<EntityPoolManager>().Spawn(*registry_, hazard_pool_id_);
  registry_->GetComponent<PositionComponent>(hazard).value = spawnPosition;

  auto& sq = registry_->GetComponent<SquarePrimitiveComponent>(hazard);
  sq.width = retaliation.size;
  sq.height = retaliation.size;
  sq.color = retaliation.color;

  registry_->GetComponent<BoxColliderComponent>(hazard).width = static_cast<int>(retaliation.size);
  registry_->GetComponent<BoxColliderComponent>(hazard).height = static_cast<int>(retaliation.size);
  registry_->GetComponent<BoxColliderComponent>(hazard).collisionMask = EntityMask{1};
  registry_->GetComponent<EntityMaskComponent>(hazard).mask = EntityMask{2};

  auto& cd = registry_->GetComponent<ContactDamageComponent>(hazard);
  cd.target_entity = retaliation.target_entity;
  cd.damage = retaliation.damage;
  cd.cooldown = retaliation.tick_interval;
  cd.current_cooldown = 0.0f;
  cd.active_contacts_count = 0;

  registry_->GetComponent<LifetimeComponent>(hazard).lifetimeDuration = retaliation.lifetime;
  registry_->GetComponent<LifetimeComponent>(hazard).remainingDuration = retaliation.lifetime;
}
