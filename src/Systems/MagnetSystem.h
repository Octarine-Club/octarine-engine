#pragma once

#include "Components/BoxColliderComponent.h"
#include "Components/MagnetComponent.h"
#include "Components/PositionComponent.h"
#include "ECS/Context.h"
#include "ECS/ECS.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"

class MagnetSystem {
 public:
  void operator()(const ContextFacade& context);

 private:
  std::shared_ptr<ComponentQuery<PositionComponent, MagnetComponent, Opt<BoxColliderComponent>>> query_ = nullptr;
};
