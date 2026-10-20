#pragma once

#include <algorithm>
#include <cmath>

#include "Components/CapsulePrimitiveComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "General/Rotation2D.h"
#include "Systems/RenderPrimitiveSystem.h"

class RenderCapsulePrimitiveSystem : public RenderPrimitiveBase {
 public:
  void operator()(const CapsulePrimitiveComponent& capsule, const GlobalTransformComponent& transform) const {
    const auto rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 p1World = TransformLocalPoint(capsule.start, transform, rot);
    const glm::vec2 p2World = TransformLocalPoint(capsule.end, transform, rot);

    const float r = capsule.radius * std::max(std::abs(transform.scale.x), std::abs(transform.scale.y));
    const float pad = r + (capsule.filled ? 0.0f : capsule.thickness * 0.5f);
    const float minX = std::min(p1World.x, p2World.x) - pad;
    const float minY = std::min(p1World.y, p2World.y) - pad;
    const float maxX = std::max(p1World.x, p2World.x) + pad;
    const float maxY = std::max(p1World.y, p2World.y) + pad;

    if (TryCull(minX, minY, maxX - minX, maxY - minY, capsule.isFixed)) {
      return;
    }

    const glm::vec2 s1 = ToScreenPoint(p1World, capsule.isFixed);
    const glm::vec2 s2 = ToScreenPoint(p2World, capsule.isFixed);

    auto& cmd = renderQueue_->EmplaceCapsule(static_cast<unsigned int>(capsule.layer), capsule.start.y, nullptr,
                                             capsule.blendMode);
    cmd.start = {s1.x, s1.y};
    cmd.end = {s2.x, s2.y};
    cmd.radius = ToScreenLength(r, capsule.isFixed);
    cmd.thickness = ToScreenLength(capsule.thickness, capsule.isFixed);
    cmd.color = SDL_Color{capsule.color.r, capsule.color.g, capsule.color.b, capsule.color.a};
    cmd.blendMode = octarine::ToSdlBlendMode(capsule.blendMode);
    cmd.filled = capsule.filled;
  }
};
