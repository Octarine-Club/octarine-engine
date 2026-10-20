#pragma once

#include <algorithm>

#include "Components/GlobalTransformComponent.h"
#include "Components/LinePrimitiveComponent.h"
#include "General/Rotation2D.h"
#include "Systems/RenderPrimitiveSystem.h"

class RenderLinePrimitiveSystem : public RenderPrimitiveBase {
 public:
  void operator()(const LinePrimitiveComponent& line, const GlobalTransformComponent& transform) const {
    const auto rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 p1World = TransformLocalPoint(line.start, transform, rot);
    const glm::vec2 p2World = TransformLocalPoint(line.end, transform, rot);

    const float pad = line.thickness * 0.5f;
    const float minX = std::min(p1World.x, p2World.x) - pad;
    const float minY = std::min(p1World.y, p2World.y) - pad;
    const float maxX = std::max(p1World.x, p2World.x) + pad;
    const float maxY = std::max(p1World.y, p2World.y) + pad;

    if (TryCull(minX, minY, maxX - minX, maxY - minY, line.isFixed)) {
      return;
    }

    const glm::vec2 s1 = ToScreenPoint(p1World, line.isFixed);
    const glm::vec2 s2 = ToScreenPoint(p2World, line.isFixed);

    auto& cmd = renderQueue_->EmplaceLine(static_cast<unsigned int>(line.layer), line.start.y, nullptr, line.blendMode);
    cmd.start = {s1.x, s1.y};
    cmd.end = {s2.x, s2.y};
    cmd.thickness = ToScreenLength(line.thickness, line.isFixed);
    cmd.color = SDL_Color{line.color.r, line.color.g, line.color.b, line.color.a};
    cmd.blendMode = octarine::ToSdlBlendMode(line.blendMode);
  }
};
