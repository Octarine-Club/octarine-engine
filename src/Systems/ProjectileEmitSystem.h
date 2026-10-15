#pragma once

#include <glm/glm.hpp>

#include "Components/BoxColliderComponent.h"
#include "Components/ContactDamageComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/LifetimeComponent.h"
#include "Components/NameComponent.h"
#include "Components/PositionComponent.h"
#include "Components/ProjectileComponent.h"
#include "Components/ProjectileEmitterComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/RotationComponent.h"
#include "Components/ScaleComponent.h"
#include "Components/SpriteComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "ECS/Context.h"
#include "ECS/ECS.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "General/Color.h"
#include "Systems/EntityPoolSystem.h"

class ProjectileEmitSystem {
 public:
  static ArchetypeID RegisterHazardPool(Registry& registry) {
    return registry.Get<EntityPoolManager>().RegisterPool<ContactDamageComponent>(registry, [](Registry& reg) {
      reg.Tag<PoolableTag>();
      return reg.CreateEntityWithBundle(
          PositionComponent(),
          SquarePrimitiveComponent(glm::vec2(0, 0), 44, 44, 0.0f, octarine::Color{80, 200, 40, 160}, false),
          BoxColliderComponent(44, 44, glm::vec2(0, 0), false, EntityMask{1}), EntityMaskComponent{2},
          ContactDamageComponent{{}, {}, 6.0f, 0.5f, 0.0f, 0}, LifetimeComponent{6.0f}, PoolableTag{});
    });
  }

  void Init(Registry& registry) {
    hazard_pool_id_ = RegisterHazardPool(registry);
    projectile_pool_id_ =
        registry.Get<EntityPoolManager>().RegisterPool<ProjectileComponent>(registry, [](Registry& reg) {
          reg.Tag<PoolableTag>();
          reg.Tag<ProjectileTag>();
          // NameComponent baked into the archetype so emitter-assigned names take the cheap
          // re-add path in Registry::AddComponent (assign-over) instead of an archetype transition,
          // which would break pool reuse (pool routes by archetype id).
          return reg.CreateEntityWithBundle(
              EntityMaskComponent(), PositionComponent(), ScaleComponent(), RotationComponent(),
              GlobalTransformComponent{}, RigidBodyComponent(),
              BoxColliderComponent(4, 4, glm::vec2(0, 0), false, EntityMask{}), ProjectileComponent(),
              SpriteComponent("bullet-texture", 4.0f, 4.0f, 4),
              SquarePrimitiveComponent(glm::vec2(0, 0), 3, 0.0f, 0.0f, octarine::Color{255, 255, 255, 0}, false),
              NameComponent(), PoolableTag{}, ProjectileTag{});
        });
  }

  // Lua entry point. `direction` is a non-normalized aim vector; zero falls back to the emitter's
  // configured velocity vector. Component-wise multiply matches the legacy player-fire semantics.
  void Fire(Registry& registry, Entity emitter, glm::vec2 direction) const {
    if (!registry.HasComponent<PositionComponent>(emitter) ||
        !registry.HasComponent<ProjectileEmitterComponent>(emitter)) {
      return;
    }
    const auto& position = registry.GetComponent<PositionComponent>(emitter);
    auto& emitterComp = registry.GetComponent<ProjectileEmitterComponent>(emitter);

    glm::vec2 velocity = emitterComp.velocity;
    if (glm::length(direction) > 1e-4f) {
      velocity = glm::normalize(direction) * emitterComp.velocity;
    }
    SpawnProjectile(registry, emitter, position.value, emitterComp, velocity);
  }

  // Per-frame auto-fire tick. Registered as a BulkSystem; iterates all emitters once per frame
  // and updates the target query exactly once. This avoids the O(N×M) cost of the old per-entity
  // approach where the target query was updated per emitter.
  //
  // Serial, not parallel: SpawnProjectile mutates the registry (pool spawn) — the same reason
  // ScriptSystem's fire path runs serially. Spawned projectiles land in a different (pooled)
  // archetype, so iterating the emitter archetype here is not invalidated by the spawn.
  void operator()(const ContextFacade& context) {
    if (context.IsPaused()) return;
    auto* registry = context.GetRegistry();
    const float dt = context.GetDeltaTime();

    if (!emitter_query_) {
      emitter_query_ = registry->CreateQuery<ProjectileEmitterComponent, PositionComponent>();
    }

    emitter_query_->Update();
    emitter_query_->ForEach([&](Entity entity, ProjectileEmitterComponent& emitter, const PositionComponent& position) {
      if (emitter.frequency <= 0.0f) {
        return;
      }
      emitter.count_down_timer -= dt;
      if (emitter.count_down_timer <= 0.0f) {
        glm::vec2 vel = emitter.velocity;
        if (emitter.aim_at_target && registry->IsAlive(emitter.target_entity) &&
            registry->HasComponent<PositionComponent>(emitter.target_entity)) {
          const glm::vec2 target_pos = registry->GetComponent<PositionComponent>(emitter.target_entity).value;
          const glm::vec2 diff = target_pos - position.value;
          const float len = glm::length(diff);
          if (len > 0.001f) {
            vel = (diff / len) * glm::length(emitter.velocity);
          }
        }
        SpawnProjectile(*registry, entity, position.value, emitter, vel);
        emitter.count_down_timer = emitter.frequency;
      }
    });
  }

 private:
  ArchetypeID projectile_pool_id_{};
  ArchetypeID hazard_pool_id_{};
  std::shared_ptr<ComponentQuery<ProjectileEmitterComponent, PositionComponent>> emitter_query_ = nullptr;

  void SpawnProjectile(Registry& registry, const Entity& entity, glm::vec2 spawnPosition,
                       const ProjectileEmitterComponent& emitter, glm::vec2 velocity) const {
    if (emitter.is_hazard) {
      const Entity hazard = registry.Get<EntityPoolManager>().Spawn(registry, hazard_pool_id_);
      registry.GetComponent<PositionComponent>(hazard).value = spawnPosition;
      auto& sq = registry.GetComponent<SquarePrimitiveComponent>(hazard);
      sq.width = emitter.size;
      sq.height = emitter.size;
      sq.color = emitter.color;
      registry.GetComponent<BoxColliderComponent>(hazard).width = static_cast<int>(emitter.size);
      registry.GetComponent<BoxColliderComponent>(hazard).height = static_cast<int>(emitter.size);
      registry.GetComponent<BoxColliderComponent>(hazard).collisionMask = EntityMask{1};
      registry.GetComponent<EntityMaskComponent>(hazard).mask = EntityMask{2};
      auto& cd = registry.GetComponent<ContactDamageComponent>(hazard);
      cd.target_entity = emitter.target_entity;
      cd.damage = static_cast<float>(emitter.damage);
      // TODO: Map hazard frequency to tick interval instead of repurposing velocity.x.
      cd.cooldown = (emitter.velocity.x > 0.0f) ? emitter.velocity.x : 0.5f;
      cd.current_cooldown = 0.0f;
      cd.active_contacts_count = 0;
      registry.GetComponent<LifetimeComponent>(hazard).lifetimeDuration = emitter.duration;
      registry.GetComponent<LifetimeComponent>(hazard).remainingDuration = emitter.duration;
      return;
    }
    if (registry.HasComponent<SpriteComponent>(entity)) {
      const auto& sprite = registry.GetComponent<SpriteComponent>(entity);
      const glm::vec2 emitterScale = registry.HasComponent<ScaleComponent>(entity)
                                         ? registry.GetComponent<ScaleComponent>(entity).value
                                         : glm::vec2(1.0f, 1.0f);
      spawnPosition.x += emitterScale.x * static_cast<float>(sprite.width) / 2;
      spawnPosition.y += emitterScale.y * static_cast<float>(sprite.height) / 2;
    }

    // Use the emitter's dedicated projectile mask — NOT the emitter entity's own
    // EntityMaskComponent.  Inheriting the emitter's identity caused projectiles
    // to be "seen" by the emitter's own collision mask, destroying themselves.
    const Entity projectile = registry.Get<EntityPoolManager>().Spawn(registry, projectile_pool_id_);
    registry.GetComponent<EntityMaskComponent>(projectile).mask = emitter.projectile_mask;
    registry.GetComponent<PositionComponent>(projectile).value = spawnPosition;
    registry.GetComponent<ScaleComponent>(projectile).value = glm::vec2(1.0f, 1.0f);
    registry.GetComponent<RotationComponent>(projectile).value = 0.0;
    registry.GetComponent<RigidBodyComponent>(projectile).velocity = velocity;
    registry.GetComponent<BoxColliderComponent>(projectile).collisionMask = emitter.collisionMask;
    registry.GetComponent<BoxColliderComponent>(projectile).width = static_cast<int>(emitter.size);
    registry.GetComponent<BoxColliderComponent>(projectile).height = static_cast<int>(emitter.size);

    auto& sq = registry.GetComponent<SquarePrimitiveComponent>(projectile);
    sq.width = emitter.size;
    sq.height = emitter.size;
    sq.color = emitter.color;

    // Hide the default sprite if we are emitting a square. If bullet-texture exists, it might render over the square,
    // but typically projects rely on one or the other. We can set sprite alpha to 0 or leave it.

    auto& projectileComponent = registry.GetComponent<ProjectileComponent>(projectile);
    projectileComponent.target_entity = emitter.target_entity;
    projectileComponent.owner = entity;
    projectileComponent.damage = emitter.damage;
    projectileComponent.duration = emitter.duration;
    registry.GetComponent<NameComponent>(projectile).name = emitter.projectile_name;
  }
};
