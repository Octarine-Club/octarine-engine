#pragma once

#include <glm/glm.hpp>

#include "ECS/Entity.h"
#include "General/Constants.h"

// `offset` is the circle center's local-space offset relative to the entity's transform,
// scaled and rotated around the entity's pivot (matching CirclePrimitiveComponent::position).
struct CircleColliderComponent {
  EntityMask collisionMask{Constants::kDefaultEntityMask};
  glm::vec2 offset{0.0f, 0.0f};
  float radius{0.0f};
  bool isFixed{false};

  explicit CircleColliderComponent(const float t_radius = 0.0f, const glm::vec2 t_offset = glm::vec2(0.0f, 0.0f),
                                   const bool t_isFixed = false,
                                   const EntityMask t_collisionMask = Constants::kDefaultEntityMask)
      : collisionMask(t_collisionMask), offset(t_offset), radius(t_radius), isFixed(t_isFixed) {}
};
