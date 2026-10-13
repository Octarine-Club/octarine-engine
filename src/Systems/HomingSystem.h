#pragma once

#include "Components/HomingComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "ECS/Context.h"
#include "ECS/Query.h"

class HomingSystem {
 public:
  void operator()(const ContextFacade& ctx);

 private:
  std::unique_ptr<ComponentQuery<PositionComponent, RigidBodyComponent, HomingComponent>> query_ = nullptr;
};
