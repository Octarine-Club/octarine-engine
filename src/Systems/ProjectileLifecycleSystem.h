#pragma once

#include "Components/ProjectileComponent.h"
#include "ECS/CommandBuffer.h"
#include "ECS/Registry.h"

class ProjectileLifecycleSystem {
 public:
  CommandBuffer& GetCommandBuffer() { return cmd_buffer_; }

  bool Prepare(Registry* registry) {
    paused_ = registry->IsPaused();
    return !paused_;
  }

  void operator()(const Entity entity, const float deltaTime, ProjectileComponent& projectile) {
    if (paused_) return;
    projectile.timer += deltaTime;
    if (projectile.timer > projectile.duration) {
      cmd_buffer_.EmplaceDespawn(entity);
    }
  }

 private:
  CommandBuffer cmd_buffer_;
  bool paused_ = false;
};
