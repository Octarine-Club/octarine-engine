#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

#include "Components/BoxColliderComponent.h"
#include "Components/CameraComponents.h"
#include "Components/CapsuleColliderComponent.h"
#include "Components/CircleColliderComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/LineColliderComponent.h"
#include "Components/TriangleColliderComponent.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Engine/EngineContext.h"
#include "General/Constants.h"
#include "General/Rotation2D.h"

class DrawColliderSystem {
 public:
  void operator()(const ContextFacade& context, const GlobalTransformComponent& transform,
                  const BoxColliderComponent& collider) const {
    auto* registry = context.GetRegistry();
    DrawBox(registry->Get<EngineContext>().sdlRenderer, registry->Get<CameraComponent>(), transform, collider);
  }

  void operator()(const ContextFacade& context, const GlobalTransformComponent& transform,
                  const CircleColliderComponent& collider) const {
    auto* registry = context.GetRegistry();
    DrawCircle(registry->Get<EngineContext>().sdlRenderer, registry->Get<CameraComponent>(), transform, collider);
  }

  void operator()(const ContextFacade& context, const GlobalTransformComponent& transform,
                  const CapsuleColliderComponent& collider) const {
    auto* registry = context.GetRegistry();
    DrawCapsule(registry->Get<EngineContext>().sdlRenderer, registry->Get<CameraComponent>(), transform, collider);
  }

  void operator()(const ContextFacade& context, const GlobalTransformComponent& transform,
                  const TriangleColliderComponent& collider) const {
    auto* registry = context.GetRegistry();
    DrawTriangle(registry->Get<EngineContext>().sdlRenderer, registry->Get<CameraComponent>(), transform, collider);
  }

  void operator()(const ContextFacade& context, const GlobalTransformComponent& transform,
                  const LineColliderComponent& collider) const {
    auto* registry = context.GetRegistry();
    DrawLine(registry->Get<EngineContext>().sdlRenderer, registry->Get<CameraComponent>(), transform, collider);
  }

  void Draw(Registry* registry) {
    EnsureQueries(registry);
    boxQuery_->Update();
    circleQuery_->Update();
    capsuleQuery_->Update();
    triangleQuery_->Update();
    lineQuery_->Update();

    boxQuery_->ForEach(*this);
    circleQuery_->ForEach(*this);
    capsuleQuery_->ForEach(*this);
    triangleQuery_->ForEach(*this);
    lineQuery_->ForEach(*this);
  }

 private:
  static constexpr int kBoxCorners = 4;
  static constexpr int kOutlinePoints = kBoxCorners + 1;
  static constexpr int kCircleSegments = 32;
  static constexpr int kCapArcSegments = 16;

  [[nodiscard]] static float EffectiveZoom(const CameraComponent& cameraComp) {
    return cameraComp.zoom > 0.001f ? cameraComp.zoom : 1.0f;
  }

  [[nodiscard]] static glm::vec2 TransformLocalPoint(const glm::vec2 localOffset,
                                                     const GlobalTransformComponent& transform,
                                                     const octarine::Rotation2D rot) {
    return octarine::RotateAround(transform.position + localOffset * transform.scale,
                                  transform.position + transform.pivot, rot);
  }

  [[nodiscard]] static glm::vec2 ToScreenPoint(const glm::vec2 worldPt, const octarine::Rect& camera, const float zoom,
                                               const bool isFixed) {
    return isFixed ? worldPt : (worldPt - glm::vec2{camera.x, camera.y}) * zoom;
  }

  [[nodiscard]] static float ToScreenLength(const float worldLen, const float zoom, const bool isFixed) {
    return isFixed ? worldLen : worldLen * zoom;
  }

  static void DrawBox(SDL_Renderer* renderer, const CameraComponent& cameraComp,
                      const GlobalTransformComponent& transform, const BoxColliderComponent& collider) {
    const auto& camera = cameraComp.viewport;
    const float zoom = EffectiveZoom(cameraComp);

    float w = static_cast<float>(collider.width) * std::abs(transform.scale.x);
    float h = static_cast<float>(collider.height) * std::abs(transform.scale.y);
    float hx = w * 0.5f;
    float hy = h * 0.5f;

    glm::vec2 centre = {transform.position.x + collider.offset.x * transform.scale.x + hx,
                        transform.position.y + collider.offset.y * transform.scale.y + hy};
    glm::vec2 origin = transform.position + transform.pivot;

    if (!collider.isFixed) {
      const glm::vec2 cameraOffset = {camera.x, camera.y};
      centre = (centre - cameraOffset) * zoom;
      origin = (origin - cameraOffset) * zoom;
      hx *= zoom;
      hy *= zoom;
      w *= zoom;
      h *= zoom;
    }

    SDL_SetRenderDrawColor(renderer, Constants::kUint8Max, 0, 0, Constants::kUint8Max);

    const octarine::Rotation2D rot = octarine::Rotation2D::FromRadians(transform.rotation);
    if (rot.IsIdentity()) {
      const SDL_FRect rect = {centre.x - hx, centre.y - hy, w, h};
      SDL_RenderRect(renderer, &rect);
      return;
    }

    centre = octarine::RotateAround(centre, origin, rot);
    DrawOrientedBox(renderer, centre, {hx, hy}, rot);
  }

  static void DrawCircle(SDL_Renderer* renderer, const CameraComponent& cameraComp,
                         const GlobalTransformComponent& transform, const CircleColliderComponent& collider) {
    const float zoom = EffectiveZoom(cameraComp);
    const octarine::Rotation2D rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 worldCentre = TransformLocalPoint(collider.offset, transform, rot);
    const float scaleMax = std::max(std::abs(transform.scale.x), std::abs(transform.scale.y));
    const float worldR = std::max(0.0f, collider.radius) * scaleMax;

    const glm::vec2 centre = ToScreenPoint(worldCentre, cameraComp.viewport, zoom, collider.isFixed);
    const float r = ToScreenLength(worldR, zoom, collider.isFixed);

    SDL_SetRenderDrawColor(renderer, Constants::kUint8Max, 0, 0, Constants::kUint8Max);
    DrawCircleOutline(renderer, centre, r);
  }

  static void DrawCapsule(SDL_Renderer* renderer, const CameraComponent& cameraComp,
                          const GlobalTransformComponent& transform, const CapsuleColliderComponent& collider) {
    const float zoom = EffectiveZoom(cameraComp);
    const octarine::Rotation2D rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 p1World = TransformLocalPoint(collider.start, transform, rot);
    const glm::vec2 p2World = TransformLocalPoint(collider.end, transform, rot);
    const float scaleMax = std::max(std::abs(transform.scale.x), std::abs(transform.scale.y));
    const float worldR = std::max(0.0f, collider.radius) * scaleMax;

    const glm::vec2 p1 = ToScreenPoint(p1World, cameraComp.viewport, zoom, collider.isFixed);
    const glm::vec2 p2 = ToScreenPoint(p2World, cameraComp.viewport, zoom, collider.isFixed);
    const float r = ToScreenLength(worldR, zoom, collider.isFixed);

    SDL_SetRenderDrawColor(renderer, Constants::kUint8Max, 0, 0, Constants::kUint8Max);
    const glm::vec2 spine = p2 - p1;
    const float len = glm::length(spine);
    if (len <= 1e-4f) {
      DrawCircleOutline(renderer, p1, r);
      return;
    }
    DrawCapsuleOutline(renderer, p1, p2, spine / len, r);
  }

  static void DrawTriangle(SDL_Renderer* renderer, const CameraComponent& cameraComp,
                           const GlobalTransformComponent& transform, const TriangleColliderComponent& collider) {
    const float zoom = EffectiveZoom(cameraComp);
    const octarine::Rotation2D rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 s1 =
        ToScreenPoint(TransformLocalPoint(collider.p1, transform, rot), cameraComp.viewport, zoom, collider.isFixed);
    const glm::vec2 s2 =
        ToScreenPoint(TransformLocalPoint(collider.p2, transform, rot), cameraComp.viewport, zoom, collider.isFixed);
    const glm::vec2 s3 =
        ToScreenPoint(TransformLocalPoint(collider.p3, transform, rot), cameraComp.viewport, zoom, collider.isFixed);

    SDL_SetRenderDrawColor(renderer, Constants::kUint8Max, 0, 0, Constants::kUint8Max);
    const SDL_FPoint pts[4] = {{s1.x, s1.y}, {s2.x, s2.y}, {s3.x, s3.y}, {s1.x, s1.y}};
    SDL_RenderLines(renderer, pts, 4);
  }

  static void DrawLine(SDL_Renderer* renderer, const CameraComponent& cameraComp,
                       const GlobalTransformComponent& transform, const LineColliderComponent& collider) {
    const float zoom = EffectiveZoom(cameraComp);
    const octarine::Rotation2D rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 s1 =
        ToScreenPoint(TransformLocalPoint(collider.start, transform, rot), cameraComp.viewport, zoom, collider.isFixed);
    const glm::vec2 s2 =
        ToScreenPoint(TransformLocalPoint(collider.end, transform, rot), cameraComp.viewport, zoom, collider.isFixed);

    SDL_SetRenderDrawColor(renderer, Constants::kUint8Max, 0, 0, Constants::kUint8Max);
    SDL_RenderLine(renderer, s1.x, s1.y, s2.x, s2.y);
  }

  static void DrawOrientedBox(SDL_Renderer* renderer, const glm::vec2 centre, const glm::vec2 half,
                              const octarine::Rotation2D rot) {
    const glm::vec2 offsets[kBoxCorners] = {{-half.x, -half.y}, {half.x, -half.y}, {half.x, half.y}, {-half.x, half.y}};
    SDL_FPoint corners[kOutlinePoints];
    for (int i = 0; i < kBoxCorners; ++i) {
      const glm::vec2 p = centre + octarine::Rotate(offsets[i], rot);
      corners[i] = {p.x, p.y};
    }
    corners[kBoxCorners] = corners[0];
    SDL_RenderLines(renderer, corners, kOutlinePoints);
  }

  static void DrawCircleOutline(SDL_Renderer* renderer, const glm::vec2 centre, const float r) {
    SDL_FPoint pts[kCircleSegments + 1];
    const float step = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kCircleSegments);
    for (int i = 0; i < kCircleSegments; ++i) {
      const float angle = static_cast<float>(i) * step;
      pts[i] = {centre.x + r * std::cos(angle), centre.y + r * std::sin(angle)};
    }
    pts[kCircleSegments] = pts[0];
    SDL_RenderLines(renderer, pts, kCircleSegments + 1);
  }

  static void DrawCapsuleOutline(SDL_Renderer* renderer, const glm::vec2 p1, const glm::vec2 p2, const glm::vec2 dir,
                                 const float r) {
    const glm::vec2 perp{-dir.y, dir.x};
    constexpr int kTotalPts = (kCapArcSegments + 1) * 2 + 1;
    SDL_FPoint pts[kTotalPts];
    int idx = 0;
    const float step = std::numbers::pi_v<float> / static_cast<float>(kCapArcSegments);
    for (int i = 0; i <= kCapArcSegments; ++i) {
      const float angle = static_cast<float>(i) * step;
      const glm::vec2 offset = perp * std::cos(angle) + dir * std::sin(angle);
      const glm::vec2 pt = p2 + offset * r;
      pts[idx++] = {pt.x, pt.y};
    }
    for (int i = 0; i <= kCapArcSegments; ++i) {
      const float angle = static_cast<float>(i) * step;
      const glm::vec2 offset = -perp * std::cos(angle) - dir * std::sin(angle);
      const glm::vec2 pt = p1 + offset * r;
      pts[idx++] = {pt.x, pt.y};
    }
    pts[idx++] = pts[0];
    SDL_RenderLines(renderer, pts, idx);
  }

  void EnsureQueries(Registry* registry) {
    if (boxQuery_) return;
    boxQuery_ = registry->CreateQuery<GlobalTransformComponent, BoxColliderComponent>();
    circleQuery_ = registry->CreateQuery<GlobalTransformComponent, CircleColliderComponent>();
    capsuleQuery_ = registry->CreateQuery<GlobalTransformComponent, CapsuleColliderComponent>();
    triangleQuery_ = registry->CreateQuery<GlobalTransformComponent, TriangleColliderComponent>();
    lineQuery_ = registry->CreateQuery<GlobalTransformComponent, LineColliderComponent>();
  }

  std::unique_ptr<ComponentQuery<GlobalTransformComponent, BoxColliderComponent>> boxQuery_;
  std::unique_ptr<ComponentQuery<GlobalTransformComponent, CircleColliderComponent>> circleQuery_;
  std::unique_ptr<ComponentQuery<GlobalTransformComponent, CapsuleColliderComponent>> capsuleQuery_;
  std::unique_ptr<ComponentQuery<GlobalTransformComponent, TriangleColliderComponent>> triangleQuery_;
  std::unique_ptr<ComponentQuery<GlobalTransformComponent, LineColliderComponent>> lineQuery_;
};
