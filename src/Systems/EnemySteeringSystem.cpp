#include "EnemySteeringSystem.h"

#include <cmath>

#include "Components/HealthComponent.h"
#include "Components/SeparationComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "General/PerfUtils.h"

static void ProcessChase(const float /*dt*/, const glm::vec2& target_pos, const PositionComponent& pos,
                         RigidBodyComponent& rb, EnemySteeringComponent& steering) {
  const float dx = target_pos.x - pos.value.x;
  const float dy = target_pos.y - pos.value.y;
  const float distSq = dx * dx + dy * dy;
  const float standoff = steering.half_size + 16.0f;

  if (distSq > (standoff * standoff) && distSq > 0.001f) {
    const float dist = std::sqrt(distSq);
    const float nx = dx / dist;
    const float ny = dy / dist;
    rb.velocity.x = nx * steering.speed;
    rb.velocity.y = ny * steering.speed;
  } else {
    rb.velocity.x = 0.0f;
    rb.velocity.y = 0.0f;
  }
}

static void SetSquareColor(Entity entity, Registry* registry, const octarine::Color& color) {
  if (registry->HasComponent<SquarePrimitiveComponent>(entity)) {
    registry->GetComponent<SquarePrimitiveComponent>(entity).color = color;
  }
}

static void RestoreBaseColor(Entity entity, Registry* registry) {
  if (registry->HasComponent<HealthComponent>(entity) && registry->HasComponent<SquarePrimitiveComponent>(entity)) {
    const auto& health = registry->GetComponent<HealthComponent>(entity);
    registry->GetComponent<SquarePrimitiveComponent>(entity).color =
        octarine::Color{health.base_color_r, health.base_color_g, health.base_color_b, health.base_color_a};
  }
}

static void ProcessChargeStalk(Entity entity, Registry* registry, const glm::vec2& d, float dist, float standoff,
                               RigidBodyComponent& rb, EnemySteeringComponent& steering) {
  if (dist <= steering.charge_range && steering.state_timer <= 0.0f) {
    steering.state = 1;  // windup
    steering.state_timer = steering.windup_time;
    rb.velocity = glm::vec2(0.0f);
    SetSquareColor(entity, registry, octarine::Color{255, 220, 40, 255});
  } else if (dist > standoff && dist > 0.001f) {
    rb.velocity.x = (d.x / dist) * steering.speed * 0.7f;
    rb.velocity.y = (d.y / dist) * steering.speed * 0.7f;
  } else {
    rb.velocity = glm::vec2(0.0f);
  }
}

static void ProcessChargeWindup(Entity entity, Registry* registry, const glm::vec2& d, float dist,
                                RigidBodyComponent& rb, EnemySteeringComponent& steering) {
  rb.velocity = glm::vec2(0.0f);
  const bool hitFlashing = registry->HasComponent<HealthComponent>(entity) &&
                           registry->GetComponent<HealthComponent>(entity).hit_flash_timer > 0.0f;
  if (!hitFlashing) {
    SetSquareColor(entity, registry, octarine::Color{255, 220, 40, 255});
  }
  if (steering.state_timer <= 0.0f) {
    steering.state = 2;  // charge
    steering.state_timer = steering.charge_duration;
    const float validDist = (dist > 0.001f) ? dist : 1.0f;
    steering.custom_dir.x = (d.x / validDist) * steering.charge_speed;
    steering.custom_dir.y = (d.y / validDist) * steering.charge_speed;
    rb.velocity = steering.custom_dir;
    RestoreBaseColor(entity, registry);
  }
}

static void ProcessCharge(Entity entity, Registry* registry, const float dt, const glm::vec2& target_pos,
                          const PositionComponent& pos, RigidBodyComponent& rb, EnemySteeringComponent& steering) {
  steering.state_timer -= dt;
  const glm::vec2 d = target_pos - pos.value;
  const float distSq = d.x * d.x + d.y * d.y;
  const float dist = (distSq > 0.001f) ? std::sqrt(distSq) : 0.0f;
  const float standoff = steering.half_size + 16.0f;

  if (steering.state == 0) {
    ProcessChargeStalk(entity, registry, d, dist, standoff, rb, steering);
  } else if (steering.state == 1) {
    ProcessChargeWindup(entity, registry, d, dist, rb, steering);
  } else if (steering.state == 2) {
    rb.velocity = steering.custom_dir;
    if (steering.state_timer <= 0.0f) {
      steering.state = 3;
      steering.state_timer = steering.charge_cooldown;
    }
  } else if (steering.state == 3) {
    if (steering.state_timer <= 0.0f) {
      steering.state = 0;
      ProcessChargeStalk(entity, registry, d, dist, standoff, rb, steering);
    } else if (dist > standoff && dist > 0.001f) {
      rb.velocity.x = (d.x / dist) * steering.speed * 0.4f;
      rb.velocity.y = (d.y / dist) * steering.speed * 0.4f;
    } else {
      rb.velocity = glm::vec2(0.0f);
    }
  }
}

static void ProcessSineFlyer(const float dt, const glm::vec2& target_pos, const PositionComponent& pos,
                             RigidBodyComponent& rb, EnemySteeringComponent& steering) {
  if (!steering.initialized) {
    steering.initialized = true;
    steering.age = 0.0f;
    const glm::vec2 d = target_pos - pos.value;
    const float distSq = d.x * d.x + d.y * d.y;
    if (distSq < 0.001f) {
      steering.custom_dir = glm::vec2(1.0f, 0.0f);
    } else {
      const float dist = std::sqrt(distSq);
      steering.custom_dir = d / dist;
    }
    steering.perpDir = glm::vec2(-steering.custom_dir.y, steering.custom_dir.x);
  }

  steering.age += dt;
  const float s = steering.speed * steering.age;
  constexpr float kAmplitude = 70.0f;
  constexpr float kFrequency = 0.016f;
  const float lateralSpeed = steering.speed * kAmplitude * kFrequency * std::cos(s * kFrequency);
  rb.velocity = steering.custom_dir * steering.speed + steering.perpDir * lateralSpeed;
}

static void ProcessFlyThrough(const float dt, const glm::vec2& target_pos, const PositionComponent& pos,
                              RigidBodyComponent& rb, EnemySteeringComponent& steering) {
  if (!steering.initialized) {
    steering.initialized = true;
    steering.age = 0.0f;
    const glm::vec2 d = target_pos - pos.value;
    const float distSq = d.x * d.x + d.y * d.y;
    if (distSq < 0.001f) {
      steering.custom_dir = glm::vec2(1.0f, 0.0f);
    } else {
      const float dist = std::sqrt(distSq);
      steering.custom_dir = d / dist;
    }
  }

  steering.age += dt;
  rb.velocity = steering.custom_dir * steering.speed;
}

static void ProcessSkitter(const float dt, const glm::vec2& target_pos, const PositionComponent& pos,
                           RigidBodyComponent& rb, EnemySteeringComponent& steering) {
  steering.age += dt * 11.0f;
  const float dx = target_pos.x - pos.value.x;
  const float dy = target_pos.y - pos.value.y;
  const float distSq = dx * dx + dy * dy;
  const float standoff = steering.half_size + 16.0f;

  if (distSq > (standoff * standoff) && distSq > 0.001f) {
    const float dist = std::sqrt(distSq);
    const float fwdX = dx / dist;
    const float fwdY = dy / dist;
    const float perpX = -fwdY;
    const float perpY = fwdX;

    const float wobble = std::sin(steering.age) * 0.8f;
    float dirX = fwdX + perpX * wobble;
    float dirY = fwdY + perpY * wobble;
    const float lenSq = dirX * dirX + dirY * dirY;
    if (lenSq > 0.0f) {
      const float len = std::sqrt(lenSq);
      dirX /= len;
      dirY /= len;
    }

    rb.velocity.x = dirX * steering.speed;
    rb.velocity.y = dirY * steering.speed;
  } else {
    rb.velocity = glm::vec2(0.0f);
  }
}

static void ProcessOrbit(Entity entity, const float /*dt*/, const glm::vec2& target_pos, const PositionComponent& pos,
                         RigidBodyComponent& rb, EnemySteeringComponent& steering) {
  if (!steering.initialized) {
    steering.initialized = true;
    steering.orbit_dir = (entity.GetId() % 2 == 0) ? 1.0f : -1.0f;
  }

  const float dx = target_pos.x - pos.value.x;
  const float dy = target_pos.y - pos.value.y;
  const float distSq = dx * dx + dy * dy;

  if (distSq > 0.001f) {
    const float dist = std::sqrt(distSq);
    const float fwdX = dx / dist;
    const float fwdY = dy / dist;
    const float tanX = -fwdY * steering.orbit_dir;
    const float tanY = fwdX * steering.orbit_dir;

    float radialSpeed = 0.0f;
    if (dist > steering.orbit_radius + 25.0f) {
      radialSpeed = steering.speed * 0.9f;
    } else if (dist < steering.orbit_radius - 25.0f) {
      radialSpeed = -steering.speed * 0.5f;
    } else {
      radialSpeed = 25.0f;
    }

    const float tanSpeed = steering.speed * 0.85f;
    rb.velocity.x = fwdX * radialSpeed + tanX * tanSpeed;
    rb.velocity.y = fwdY * radialSpeed + tanY * tanSpeed;
  } else {
    rb.velocity = glm::vec2(0.0f);
  }
}

static void ProcessFleeSpitter(const float /*dt*/, const glm::vec2& target_pos, const PositionComponent& pos,
                               RigidBodyComponent& rb, EnemySteeringComponent& steering) {
  const float dx = target_pos.x - pos.value.x;
  const float dy = target_pos.y - pos.value.y;
  const float distSq = dx * dx + dy * dy;

  if (distSq > 0.001f) {
    const float dist = std::sqrt(distSq);
    if (dist < steering.flee_range) {
      const float fleeSpeed = steering.speed * 1.2f;
      rb.velocity.x = -(dx / dist) * fleeSpeed;
      rb.velocity.y = -(dy / dist) * fleeSpeed;
    } else if (dist > steering.flee_range + 80.0f) {
      rb.velocity.x = (dx / dist) * steering.speed * 0.4f;
      rb.velocity.y = (dy / dist) * steering.speed * 0.4f;
    } else {
      rb.velocity = glm::vec2(0.0f);
    }
  } else {
    rb.velocity = glm::vec2(0.0f);
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Switch dispatch over steering types is intrinsically
// complex but flat.
void EnemySteeringSystem::operator()(const ContextFacade& ctx) {
  auto* registry = ctx.GetRegistry();
  EnsureInitialized(registry);

  steering_query_->Update();
  if (registry->IsPaused()) {
    steering_query_->ParallelForEach([](Entity /*entity*/, const PositionComponent& /*pos*/, RigidBodyComponent& rb,
                                        EnemySteeringComponent& /*steering*/) { rb.velocity = glm::vec2(0.0f); });
    return;
  }
  const float dt = ctx.GetDeltaTime();

  steering_query_->ParallelForEach(
      [&](Entity entity, const PositionComponent& pos, RigidBodyComponent& rb, EnemySteeringComponent& steering) {
        if (!registry->IsAlive(steering.target_entity) ||
            !registry->HasComponent<PositionComponent>(steering.target_entity)) {
          rb.velocity = glm::vec2(0.0f);
          return;
        }

        glm::vec2 currentTarget = registry->GetComponent<PositionComponent>(steering.target_entity).value;
        steering.target_pos = currentTarget;

        if (steering.dynamic_separation_weight) {
          if (registry->HasComponent<SeparationComponent>(entity)) {
            auto& sep = registry->GetComponent<SeparationComponent>(entity);
            const float dx = currentTarget.x - pos.value.x;
            const float dy = currentTarget.y - pos.value.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            sep.weight = 1.0f + (1000.0f / (dist + 10.0f));
          }
        }

        switch (steering.type) {
          case EnemyBehaviorType::Chase:
            ProcessChase(dt, currentTarget, pos, rb, steering);
            break;
          case EnemyBehaviorType::Charge:
            ProcessCharge(entity, registry, dt, currentTarget, pos, rb, steering);
            break;
          case EnemyBehaviorType::Skitter:
            ProcessSkitter(dt, currentTarget, pos, rb, steering);
            break;
          case EnemyBehaviorType::Orbit:
            ProcessOrbit(entity, dt, currentTarget, pos, rb, steering);
            break;
          case EnemyBehaviorType::Flee:
          case EnemyBehaviorType::ChaseSpitter:
            ProcessFleeSpitter(dt, currentTarget, pos, rb, steering);
            break;
          case EnemyBehaviorType::SineFlyer:
            ProcessSineFlyer(dt, currentTarget, pos, rb, steering);
            break;
          case EnemyBehaviorType::FlyThrough:
            ProcessFlyThrough(dt, currentTarget, pos, rb, steering);
            break;
          case EnemyBehaviorType::Dummy:
            rb.velocity = glm::vec2(0.0f);
            break;
        }
      });
}
