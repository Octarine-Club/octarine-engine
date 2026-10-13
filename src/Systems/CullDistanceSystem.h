#pragma once

#include "Components/EnemySteeringComponent.h"
#include "Components/PositionComponent.h"
#include "ECS/Context.h"
#include "ECS/ECS.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"

class CullDistanceSystem {
 public:
  void operator()(const ContextFacade& context);

 private:
  std::shared_ptr<ComponentQuery<PositionComponent, EnemySteeringComponent>> query_ = nullptr;
  float cull_t_ = 0.5f;

  void PickPerimeterPosition(glm::vec2& out_pos, glm::vec2 player_pos, float map_w, float map_h) const;
};
