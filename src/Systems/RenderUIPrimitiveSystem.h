#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>

#include "Components/CapsulePrimitiveComponent.h"
#include "Components/CirclePrimitiveComponent.h"
#include "Components/LinePrimitiveComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "Components/TrianglePrimitiveComponent.h"
#include "Components/UIRectComponent.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "General/BlendMode.h"
#include "Renderer/RenderQueue.h"

namespace octarine::ui {

struct ResolvedCircle {
  glm::vec2 center{};
  float radiusX{};
  float radiusY{};
};

struct ResolvedCapsule {
  glm::vec2 start{};
  glm::vec2 end{};
  float radius{};
};

struct ResolvedTriangle {
  glm::vec2 p1{};
  glm::vec2 p2{};
  glm::vec2 p3{};
};

struct ResolvedLine {
  glm::vec2 start{};
  glm::vec2 end{};
};

// When `explicitRadius <= 0`, the circle/ellipse fills the resolved UIRectComponent. `offset`
// shifts the center relative to `rect.Center()`.
[[nodiscard]] inline ResolvedCircle ResolveCircle(const UIRectComponent& rect, const glm::vec2 offset,
                                                  const float explicitRadius) {
  const glm::vec2 center = rect.Center() + offset;
  if (explicitRadius > 0.0f) {
    return {center, explicitRadius, explicitRadius};
  }
  static constexpr float kHalf = 0.5f;
  return {center, std::max(0.0f, rect.Width() * kHalf), std::max(0.0f, rect.Height() * kHalf)};
}

// When `localStart` and `localEnd` are both zero, the capsule spans the major axis of `rect` as a
// pill whose cap radius defaults to half the minor dimension. Explicit endpoints are interpreted as
// offsets from `(rect.left, rect.top)`.
[[nodiscard]] inline ResolvedCapsule ResolveCapsule(const UIRectComponent& rect, const glm::vec2 localStart,
                                                    const glm::vec2 localEnd, const float explicitRadius) {
  static constexpr float kHalf = 0.5f;
  const float w = std::max(0.0f, rect.Width());
  const float h = std::max(0.0f, rect.Height());
  const float r = explicitRadius > 0.0f ? explicitRadius : std::min(w, h) * kHalf;
  if (localStart != glm::vec2{0.0f, 0.0f} || localEnd != glm::vec2{0.0f, 0.0f}) {
    const glm::vec2 origin{rect.left, rect.top};
    return {origin + localStart, origin + localEnd, r};
  }
  const glm::vec2 center = rect.Center();
  if (w >= h) {
    return {{std::min(center.x, rect.left + r), center.y}, {std::max(center.x, rect.right - r), center.y}, r};
  }
  return {{center.x, std::min(center.y, rect.top + r)}, {center.x, std::max(center.y, rect.bottom - r)}, r};
}

// When all three vertices are zero, inscribes an upward-pointing isosceles triangle inside `rect`.
// Explicit vertices are offsets from `(rect.left, rect.top)`.
[[nodiscard]] inline ResolvedTriangle ResolveTriangle(const UIRectComponent& rect, const glm::vec2 localP1,
                                                      const glm::vec2 localP2, const glm::vec2 localP3) {
  if (localP1 != glm::vec2{0.0f, 0.0f} || localP2 != glm::vec2{0.0f, 0.0f} || localP3 != glm::vec2{0.0f, 0.0f}) {
    const glm::vec2 origin{rect.left, rect.top};
    return {origin + localP1, origin + localP2, origin + localP3};
  }
  static constexpr float kHalf = 0.5f;
  const float midX = (rect.left + rect.right) * kHalf;
  return {{midX, rect.top}, {rect.left, rect.bottom}, {rect.right, rect.bottom}};
}

// When both endpoints are zero, draws a centered segment along the major axis of `rect` (e.g. a
// horizontal or vertical UI divider). Explicit endpoints are offsets from `(rect.left, rect.top)`.
[[nodiscard]] inline ResolvedLine ResolveLine(const UIRectComponent& rect, const glm::vec2 localStart,
                                              const glm::vec2 localEnd) {
  if (localStart != glm::vec2{0.0f, 0.0f} || localEnd != glm::vec2{0.0f, 0.0f}) {
    const glm::vec2 origin{rect.left, rect.top};
    return {origin + localStart, origin + localEnd};
  }
  const glm::vec2 center = rect.Center();
  if (rect.Width() >= rect.Height()) {
    return {{rect.left, center.y}, {rect.right, center.y}};
  }
  return {{center.x, rect.top}, {center.x, rect.bottom}};
}

}  // namespace octarine::ui

// Renders primitive components whose bounds are driven by UIRectComponent (layout-driven UI shapes).
// Registered after UILayoutSystem. World primitives without UIRectComponent continue through the
// parallel Render*PrimitiveSystem paths unchanged; the queries are disjoint because UI layout nodes
// carry no GlobalTransformComponent.
class RenderUIPrimitiveSystem {
 public:
  void operator()(const ContextFacade& ctx) {
    auto* registry = ctx.GetRegistry();
    EnsureQueries(registry);
    squareQuery_->Update();
    circleQuery_->Update();
    capsuleQuery_->Update();
    triangleQuery_->Update();
    lineQuery_->Update();

    auto& renderQueue = registry->Get<RenderQueue>();
    squareQuery_->ForEach([&](const UIRectComponent& rect, const SquarePrimitiveComponent& square) {
      EmplaceSquare(renderQueue, rect, square);
    });
    circleQuery_->ForEach([&](const UIRectComponent& rect, const CirclePrimitiveComponent& circle) {
      EmplaceCircle(renderQueue, rect, circle);
    });
    capsuleQuery_->ForEach([&](const UIRectComponent& rect, const CapsulePrimitiveComponent& capsule) {
      EmplaceCapsule(renderQueue, rect, capsule);
    });
    triangleQuery_->ForEach([&](const UIRectComponent& rect, const TrianglePrimitiveComponent& triangle) {
      EmplaceTriangle(renderQueue, rect, triangle);
    });
    lineQuery_->ForEach(
        [&](const UIRectComponent& rect, const LinePrimitiveComponent& line) { EmplaceLine(renderQueue, rect, line); });
  }

  void operator()(const ContextFacade& ctx, const UIRectComponent& rect, const SquarePrimitiveComponent& square) const {
    EmplaceSquare(ctx.GetRegistry()->Get<RenderQueue>(), rect, square);
  }

  void operator()(const ContextFacade& ctx, const UIRectComponent& rect, const CirclePrimitiveComponent& circle) const {
    EmplaceCircle(ctx.GetRegistry()->Get<RenderQueue>(), rect, circle);
  }

  void operator()(const ContextFacade& ctx, const UIRectComponent& rect,
                  const CapsulePrimitiveComponent& capsule) const {
    EmplaceCapsule(ctx.GetRegistry()->Get<RenderQueue>(), rect, capsule);
  }

  void operator()(const ContextFacade& ctx, const UIRectComponent& rect,
                  const TrianglePrimitiveComponent& triangle) const {
    EmplaceTriangle(ctx.GetRegistry()->Get<RenderQueue>(), rect, triangle);
  }

  void operator()(const ContextFacade& ctx, const UIRectComponent& rect, const LinePrimitiveComponent& line) const {
    EmplaceLine(ctx.GetRegistry()->Get<RenderQueue>(), rect, line);
  }

 private:
  static void EmplaceSquare(RenderQueue& renderQueue, const UIRectComponent& rect,
                            const SquarePrimitiveComponent& square) {
    auto& cmd = renderQueue.EmplaceSquare(static_cast<unsigned int>(rect.layer), rect.top, nullptr, square.blendMode);
    cmd.destRect = {rect.left, rect.top, rect.Width(), rect.Height()};
    cmd.color = SDL_Color{square.color.r, square.color.g, square.color.b, square.color.a};
    cmd.rotation = 0.0;
    cmd.blendMode = octarine::ToSdlBlendMode(square.blendMode);
  }

  static void EmplaceCircle(RenderQueue& renderQueue, const UIRectComponent& rect,
                            const CirclePrimitiveComponent& circle) {
    const auto resolved = octarine::ui::ResolveCircle(rect, circle.position, circle.radius);
    auto& cmd = renderQueue.EmplaceCircle(static_cast<unsigned int>(rect.layer), rect.top, nullptr, circle.blendMode);
    cmd.center = {resolved.center.x, resolved.center.y};
    cmd.radiusX = resolved.radiusX;
    cmd.radiusY = resolved.radiusY;
    cmd.rotation = 0.0f;
    cmd.thickness = circle.thickness;
    cmd.color = SDL_Color{circle.color.r, circle.color.g, circle.color.b, circle.color.a};
    cmd.blendMode = octarine::ToSdlBlendMode(circle.blendMode);
    cmd.filled = circle.filled;
  }

  static void EmplaceCapsule(RenderQueue& renderQueue, const UIRectComponent& rect,
                             const CapsulePrimitiveComponent& capsule) {
    const auto resolved = octarine::ui::ResolveCapsule(rect, capsule.start, capsule.end, capsule.radius);
    auto& cmd = renderQueue.EmplaceCapsule(static_cast<unsigned int>(rect.layer), rect.top, nullptr, capsule.blendMode);
    cmd.start = {resolved.start.x, resolved.start.y};
    cmd.end = {resolved.end.x, resolved.end.y};
    cmd.radius = resolved.radius;
    cmd.thickness = capsule.thickness;
    cmd.color = SDL_Color{capsule.color.r, capsule.color.g, capsule.color.b, capsule.color.a};
    cmd.blendMode = octarine::ToSdlBlendMode(capsule.blendMode);
    cmd.filled = capsule.filled;
  }

  static void EmplaceTriangle(RenderQueue& renderQueue, const UIRectComponent& rect,
                              const TrianglePrimitiveComponent& triangle) {
    const auto resolved = octarine::ui::ResolveTriangle(rect, triangle.p1, triangle.p2, triangle.p3);
    auto& cmd =
        renderQueue.EmplaceTriangle(static_cast<unsigned int>(rect.layer), rect.top, nullptr, triangle.blendMode);
    cmd.p1 = {resolved.p1.x, resolved.p1.y};
    cmd.p2 = {resolved.p2.x, resolved.p2.y};
    cmd.p3 = {resolved.p3.x, resolved.p3.y};
    cmd.thickness = triangle.thickness;
    cmd.color = SDL_Color{triangle.color.r, triangle.color.g, triangle.color.b, triangle.color.a};
    cmd.blendMode = octarine::ToSdlBlendMode(triangle.blendMode);
    cmd.filled = triangle.filled;
  }

  static void EmplaceLine(RenderQueue& renderQueue, const UIRectComponent& rect, const LinePrimitiveComponent& line) {
    const auto resolved = octarine::ui::ResolveLine(rect, line.start, line.end);
    auto& cmd = renderQueue.EmplaceLine(static_cast<unsigned int>(rect.layer), rect.top, nullptr, line.blendMode);
    cmd.start = {resolved.start.x, resolved.start.y};
    cmd.end = {resolved.end.x, resolved.end.y};
    cmd.thickness = line.thickness;
    cmd.color = SDL_Color{line.color.r, line.color.g, line.color.b, line.color.a};
    cmd.blendMode = octarine::ToSdlBlendMode(line.blendMode);
  }

  void EnsureQueries(Registry* registry) {
    if (squareQuery_) return;
    squareQuery_ = registry->CreateQuery<UIRectComponent, SquarePrimitiveComponent>();
    circleQuery_ = registry->CreateQuery<UIRectComponent, CirclePrimitiveComponent>();
    capsuleQuery_ = registry->CreateQuery<UIRectComponent, CapsulePrimitiveComponent>();
    triangleQuery_ = registry->CreateQuery<UIRectComponent, TrianglePrimitiveComponent>();
    lineQuery_ = registry->CreateQuery<UIRectComponent, LinePrimitiveComponent>();
  }

  std::unique_ptr<ComponentQuery<UIRectComponent, SquarePrimitiveComponent>> squareQuery_;
  std::unique_ptr<ComponentQuery<UIRectComponent, CirclePrimitiveComponent>> circleQuery_;
  std::unique_ptr<ComponentQuery<UIRectComponent, CapsulePrimitiveComponent>> capsuleQuery_;
  std::unique_ptr<ComponentQuery<UIRectComponent, TrianglePrimitiveComponent>> triangleQuery_;
  std::unique_ptr<ComponentQuery<UIRectComponent, LinePrimitiveComponent>> lineQuery_;
};
