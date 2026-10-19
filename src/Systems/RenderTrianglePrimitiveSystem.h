#pragma once

#include <algorithm>

#include "Components/GlobalTransformComponent.h"
#include "Components/TrianglePrimitiveComponent.h"
#include "General/Rotation2D.h"
#include "Systems/RenderPrimitiveSystem.h"

class RenderTrianglePrimitiveSystem : public RenderPrimitiveBase {
 public:
  void operator()(const TrianglePrimitiveComponent& triangle, const GlobalTransformComponent& transform) const {
    const auto rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 p1World = TransformLocalPoint(triangle.p1, transform, rot);
    const glm::vec2 p2World = TransformLocalPoint(triangle.p2, transform, rot);
    const glm::vec2 p3World = TransformLocalPoint(triangle.p3, transform, rot);

    const float pad = triangle.filled ? 0.0f : triangle.thickness * 0.5f;
    const float minX = std::min({p1World.x, p2World.x, p3World.x}) - pad;
    const float minY = std::min({p1World.y, p2World.y, p3World.y}) - pad;
    const float maxX = std::max({p1World.x, p2World.x, p3World.x}) + pad;
    const float maxY = std::max({p1World.y, p2World.y, p3World.y}) + pad;

    if (TryCull(minX, minY, maxX - minX, maxY - minY, triangle.isFixed)) {
      return;
    }

    const glm::vec2 s1 = ToScreenPoint(p1World, triangle.isFixed);
    const glm::vec2 s2 = ToScreenPoint(p2World, triangle.isFixed);
    const glm::vec2 s3 = ToScreenPoint(p3World, triangle.isFixed);

    auto& cmd = renderQueue_->EmplaceTriangle(static_cast<unsigned int>(triangle.layer), triangle.p1.y, nullptr,
                                              triangle.blendMode);
    cmd.p1 = {s1.x, s1.y};
    cmd.p2 = {s2.x, s2.y};
    cmd.p3 = {s3.x, s3.y};
    cmd.thickness = ToScreenLength(triangle.thickness, triangle.isFixed);
    cmd.color = SDL_Color{triangle.color.r, triangle.color.g, triangle.color.b, triangle.color.a};
    cmd.blendMode = octarine::ToSdlBlendMode(triangle.blendMode);
    cmd.filled = triangle.filled;
  }
};
