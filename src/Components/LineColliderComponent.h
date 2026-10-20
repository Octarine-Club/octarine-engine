#pragma once

#include <glm/glm.hpp>

#include "ECS/Entity.h"
#include "General/Constants.h"

// `start` and `end` are local-space endpoint offsets relative to the entity's transform,
// scaled and rotated around the entity's pivot (matching LinePrimitiveComponent).
struct LineColliderComponent {
  EntityMask collisionMask{Constants::kDefaultEntityMask};
  glm::vec2 start{0.0f, 0.0f};
  glm::vec2 end{0.0f, 0.0f};
  bool isFixed{false};

  explicit LineColliderComponent(const glm::vec2 t_start = glm::vec2(0.0f, 0.0f),
                                 const glm::vec2 t_end = glm::vec2(0.0f, 0.0f), const bool t_isFixed = false,
                                 const EntityMask t_collisionMask = Constants::kDefaultEntityMask)
      : collisionMask(t_collisionMask), start(t_start), end(t_end), isFixed(t_isFixed) {}
};
