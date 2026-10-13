#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "Components/BoxColliderComponent.h"
#include "Components/MagnetComponent.h"
#include "Components/PositionComponent.h"
#include "ECS/Context.h"
#include "ECS/ECS.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"

class MagnetSystem {
 public:
  struct Attractor {
    glm::vec2 center{};
    uint32_t attracts_mask = 0;
    float attraction_radius = 0.0f;
  };

  void operator()(const ContextFacade& context);

 private:
  std::shared_ptr<ComponentQuery<PositionComponent, MagnetComponent, Opt<BoxColliderComponent>>> query_ = nullptr;
  std::vector<Attractor> attractors_;
};
