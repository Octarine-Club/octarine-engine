#pragma once

#include <algorithm>
#include <cmath>

#include "Components/CirclePrimitiveComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "General/Rotation2D.h"
#include "Systems/RenderPrimitiveSystem.h"

class RenderCirclePrimitiveSystem : public RenderPrimitiveBase {
 public:
  void operator()(const CirclePrimitiveComponent& circle, const GlobalTransformComponent& transform) const {
    const auto rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 worldCenter = TransformLocalPoint(circle.position, transform, rot);

    const float rx = circle.radius * std::abs(transform.scale.x);
    const float ry = circle.radius * std::abs(transform.scale.y);
    const float pad = circle.filled ? 0.0f : circle.thickness * 0.5f;
    const float cullR = std::max(rx, ry) + pad;

    if (TryCull(worldCenter.x - cullR, worldCenter.y - cullR, 2.0f * cullR, 2.0f * cullR, circle.isFixed)) {
      return;
    }

    const glm::vec2 screenCenter = ToScreenPoint(worldCenter, circle.isFixed);

    auto& cmd = renderQueue_->EmplaceCircle(static_cast<unsigned int>(circle.layer), circle.position.y, nullptr,
                                            circle.blendMode);
    cmd.center = {screenCenter.x, screenCenter.y};
    cmd.radiusX = ToScreenLength(rx, circle.isFixed);
    cmd.radiusY = ToScreenLength(ry, circle.isFixed);
    cmd.rotation = transform.rotation;
    cmd.thickness = ToScreenLength(circle.thickness, circle.isFixed);
    cmd.color = SDL_Color{circle.color.r, circle.color.g, circle.color.b, circle.color.a};
    cmd.blendMode = octarine::ToSdlBlendMode(circle.blendMode);
    cmd.filled = circle.filled;
  }
};
