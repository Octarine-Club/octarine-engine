#pragma once

#include "Components/EnemySteeringComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "ECS/Context.h"
#include "ECS/Query.h"

class EnemySteeringSystem {
 public:
  void operator()(const ContextFacade& ctx);

 private:
  std::unique_ptr<ComponentQuery<PositionComponent, RigidBodyComponent, EnemySteeringComponent>> steering_query_ =
      nullptr;

  void EnsureInitialized(Registry* registry) {
    if (!steering_query_) {
      steering_query_ = registry->CreateQuery<PositionComponent, RigidBodyComponent, EnemySteeringComponent>();
    }
  }
};
