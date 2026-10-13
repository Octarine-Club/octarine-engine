#pragma once

#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "ECS/Registry.h"

class VelocityIntegrationSystem {
 public:
  bool Prepare(Registry* registry) {
    paused_ = registry->IsPaused();
    return !paused_;
  }

  void operator()(const float deltaTime, PositionComponent& position, const RigidBodyComponent& rigidBody) const {
    if (paused_) return;
    position.value.x += rigidBody.velocity.x * deltaTime;
    position.value.y += rigidBody.velocity.y * deltaTime;
  }

 private:
  bool paused_ = false;
};
