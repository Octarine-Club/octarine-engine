#pragma once

#include <memory>

#include "Components/HealthComponent.h"
#include "ECS/Context.h"
#include "ECS/Query.h"

class HealthRegenSystem {
 public:
  void operator()(const ContextFacade& ctx);

 private:
  std::unique_ptr<ComponentQuery<HealthComponent>> query_ = nullptr;
  float accum_seconds_ = 0.0F;
};
