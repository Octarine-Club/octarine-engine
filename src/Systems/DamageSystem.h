#pragma once

#include <utility>
#include <vector>

#include "Components/EnemySteeringComponent.h"
#include "Components/HealthComponent.h"
#include "Components/ProjectileComponent.h"
#include "Components/ScriptComponent.h"
#include "Engine/EngineContext.h"
#include "EventBus/EventBus.h"
#include "Events/CollisionBatchEvent.h"
#include "Events/CombatDamagePlayerEvent.h"
#include "Events/CombatDeathEvent.h"
#include "Events/CombatHitEvent.h"
#include "General/Constants.h"
#include "Systems/CollisionResponseParallel.h"

class DamageSystem {
 public:
  void Init(Registry* registry, const std::unique_ptr<EventBus>& eventBus) {
    registry_ = registry;
    event_bus_ = eventBus.get();
    projectiles_ = registry_->Tag<ProjectileTag>();
    enemies_ = registry_->TagId("enemy");
    enemies_alt_ = registry_->TagId("enemies");
    subscription_ = eventBus->SubscribeEvent<DamageSystem, CollisionBatchEvent>(this, &DamageSystem::OnCollisionBatch);
  }

  // Process the whole frame's collision pairs (W2.3), parallelising the per-pair tag classification
  // above a density threshold (parallel collision response). The classify step does registry reads
  // only (safe to run across worker threads — the registry is structurally stable during the
  // response); the damage/despawn mutations run serially in apply(). See CollisionResponseParallel.h.
  void OnCollisionBatch(const CollisionBatchEvent& event) {
    using Hit = std::pair<Entity, Entity>;  // (projectile, target)
    RunCollisionResponse<Hit>(
        event.pairs, Constants::kCollisionResponseParallelThreshold,
        [this](const Entity a, const Entity b, std::vector<Hit>& sink) {
          if (registry_->HasTag(a, projectiles_)) {
            const auto& proj = registry_->GetComponent<ProjectileComponent>(a);
            bool is_target = (proj.target_entity == b && b != Entity{});
            bool is_player_proj_hitting_enemy =
                (proj.target_entity == Entity{} && registry_->HasComponent<HealthComponent>(b) &&
                 (registry_->HasTag(b, enemies_) || registry_->HasTag(b, enemies_alt_)));
            if (is_target || is_player_proj_hitting_enemy) {
              sink.emplace_back(a, b);
            }
          }
          if (registry_->HasTag(b, projectiles_)) {
            const auto& proj = registry_->GetComponent<ProjectileComponent>(b);
            bool is_target = (proj.target_entity == a && a != Entity{});
            bool is_player_proj_hitting_enemy =
                (proj.target_entity == Entity{} && registry_->HasComponent<HealthComponent>(a) &&
                 (registry_->HasTag(a, enemies_) || registry_->HasTag(a, enemies_alt_)));
            if (is_target || is_player_proj_hitting_enemy) {
              sink.emplace_back(b, a);
            }
          }
        },
        [this](const Hit& hit) { OnProjectileHit(hit.first, hit.second); });
  }

  void OnProjectileHit(const Entity projectile, const Entity target) const {
    const auto projectileComponent = registry_->GetComponent<ProjectileComponent>(projectile);

    if (projectileComponent.target_entity == target) {
      if (event_bus_) {
        event_bus_->EmitEvent<CombatDamagePlayerEvent>(target, static_cast<int>(projectileComponent.damage));
      }
      registry_->QueueDespawnEntity(projectile);
      return;
    }

    if (!registry_->HasComponent<HealthComponent>(target)) {
      registry_->QueueDespawnEntity(projectile);
      return;
    }

    auto& targetComponent = registry_->GetComponent<HealthComponent>(target);
    targetComponent.Damage(projectileComponent.damage);

    if (targetComponent.IsDead()) {
      if (event_bus_) {
        event_bus_->EmitEvent<CombatDeathEvent>(target);
      }
      registry_->QueueDespawnEntity(target);
    } else {
      if (event_bus_) {
        event_bus_->EmitEvent<CombatHitEvent>(projectile, target);
      }
    }

    registry_->QueueDespawnEntity(projectile);
  }

 private:
  Registry* registry_ = nullptr;
  EventBus* event_bus_ = nullptr;
  Entity projectiles_{};
  Entity enemies_{};
  Entity enemies_alt_{};
  EventBus::SubscriptionHandle subscription_;
};