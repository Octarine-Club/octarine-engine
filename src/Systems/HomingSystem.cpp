#include "Systems/HomingSystem.h"

#include <cmath>

#include "ECS/Query.h"

void HomingSystem::operator()(const ContextFacade& ctx) {
  if (ctx.IsPaused()) return;
  auto* registry = ctx.GetRegistry();

  if (!query_) {
    query_ = registry->CreateQuery<PositionComponent, RigidBodyComponent, HomingComponent>();
  }

  query_->Update();
  query_->ParallelForEach(
      [&](Entity /*entity*/, const PositionComponent& pos, RigidBodyComponent& rb, const HomingComponent& homing) {
        if (!registry->IsAlive(homing.target)) return;

        if (registry->HasComponent<PositionComponent>(homing.target)) {
          const auto& target_pos = registry->GetComponent<PositionComponent>(homing.target);
          float dx = target_pos.value.x - pos.value.x;
          float dy = target_pos.value.y - pos.value.y;
          float distanceSq = dx * dx + dy * dy;

          if (distanceSq > 0.0001f) {
            float distance = std::sqrt(distanceSq);
            rb.velocity.x = (dx / distance) * homing.speed;
            rb.velocity.y = (dy / distance) * homing.speed;
          }
        }
      });
}
