#pragma once

#include <glm/glm.hpp>

#include "ECS/Entity.h"
#include "General/Constants.h"

// `p1`, `p2`, and `p3` are local-space vertex offsets relative to the entity's transform,
// scaled and rotated around the entity's pivot (matching TrianglePrimitiveComponent).
struct TriangleColliderComponent {
  EntityMask collisionMask{Constants::kDefaultEntityMask};
  glm::vec2 p1{0.0f, 0.0f};
  glm::vec2 p2{0.0f, 0.0f};
  glm::vec2 p3{0.0f, 0.0f};
  bool isFixed{false};

  explicit TriangleColliderComponent(const glm::vec2 t_p1 = glm::vec2(0.0f, 0.0f),
                                     const glm::vec2 t_p2 = glm::vec2(0.0f, 0.0f),
                                     const glm::vec2 t_p3 = glm::vec2(0.0f, 0.0f), const bool t_isFixed = false,
                                     const EntityMask t_collisionMask = Constants::kDefaultEntityMask)
      : collisionMask(t_collisionMask), p1(t_p1), p2(t_p2), p3(t_p3), isFixed(t_isFixed) {}
};
