#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Components/BoxColliderComponent.h"
#include "Components/CapsuleColliderComponent.h"
#include "Components/CircleColliderComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/LineColliderComponent.h"
#include "Components/TriangleColliderComponent.h"
#include "ECS/Entity.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Engine/EngineContext.h"
#include "EventBus/EventBus.h"
#include "Events/CollisionBatchEvent.h"
#include "Events/CollisionExitBatchEvent.h"
#include "General/PerfUtils.h"
#include "General/Rotation2D.h"
#include "General/ThreadPool.h"

constexpr int kMaxDimensions = 2;
// These values can be tuned for better performance.
constexpr int kMaxRecursionDepth = 64;
constexpr int kBruteforceCutoff = 32;

enum class ColliderShape : uint8_t {
  Box = 0,
  Circle = 1,
  Capsule = 2,
  Triangle = 3,
  Line = 4,
};

namespace octarine::collision {

[[nodiscard]] inline float Cross2D(const glm::vec2 u, const glm::vec2 v) { return u.x * v.y - u.y * v.x; }

[[nodiscard]] inline float PointSegmentDistanceSq(const glm::vec2 p, const glm::vec2 a, const glm::vec2 b) {
  const glm::vec2 ab = b - a;
  const float lenSq = glm::dot(ab, ab);
  float t = 0.0f;
  if (lenSq > 1e-12f) {
    t = std::clamp(glm::dot(p - a, ab) / lenSq, 0.0f, 1.0f);
  }
  const glm::vec2 diff = p - (a + ab * t);
  return glm::dot(diff, diff);
}

[[nodiscard]] inline bool OppositeSigns(const float a, const float b) {
  return (a > 0.0f && b < 0.0f) || (a < 0.0f && b > 0.0f);
}

[[nodiscard]] inline bool SegmentsStrictlyCross(const glm::vec2 a1, const glm::vec2 a2, const glm::vec2 b1,
                                                const glm::vec2 b2) {
  const glm::vec2 da = a2 - a1;
  const glm::vec2 db = b2 - b1;
  const float d1 = Cross2D(db, a1 - b1);
  const float d2 = Cross2D(db, a2 - b1);
  const float d3 = Cross2D(da, b1 - a1);
  const float d4 = Cross2D(da, b2 - a1);
  return OppositeSigns(d1, d2) && OppositeSigns(d3, d4);
}

[[nodiscard]] inline float SegmentSegmentDistanceSq(const glm::vec2 a1, const glm::vec2 a2, const glm::vec2 b1,
                                                    const glm::vec2 b2) {
  if (SegmentsStrictlyCross(a1, a2, b1, b2)) return 0.0f;
  return std::min({PointSegmentDistanceSq(a1, b1, b2), PointSegmentDistanceSq(a2, b1, b2),
                   PointSegmentDistanceSq(b1, a1, a2), PointSegmentDistanceSq(b2, a1, a2)});
}

[[nodiscard]] inline float PointAabbDistanceSq(const glm::vec2 p, const float hx, const float hy) {
  const float dx = std::max(std::abs(p.x) - hx, 0.0f);
  const float dy = std::max(std::abs(p.y) - hy, 0.0f);
  return dx * dx + dy * dy;
}

[[nodiscard]] inline float SegmentAabbDistanceSq(const glm::vec2 p1, const glm::vec2 p2, const float hx,
                                                 const float hy) {
  if (PointAabbDistanceSq(p1, hx, hy) == 0.0f || PointAabbDistanceSq(p2, hx, hy) == 0.0f) {
    return 0.0f;
  }
  const glm::vec2 c0{-hx, -hy};
  const glm::vec2 c1{hx, -hy};
  const glm::vec2 c2{hx, hy};
  const glm::vec2 c3{-hx, hy};
  return std::min({SegmentSegmentDistanceSq(p1, p2, c0, c1), SegmentSegmentDistanceSq(p1, p2, c1, c2),
                   SegmentSegmentDistanceSq(p1, p2, c2, c3), SegmentSegmentDistanceSq(p1, p2, c3, c0)});
}

[[nodiscard]] inline bool PointInTriangle(const glm::vec2 p, const glm::vec2 v0, const glm::vec2 v1,
                                          const glm::vec2 v2) {
  const float d1 = Cross2D(v1 - v0, p - v0);
  const float d2 = Cross2D(v2 - v1, p - v1);
  const float d3 = Cross2D(v0 - v2, p - v2);
  const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
  const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
  return !(hasNeg && hasPos);
}

[[nodiscard]] inline float SegmentTriangleDistanceSq(const glm::vec2 p1, const glm::vec2 p2, const glm::vec2 v0,
                                                     const glm::vec2 v1, const glm::vec2 v2) {
  if (PointInTriangle(p1, v0, v1, v2) || PointInTriangle(p2, v0, v1, v2)) {
    return 0.0f;
  }
  return std::min({SegmentSegmentDistanceSq(p1, p2, v0, v1), SegmentSegmentDistanceSq(p1, p2, v1, v2),
                   SegmentSegmentDistanceSq(p1, p2, v2, v0)});
}

[[nodiscard]] inline glm::vec2 ProjectTriangle(const glm::vec2 v0, const glm::vec2 v1, const glm::vec2 v2,
                                               const glm::vec2 axis) {
  const float p0 = glm::dot(v0, axis);
  const float p1 = glm::dot(v1, axis);
  const float p2 = glm::dot(v2, axis);
  return {std::min({p0, p1, p2}), std::max({p0, p1, p2})};
}

[[nodiscard]] inline bool IntervalsOverlap(const glm::vec2 a, const glm::vec2 b) { return a.y >= b.x && b.y >= a.x; }

[[nodiscard]] inline bool SeparatedByEdge(const glm::vec2 e0, const glm::vec2 e1, const glm::vec2 a0,
                                          const glm::vec2 a1, const glm::vec2 a2, const glm::vec2 b0,
                                          const glm::vec2 b1, const glm::vec2 b2) {
  const glm::vec2 edge = e1 - e0;
  if (glm::dot(edge, edge) <= 1e-12f) return false;
  const glm::vec2 axis{-edge.y, edge.x};
  return !IntervalsOverlap(ProjectTriangle(a0, a1, a2, axis), ProjectTriangle(b0, b1, b2, axis));
}

[[nodiscard]] inline bool TrianglesIntersect(const glm::vec2 a0, const glm::vec2 a1, const glm::vec2 a2,
                                             const glm::vec2 b0, const glm::vec2 b1, const glm::vec2 b2) {
  if (std::abs(Cross2D(a1 - a0, a2 - a0)) <= 1e-6f) {
    return SegmentTriangleDistanceSq(a0, a1, b0, b1, b2) == 0.0f ||
           SegmentTriangleDistanceSq(a1, a2, b0, b1, b2) == 0.0f;
  }
  if (std::abs(Cross2D(b1 - b0, b2 - b0)) <= 1e-6f) {
    return SegmentTriangleDistanceSq(b0, b1, a0, a1, a2) == 0.0f ||
           SegmentTriangleDistanceSq(b1, b2, a0, a1, a2) == 0.0f;
  }
  return !SeparatedByEdge(a0, a1, a0, a1, a2, b0, b1, b2) && !SeparatedByEdge(a1, a2, a0, a1, a2, b0, b1, b2) &&
         !SeparatedByEdge(a2, a0, a0, a1, a2, b0, b1, b2) && !SeparatedByEdge(b0, b1, a0, a1, a2, b0, b1, b2) &&
         !SeparatedByEdge(b1, b2, a0, a1, a2, b0, b1, b2) && !SeparatedByEdge(b2, b0, a0, a1, a2, b0, b1, b2);
}

[[nodiscard]] inline bool ObbTriangleSeparatedOnAxis(const glm::vec2 obbCenter, const glm::vec2 obbHalf,
                                                     const glm::vec2 ax0, const glm::vec2 ax1, const glm::vec2 v0,
                                                     const glm::vec2 v1, const glm::vec2 v2, const glm::vec2 axis) {
  if (glm::dot(axis, axis) <= 1e-12f) return false;
  const float cProj = glm::dot(obbCenter, axis);
  const float rProj = obbHalf.x * std::abs(glm::dot(ax0, axis)) + obbHalf.y * std::abs(glm::dot(ax1, axis));
  return !IntervalsOverlap({cProj - rProj, cProj + rProj}, ProjectTriangle(v0, v1, v2, axis));
}

[[nodiscard]] inline bool ObbTriangleIntersects(const glm::vec2 obbCenter, const glm::vec2 obbHalf,
                                                const octarine::Rotation2D obbRot, const glm::vec2 v0,
                                                const glm::vec2 v1, const glm::vec2 v2) {
  const glm::vec2 ax0{obbRot.cos, obbRot.sin};
  const glm::vec2 ax1{-obbRot.sin, obbRot.cos};
  const glm::vec2 e0 = v1 - v0;
  const glm::vec2 e1 = v2 - v1;
  const glm::vec2 e2 = v0 - v2;
  return !ObbTriangleSeparatedOnAxis(obbCenter, obbHalf, ax0, ax1, v0, v1, v2, ax0) &&
         !ObbTriangleSeparatedOnAxis(obbCenter, obbHalf, ax0, ax1, v0, v1, v2, ax1) &&
         !ObbTriangleSeparatedOnAxis(obbCenter, obbHalf, ax0, ax1, v0, v1, v2, {-e0.y, e0.x}) &&
         !ObbTriangleSeparatedOnAxis(obbCenter, obbHalf, ax0, ax1, v0, v1, v2, {-e1.y, e1.x}) &&
         !ObbTriangleSeparatedOnAxis(obbCenter, obbHalf, ax0, ax1, v0, v1, v2, {-e2.y, e2.x});
}

}  // namespace octarine::collision

struct Box {
  Entity entity{};
  uint32_t entityMask{0};
  uint32_t collisionMask{0};
  // AABB enclosing the world-space shape — used by the median-cut broadphase.
  float minX{0.0f};
  float minY{0.0f};
  float maxX{0.0f};
  float maxY{0.0f};
  // Shape narrowphase payload:
  //   - Box:      (cx, cy) = center, (hx, hy) = half-extents, rot = orientation
  //   - Circle:   (cx, cy) = center, (hx, hy) = center, rot.cos = radius
  //   - Capsule:  (cx, cy) = start,  (hx, hy) = end,    rot.cos = radius
  //   - Line:     (cx, cy) = start,  (hx, hy) = end,    rot.cos = 0
  //   - Triangle: (cx, cy) = v0,     (hx, hy) = v1,     (rot.cos, rot.sin) = v2
  float cx{0.0f};
  float cy{0.0f};
  float hx{0.0f};
  float hy{0.0f};
  octarine::Rotation2D rot{};
  ColliderShape shape{ColliderShape::Box};

  [[nodiscard]] bool IsCapsuleLike() const {
    return shape == ColliderShape::Circle || shape == ColliderShape::Capsule || shape == ColliderShape::Line;
  }

  [[nodiscard]] bool intersectsInDimension(const Box& other, const int dim) const {
    if (dim == 0) return !(maxX < other.minX || minX > other.maxX);
    if (dim == 1) return !(maxY < other.minY || minY > other.maxY);
    return false;
  }

  // SAT on the 4 face normals of two OBBs. Only invoked when at least one box is rotated;
  // axis-aligned pairs short-circuit on the AABB check above.
  [[nodiscard]] bool obbIntersects(const Box& other) const {
    const float ax0 = rot.cos, ay0 = rot.sin;
    const float ax1 = -rot.sin, ay1 = rot.cos;
    const float bx0 = other.rot.cos, by0 = other.rot.sin;
    const float bx1 = -other.rot.sin, by1 = other.rot.cos;
    const float dx = other.cx - cx;
    const float dy = other.cy - cy;

    const float axes[4][2] = {{ax0, ay0}, {ax1, ay1}, {bx0, by0}, {bx1, by1}};
    return std::ranges::all_of(axes, [&](const auto& axis) {
      const float ux = axis[0];
      const float uy = axis[1];
      const float aProj = hx * std::abs(ax0 * ux + ay0 * uy) + hy * std::abs(ax1 * ux + ay1 * uy);
      const float bProj = other.hx * std::abs(bx0 * ux + by0 * uy) + other.hy * std::abs(bx1 * ux + by1 * uy);
      const float distProj = std::abs(dx * ux + dy * uy);
      return distProj <= aProj + bProj;
    });
  }

  [[nodiscard]] static bool CapsuleObbIntersects(const Box& cap, const Box& obb) {
    const octarine::Rotation2D invRot{obb.rot.cos, -obb.rot.sin};
    const glm::vec2 obbCenter{obb.cx, obb.cy};
    const glm::vec2 localP1 = octarine::Rotate(glm::vec2{cap.cx, cap.cy} - obbCenter, invRot);
    const float rSq = cap.rot.cos * cap.rot.cos;
    if (cap.shape == ColliderShape::Circle) {
      return octarine::collision::PointAabbDistanceSq(localP1, obb.hx, obb.hy) <= rSq;
    }
    const glm::vec2 localP2 = octarine::Rotate(glm::vec2{cap.hx, cap.hy} - obbCenter, invRot);
    return octarine::collision::SegmentAabbDistanceSq(localP1, localP2, obb.hx, obb.hy) <= rSq;
  }

  [[nodiscard]] static bool CapsuleTriangleIntersects(const Box& cap, const Box& tri) {
    const float rSq = cap.rot.cos * cap.rot.cos;
    return octarine::collision::SegmentTriangleDistanceSq({cap.cx, cap.cy}, {cap.hx, cap.hy}, {tri.cx, tri.cy},
                                                          {tri.hx, tri.hy}, {tri.rot.cos, tri.rot.sin}) <= rSq;
  }

  [[nodiscard]] static bool CapsulePairIntersects(const Box& a, const Box& b) {
    const float rSum = a.rot.cos + b.rot.cos;
    if (a.shape == ColliderShape::Circle && b.shape == ColliderShape::Circle) {
      const float dx = b.cx - a.cx;
      const float dy = b.cy - a.cy;
      return (dx * dx + dy * dy) <= (rSum * rSum);
    }
    return octarine::collision::SegmentSegmentDistanceSq({a.cx, a.cy}, {a.hx, a.hy}, {b.cx, b.cy}, {b.hx, b.hy}) <=
           (rSum * rSum);
  }

  [[nodiscard]] static bool BoxTrianglePairIntersects(const Box& a, const Box& b) {
    if (a.shape == ColliderShape::Box) {
      return octarine::collision::ObbTriangleIntersects({a.cx, a.cy}, {a.hx, a.hy}, a.rot, {b.cx, b.cy}, {b.hx, b.hy},
                                                        {b.rot.cos, b.rot.sin});
    }
    return octarine::collision::ObbTriangleIntersects({b.cx, b.cy}, {b.hx, b.hy}, b.rot, {a.cx, a.cy}, {a.hx, a.hy},
                                                      {a.rot.cos, a.rot.sin});
  }

#if defined(__clang__) || defined(__GNUC__)
  [[gnu::noinline]]
#endif
  [[nodiscard]] bool
  narrowphaseIntersects(const Box& other) const {
    if (shape == ColliderShape::Box && other.shape == ColliderShape::Box) {
      return obbIntersects(other);
    }
    if (IsCapsuleLike() && other.IsCapsuleLike()) {
      return CapsulePairIntersects(*this, other);
    }
    if (IsCapsuleLike() && other.shape == ColliderShape::Box) return CapsuleObbIntersects(*this, other);
    if (shape == ColliderShape::Box && other.IsCapsuleLike()) return CapsuleObbIntersects(other, *this);
    if (IsCapsuleLike() && other.shape == ColliderShape::Triangle) return CapsuleTriangleIntersects(*this, other);
    if (shape == ColliderShape::Triangle && other.IsCapsuleLike()) return CapsuleTriangleIntersects(other, *this);
    if (shape == ColliderShape::Box || other.shape == ColliderShape::Box) {
      return BoxTrianglePairIntersects(*this, other);
    }
    return octarine::collision::TrianglesIntersect({cx, cy}, {hx, hy}, {rot.cos, rot.sin}, {other.cx, other.cy},
                                                   {other.hx, other.hy}, {other.rot.cos, other.rot.sin});
  }

  [[nodiscard]] bool containsPoint(const glm::vec2 pt) const {
    if (pt.x < minX || pt.x > maxX || pt.y < minY || pt.y > maxY) return false;
    if (shape == ColliderShape::Box) {
      if (rot.IsIdentity()) return true;
      const glm::vec2 local = octarine::Rotate(pt - glm::vec2{cx, cy}, {rot.cos, -rot.sin});
      return std::abs(local.x) <= hx && std::abs(local.y) <= hy;
    }
    if (IsCapsuleLike()) {
      return octarine::collision::PointSegmentDistanceSq(pt, {cx, cy}, {hx, hy}) <= (rot.cos * rot.cos);
    }
    return octarine::collision::PointInTriangle(pt, {cx, cy}, {hx, hy}, {rot.cos, rot.sin});
  }

  [[nodiscard]] bool intersects(const Box& other) const {
    if (entity == other.entity) return false;
    if (((collisionMask & other.entityMask) | (other.collisionMask & entityMask)) == 0U) {
      return false;
    }
    if (!intersectsInDimension(other, 0) || !intersectsInDimension(other, 1)) return false;
    if ((static_cast<uint8_t>(shape) | static_cast<uint8_t>(other.shape)) == 0U && rot.IsIdentity() &&
        other.rot.IsIdentity()) {
      return true;
    }
    return narrowphaseIntersects(other);
  }
};
static_assert(sizeof(Box) <= 64, "Box must fit in a single 64-byte cache line for KD-tree partitioning");

struct CollisionResult {
  std::vector<std::pair<Entity, Entity>> intersectingPairs;
  std::vector<Box> boxes;
};

struct Partitions {
  int leftEnd;
  int rightStart;
};

class KDTreeCollisionSystem {
 public:
  explicit KDTreeCollisionSystem(bool synchronous = false) : synchronous_(synchronous) {}

  void SetSynchronous(bool sync) { synchronous_ = sync; }
  [[nodiscard]] bool IsSynchronous() const { return synchronous_; }

  [[nodiscard]] static glm::vec2 TransformLocalPoint(const glm::vec2 localOffset,
                                                     const GlobalTransformComponent& transform,
                                                     const octarine::Rotation2D rot) {
    return octarine::RotateAround(transform.position + localOffset * transform.scale,
                                  transform.position + transform.pivot, rot);
  }

  static Box BuildBox(Entity entity, const GlobalTransformComponent& transform, const BoxColliderComponent& collider,
                      const EntityMaskComponent& entityMask) {
    const float w = static_cast<float>(collider.width) * std::abs(transform.scale.x);
    const float h = static_cast<float>(collider.height) * std::abs(transform.scale.y);
    const float hx = w * 0.5f;
    const float hy = h * 0.5f;

    // transform.position is top-left. apply collider offset (scaled).
    const float boxCx = transform.position.x + collider.offset.x * transform.scale.x + hx;
    const float boxCy = transform.position.y + collider.offset.y * transform.scale.y + hy;

    // FromRadians short-circuits the unrotated majority, and RotateAround collapses to a
    // copy for identity — so a world of axis-aligned colliders pays no trig at all here.
    const octarine::Rotation2D rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 centre = octarine::RotateAround({boxCx, boxCy}, transform.position + transform.pivot, rot);
    const glm::vec2 aabbHalf = octarine::RotatedHalfExtents({hx, hy}, rot);

    return {.entity = entity,
            .entityMask = static_cast<uint32_t>(entityMask.mask.to_ulong()),
            .collisionMask = static_cast<uint32_t>(collider.collisionMask.to_ulong()),
            .minX = centre.x - aabbHalf.x,
            .minY = centre.y - aabbHalf.y,
            .maxX = centre.x + aabbHalf.x,
            .maxY = centre.y + aabbHalf.y,
            .cx = centre.x,
            .cy = centre.y,
            .hx = hx,
            .hy = hy,
            .rot = rot,
            .shape = ColliderShape::Box};
  }

  static Box BuildBox(Entity entity, const GlobalTransformComponent& transform, const CircleColliderComponent& collider,
                      const EntityMaskComponent& entityMask) {
    const octarine::Rotation2D rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 centre = TransformLocalPoint(collider.offset, transform, rot);
    const float scaleMax = std::max(std::abs(transform.scale.x), std::abs(transform.scale.y));
    const float r = std::max(0.0f, collider.radius) * scaleMax;

    return {.entity = entity,
            .entityMask = static_cast<uint32_t>(entityMask.mask.to_ulong()),
            .collisionMask = static_cast<uint32_t>(collider.collisionMask.to_ulong()),
            .minX = centre.x - r,
            .minY = centre.y - r,
            .maxX = centre.x + r,
            .maxY = centre.y + r,
            .cx = centre.x,
            .cy = centre.y,
            .hx = centre.x,
            .hy = centre.y,
            .rot = {r, 0.0f},
            .shape = ColliderShape::Circle};
  }

  static Box BuildBox(Entity entity, const GlobalTransformComponent& transform,
                      const CapsuleColliderComponent& collider, const EntityMaskComponent& entityMask) {
    const octarine::Rotation2D rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 p1 = TransformLocalPoint(collider.start, transform, rot);
    const glm::vec2 p2 = TransformLocalPoint(collider.end, transform, rot);
    const float scaleMax = std::max(std::abs(transform.scale.x), std::abs(transform.scale.y));
    const float r = std::max(0.0f, collider.radius) * scaleMax;

    return {.entity = entity,
            .entityMask = static_cast<uint32_t>(entityMask.mask.to_ulong()),
            .collisionMask = static_cast<uint32_t>(collider.collisionMask.to_ulong()),
            .minX = std::min(p1.x, p2.x) - r,
            .minY = std::min(p1.y, p2.y) - r,
            .maxX = std::max(p1.x, p2.x) + r,
            .maxY = std::max(p1.y, p2.y) + r,
            .cx = p1.x,
            .cy = p1.y,
            .hx = p2.x,
            .hy = p2.y,
            .rot = {r, 0.0f},
            .shape = ColliderShape::Capsule};
  }

  static Box BuildBox(Entity entity, const GlobalTransformComponent& transform,
                      const TriangleColliderComponent& collider, const EntityMaskComponent& entityMask) {
    const octarine::Rotation2D rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 v0 = TransformLocalPoint(collider.p1, transform, rot);
    const glm::vec2 v1 = TransformLocalPoint(collider.p2, transform, rot);
    const glm::vec2 v2 = TransformLocalPoint(collider.p3, transform, rot);

    return {.entity = entity,
            .entityMask = static_cast<uint32_t>(entityMask.mask.to_ulong()),
            .collisionMask = static_cast<uint32_t>(collider.collisionMask.to_ulong()),
            .minX = std::min({v0.x, v1.x, v2.x}),
            .minY = std::min({v0.y, v1.y, v2.y}),
            .maxX = std::max({v0.x, v1.x, v2.x}),
            .maxY = std::max({v0.y, v1.y, v2.y}),
            .cx = v0.x,
            .cy = v0.y,
            .hx = v1.x,
            .hy = v1.y,
            .rot = {v2.x, v2.y},
            .shape = ColliderShape::Triangle};
  }

  static Box BuildBox(Entity entity, const GlobalTransformComponent& transform, const LineColliderComponent& collider,
                      const EntityMaskComponent& entityMask) {
    const octarine::Rotation2D rot = octarine::Rotation2D::FromRadians(transform.rotation);
    const glm::vec2 p1 = TransformLocalPoint(collider.start, transform, rot);
    const glm::vec2 p2 = TransformLocalPoint(collider.end, transform, rot);

    return {.entity = entity,
            .entityMask = static_cast<uint32_t>(entityMask.mask.to_ulong()),
            .collisionMask = static_cast<uint32_t>(collider.collisionMask.to_ulong()),
            .minX = std::min(p1.x, p2.x),
            .minY = std::min(p1.y, p2.y),
            .maxX = std::max(p1.x, p2.x),
            .maxY = std::max(p1.y, p2.y),
            .cx = p1.x,
            .cy = p1.y,
            .hx = p2.x,
            .hy = p2.y,
            .rot = {0.0f, 0.0f},
            .shape = ColliderShape::Line};
  }

  void operator()(const ContextFacade& ctx) {
    auto* registry = ctx.GetRegistry();
    auto* eventBus = registry->Get<EngineContext>().eventBus;

    EnsureAndUpdateQueries(registry);

    // Deterministic simulation mode (ThreadPool force-serial) also forces synchronous detection:
    // the async path emits whichever pass has finished by wall-clock time, which varies per run.
    if (synchronous_ || ThreadPool::IsForceSerial()) {
      if (collisionResult_.valid()) {
        static_cast<void>(collisionResult_.get());  // drain any in-flight async pass from before the mode switch
      }
      UpdateSynchronous(eventBus);
    } else {
      UpdateAsynchronous(eventBus);
    }
  }

 private:
  template <typename ColliderT>
  using ColliderQuery = ComponentQuery<GlobalTransformComponent, ColliderT, EntityMaskComponent>;

  void EnsureAndUpdateQueries(Registry* registry) {
    if (!query_) {
      query_ = registry->CreateQuery<GlobalTransformComponent, BoxColliderComponent, EntityMaskComponent>();
      circleQuery_ = registry->CreateQuery<GlobalTransformComponent, CircleColliderComponent, EntityMaskComponent>();
      capsuleQuery_ = registry->CreateQuery<GlobalTransformComponent, CapsuleColliderComponent, EntityMaskComponent>();
      triangleQuery_ =
          registry->CreateQuery<GlobalTransformComponent, TriangleColliderComponent, EntityMaskComponent>();
      lineQuery_ = registry->CreateQuery<GlobalTransformComponent, LineColliderComponent, EntityMaskComponent>();
    }
    query_->Update();
    circleQuery_->Update();
    capsuleQuery_->Update();
    triangleQuery_->Update();
    lineQuery_->Update();
  }

  [[nodiscard]] size_t TotalColliderCount() const {
    return query_->GetCount() + circleQuery_->GetCount() + capsuleQuery_->GetCount() + triangleQuery_->GetCount() +
           lineQuery_->GetCount();
  }

  template <typename QueryT>
  static void AppendBoxesSync(QueryT& q, std::vector<Box>& boxes) {
    if (q.GetCount() == 0) return;
    q.ForEach([&](Entity entity, const GlobalTransformComponent& transform, const auto& collider,
                  const EntityMaskComponent& entityMask) {
      boxes.push_back(BuildBox(entity, transform, collider, entityMask));
    });
  }

  template <typename QueryT>
  static void AppendBoxesAsync(QueryT& q, std::vector<Box>& boxes, std::atomic<size_t>& nextIndex) {
    if (q.GetCount() == 0) return;
    q.ParallelForEach([&](Entity entity, const GlobalTransformComponent& transform, const auto& collider,
                          const EntityMaskComponent& entityMask) {
      const size_t idx = nextIndex.fetch_add(1, std::memory_order_relaxed);
      boxes[idx] = BuildBox(entity, transform, collider, entityMask);
    });
  }

  void UpdateSynchronous(EventBus* eventBus) {
    std::vector<Box>& boxes = cachedBoxes_;
    boxes.clear();
    boxes.reserve(TotalColliderCount());
    AppendBoxesSync(*query_, boxes);
    AppendBoxesSync(*circleQuery_, boxes);
    AppendBoxesSync(*capsuleQuery_, boxes);
    AppendBoxesSync(*triangleQuery_, boxes);
    AppendBoxesSync(*lineQuery_, boxes);
    std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) {
      return a.entity.id != b.entity.id ? (a.entity.id < b.entity.id) : (a.shape < b.shape);
    });

    PROFILE_COUNTER_SET("Collision: Box count", static_cast<long long>(boxes.size()));

    std::vector<std::pair<Entity, Entity>> intersectingPairs;
    if (!boxes.empty()) {
      ComputeAsyncCollisions(boxes, intersectingPairs);
      std::sort(intersectingPairs.begin(), intersectingPairs.end(), [](const auto& a, const auto& b) {
        if (a.first.id != b.first.id) return a.first.id < b.first.id;
        return a.second.id < b.second.id;
      });
    }
    EmitCollisionEvents(eventBus, intersectingPairs);
  }

  void UpdateAsynchronous(EventBus* eventBus) {
    if (collisionResult_.valid() &&
        collisionResult_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      return;
    }

    if (collisionResult_.valid()) {
      PROFILE_NAMED_SCOPE("Emit Events");
      CollisionResult result = collisionResult_.get();
      EmitCollisionEvents(eventBus, result.intersectingPairs);
      cachedBoxes_ = std::move(result.boxes);
      cachedBoxes_.clear();
    }

    PROFILE_NAMED_SCOPE("Gather Boxes");
    std::vector<Box> boxes = std::move(cachedBoxes_);
    boxes.resize(TotalColliderCount());
    std::atomic<size_t> nextIndex{0};

    AppendBoxesAsync(*query_, boxes, nextIndex);
    AppendBoxesAsync(*circleQuery_, boxes, nextIndex);
    AppendBoxesAsync(*capsuleQuery_, boxes, nextIndex);
    AppendBoxesAsync(*triangleQuery_, boxes, nextIndex);
    AppendBoxesAsync(*lineQuery_, boxes, nextIndex);

    PROFILE_COUNTER_SET("Collision: Box count", static_cast<long long>(boxes.size()));

    if (boxes.empty()) {
      return;
    }

    collisionResult_ = StartAsyncCollisionDetection(std::move(boxes));
  }

 public:
  // Returns true if entity a and entity b are currently overlapping (sustained OR just-entered).
  // Reflects the result of the most recently completed async detection pass (one frame of lag
  // on the very first frame, stable thereafter). Safe to call from on_update or on_collision.
  [[nodiscard]] bool IsOverlapping(const Entity a, const Entity b) const {
    return prevPairSet_.contains({std::min(a.id, b.id), std::max(a.id, b.id)});
  }

 private:
  struct PairHash {
    size_t operator()(const std::pair<EntityID, EntityID>& p) const noexcept {
      // Boost-style hash_combine: golden-ratio constant spreads entropy, shifts mix bits.
      static constexpr size_t kGoldenRatio = 0x9e3779b9ULL;
      static constexpr int kLeftShift = 6;
      size_t h = std::hash<EntityID>{}(p.first);
      h ^= std::hash<EntityID>{}(p.second) + kGoldenRatio + (h << kLeftShift) + (h >> 2);
      return h;
    }
  };
  using PairSet = std::unordered_set<std::pair<EntityID, EntityID>, PairHash>;

  PairSet prevPairSet_;
  std::vector<Box> cachedBoxes_;
  std::future<CollisionResult> collisionResult_;
  std::unique_ptr<ColliderQuery<BoxColliderComponent>> query_;
  std::unique_ptr<ColliderQuery<CircleColliderComponent>> circleQuery_;
  std::unique_ptr<ColliderQuery<CapsuleColliderComponent>> capsuleQuery_;
  std::unique_ptr<ColliderQuery<TriangleColliderComponent>> triangleQuery_;
  std::unique_ptr<ColliderQuery<LineColliderComponent>> lineQuery_;
  bool synchronous_{false};

  // Diff this frame's overlaps against the previous frame and emit the enter/exit batches.
  //   - enter: pairs overlapping now but not last frame — first-contact only, so persistent
  //     overlaps (enemy pinned against a wall, stacked entities) don't re-fire every frame.
  //   - exit: pairs that overlapped last frame but no longer do.
  void EmitCollisionEvents(EventBus* eventBus, const std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    PROFILE_COUNTER_SET("Collision: Intersecting pairs", static_cast<long long>(intersectingPairs.size()));

    PairSet currentSet;
    currentSet.reserve(intersectingPairs.size());
    std::vector<std::pair<Entity, Entity>> enteringPairs;
    enteringPairs.reserve(intersectingPairs.size());
    for (const auto& [a, b] : intersectingPairs) {
      const auto key = std::make_pair(std::min(a.id, b.id), std::max(a.id, b.id));
      if (currentSet.insert(key).second && !prevPairSet_.contains(key)) {
        enteringPairs.emplace_back(a, b);
      }
    }

    std::vector<std::pair<Entity, Entity>> exitingPairs;
    for (const auto& [minId, maxId] : prevPairSet_) {
      if (!currentSet.contains({minId, maxId})) {
        exitingPairs.emplace_back(Entity{minId}, Entity{maxId});
      }
    }

    prevPairSet_ = std::move(currentSet);

    PROFILE_COUNTER_SET("Collision: Entering pairs", static_cast<long long>(enteringPairs.size()));
    eventBus->EmitEvent<CollisionBatchEvent>(enteringPairs);
    eventBus->EmitEvent<CollisionExitBatchEvent>(exitingPairs);
  }

  static void BruteForceHugeBoxes(const std::vector<Box>& boxes, const std::vector<Box>::iterator& hugeBoxesBegin,
                                  std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    ACCUMULATE_PROFILE_SCOPE("Huge Box Brute Force");
    for (auto hugeIt = hugeBoxesBegin; hugeIt != boxes.end(); ++hugeIt) {
      for (auto normalIt = boxes.begin(); normalIt != hugeBoxesBegin; ++normalIt) {
        if (hugeIt->intersects(*normalIt)) {
          intersectingPairs.emplace_back(hugeIt->entity, normalIt->entity);
        }
      }
      for (auto otherHugeIt = hugeIt + 1; otherHugeIt != boxes.end(); ++otherHugeIt) {
        if (hugeIt->intersects(*otherHugeIt)) {
          intersectingPairs.emplace_back(hugeIt->entity, otherHugeIt->entity);
        }
      }
    }
  }

  static void ComputeAsyncCollisions(std::vector<Box>& boxes,
                                     std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    AGGREGATE_PROFILE_SESSION("Async Box Creation");
    if (boxes.empty()) return;

    ACCUMULATE_PROFILE_SCOPE("Huge Box Extraction");

    // Dynamically calculate the huge box threshold to be scale-invariant.
    // Defines a "huge outlier" as a box >= 4x the average size.
    double sum = 0.0;
    for (const auto& b : boxes) {
      sum += std::max(b.maxX - b.minX, b.maxY - b.minY);
    }
    const double mean = sum / static_cast<double>(boxes.size());
    const auto hugeThreshold = static_cast<float>(mean * 4.0);
    // Extract any box larger than the dynamic threshold. These are typically background bounds or map
    // triggers that cross the entire level. Putting them in the KD-Tree destroys the median
    // splits and breaks the Sweep Bipartite early-outs.
    const auto hugeBoxesBegin = std::ranges::partition(boxes, [hugeThreshold](const Box& b) {
                                  return (b.maxX - b.minX) <= hugeThreshold && (b.maxY - b.minY) <= hugeThreshold;
                                }).begin();

    const int normalCount = static_cast<int>(std::distance(boxes.begin(), hugeBoxesBegin));
    if (normalCount > 0) {
      FindIntersectionsRecursive(boxes, 0, normalCount, 0, 0, intersectingPairs);
    }

    if (hugeBoxesBegin != boxes.end()) {
      BruteForceHugeBoxes(boxes, hugeBoxesBegin, intersectingPairs);
    }
  }

  [[nodiscard]] static std::future<CollisionResult> StartAsyncCollisionDetection(std::vector<Box> boxes) {
    // Run on the persistent worker pool rather than std::async(launch::async), which spawns and
    // tears down an OS thread per cycle. The promise is shared because ThreadPool::Submit takes a
    // copyable std::function; the future hands back to the same valid()/wait_for/get polling below.
    auto promise = std::make_shared<std::promise<CollisionResult>>();
    std::future<CollisionResult> result = promise->get_future();
    ThreadPool::Instance().Submit([boxes = std::move(boxes), promise]() mutable {
      std::vector<std::pair<Entity, Entity>> intersectingPairs;
      ComputeAsyncCollisions(boxes, intersectingPairs);
      promise->set_value(CollisionResult{std::move(intersectingPairs), std::move(boxes)});
    });
    return result;
  }

  static void FindIntersectionsBruteForce(const std::vector<Box>& boxes, const int begin, const int end,
                                          std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    ACCUMULATE_PROFILE_SCOPE("Brute Force Intersection");

    uint32_t entityUnion = 0;
    uint32_t collUnion = 0;
    for (int i = begin; i < end; ++i) {
      entityUnion |= boxes[static_cast<size_t>(i)].entityMask;
      collUnion |= boxes[static_cast<size_t>(i)].collisionMask;
    }
    if ((entityUnion & collUnion) == 0U) {
      return;
    }

    int checks = 0;
    for (int i = begin; i < end; ++i) {
      for (int j = i + 1; j < end; ++j) {
        checks++;
        const auto& bi = boxes[static_cast<size_t>(i)];
        const auto& bj = boxes[static_cast<size_t>(j)];
        if (bi.intersects(bj)) {
          intersectingPairs.emplace_back(bi.entity, bj.entity);
        }
      }
    }
#ifdef OCTARINE_PROFILING
    PROFILE_COUNTER_ADD("Collision: Brute Force Checks", checks);
#else
    (void)checks;
#endif
  }

  [[nodiscard]] static Partitions PartitionBoxes(std::vector<Box>& boxes, const int begin, const int end,
                                                 const int dimension, const float medianValue) {
    ACCUMULATE_PROFILE_SCOPE("Partition Boxes");
    const auto first = boxes.begin() + begin;
    const auto last = boxes.begin() + end;

    const auto leftEndIt = std::partition(first, last, [&](const Box& box) {
      if (dimension == 0) {
        return box.maxX < medianValue;
      }

      return box.maxY < medianValue;
    });

    const auto rightBeginIt = std::partition(leftEndIt, last, [&](const Box& box) {
      if (dimension == 0) {
        return box.minX <= medianValue;
      }

      return box.minY <= medianValue;
    });

    const int leftEnd = static_cast<int>(std::distance(boxes.begin(), leftEndIt));

    const int rightStart = static_cast<int>(std::distance(boxes.begin(), rightBeginIt));

    return Partitions{leftEnd, rightStart};
  }

  static void FindIntersectionsBruteForceBipartite(const std::vector<Box>& boxes, const int begin1, const int end1,
                                                   const int begin2, const int end2,
                                                   std::vector<std::pair<Entity, Entity>>& pairs) {
    ACCUMULATE_PROFILE_SCOPE("Brute Force Bipartite");
    int checks = 0;
    for (int i = begin1; i < end1; ++i) {
      for (int j = begin2; j < end2; ++j) {
        checks++;
        const auto& bi = boxes[static_cast<size_t>(i)];
        const auto& bj = boxes[static_cast<size_t>(j)];
        if (bi.intersects(bj)) {
          pairs.emplace_back(bi.entity, bj.entity);
        }
      }
    }
#ifdef OCTARINE_PROFILING
    PROFILE_COUNTER_ADD("Collision: Brute Force Bipartite Checks", checks);
#else
    (void)checks;
#endif
  }

  // KD-Tree inherently requires recursion and dense Bipartite bounds-checking logic.
  // NOLINTNEXTLINE(misc-no-recursion,readability-function-cognitive-complexity)
  static void FindIntersectionsSweepBipartite(std::vector<Box>& boxes, const int begin1, const int end1,
                                              const int begin2, const int end2, const int dimension,
                                              std::vector<std::pair<Entity, Entity>>& pairs) {
    ACCUMULATE_PROFILE_SCOPE("Sweep Bipartite");

    if (end1 - begin1 == 0 || end2 - begin2 == 0) return;

    // Aggregate Mask Pruning: Skips bipartite sweep entirely if the union of entity and collision masks proves no
    // interaction is possible.
    uint32_t entity1 = 0;
    uint32_t coll1 = 0;
    for (int i = begin1; i < end1; ++i) {
      entity1 |= boxes[static_cast<size_t>(i)].entityMask;
      coll1 |= boxes[static_cast<size_t>(i)].collisionMask;
    }

    uint32_t entity2 = 0;
    uint32_t coll2 = 0;
    for (int i = begin2; i < end2; ++i) {
      entity2 |= boxes[static_cast<size_t>(i)].entityMask;
      coll2 |= boxes[static_cast<size_t>(i)].collisionMask;
    }

    const bool groupCanInteract = ((coll1 & entity2) | (coll2 & entity1)) != 0U;
    if (!groupCanInteract) {
      return;
    }

    if ((end1 - begin1) * (end2 - begin2) <= kBruteforceCutoff * kBruteforceCutoff) {
      FindIntersectionsBruteForceBipartite(boxes, begin1, end1, begin2, end2, pairs);
      return;
    }

    {
      ACCUMULATE_PROFILE_SCOPE("Bipartite Sort Phase");
      if (dimension == 0) {
        std::sort(boxes.begin() + begin1, boxes.begin() + end1,
                  [](const Box& a, const Box& b) { return a.minX < b.minX; });
        std::sort(boxes.begin() + begin2, boxes.begin() + end2,
                  [](const Box& a, const Box& b) { return a.minX < b.minX; });
      } else {
        std::sort(boxes.begin() + begin1, boxes.begin() + end1,
                  [](const Box& a, const Box& b) { return a.minY < b.minY; });
        std::sort(boxes.begin() + begin2, boxes.begin() + end2,
                  [](const Box& a, const Box& b) { return a.minY < b.minY; });
      }
    }

    {
      ACCUMULATE_PROFILE_SCOPE("Bipartite Sweep Phase");
      int sweepChecks = 0;
      if (dimension == 0) {
        int startJ = begin2;
        for (int i = begin1; i < end1; ++i) {
          const Box& a = boxes[static_cast<size_t>(i)];
          while (startJ < end2 && boxes[static_cast<size_t>(startJ)].maxX < a.minX) ++startJ;
          for (int j = startJ; j < end2; ++j) {
            sweepChecks++;
            const Box& b = boxes[static_cast<size_t>(j)];
            if (b.minX > a.maxX) break;
            if (b.maxY < a.minY || b.minY > a.maxY) continue;
            if (a.intersects(b)) pairs.emplace_back(a.entity, b.entity);
          }
        }
      } else {
        int startJ = begin2;
        for (int i = begin1; i < end1; ++i) {
          const Box& a = boxes[static_cast<size_t>(i)];
          while (startJ < end2 && boxes[static_cast<size_t>(startJ)].maxY < a.minY) ++startJ;
          for (int j = startJ; j < end2; ++j) {
            sweepChecks++;
            const Box& b = boxes[static_cast<size_t>(j)];
            if (b.minY > a.maxY) break;
            if (b.maxX < a.minX || b.minX > a.maxX) continue;
            if (a.intersects(b)) pairs.emplace_back(a.entity, b.entity);
          }
        }
      }
#ifdef OCTARINE_PROFILING
      PROFILE_COUNTER_ADD("Collision: Bipartite Sweep Inner Loop Checks", sweepChecks);
#else
      (void)sweepChecks;
#endif
    }
  }

  static constexpr float kHalf = 0.5f;

  // NOLINTNEXTLINE(misc-no-recursion) - KD-Tree cross-checking inherently requires recursion.
  static void ProcessSpanningBipartite(std::vector<Box>& boxes, const int begin, const int end, const int leftEnd,
                                       const int rightStart, const int dimension, const int nextDimension,
                                       std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    if (leftEnd >= rightStart) return;

    // Find the min/max bounds of the Spanning set along the current dimension
    float spanMin = std::numeric_limits<float>::max();
    float spanMax = std::numeric_limits<float>::lowest();
    for (int i = leftEnd; i < rightStart; ++i) {
      const float minV = dimension == 0 ? boxes[static_cast<size_t>(i)].minX : boxes[static_cast<size_t>(i)].minY;
      const float maxV = dimension == 0 ? boxes[static_cast<size_t>(i)].maxX : boxes[static_cast<size_t>(i)].maxY;
      if (minV < spanMin) spanMin = minV;
      if (maxV > spanMax) spanMax = maxV;
    }

    const auto activeLeftBegin = std::partition(boxes.begin() + begin, boxes.begin() + leftEnd, [=](const Box& a) {
      const float maxV = dimension == 0 ? a.maxX : a.maxY;
      return maxV < spanMin;
    });
    const int leftOverlapStart = static_cast<int>(std::distance(boxes.begin(), activeLeftBegin));

    const auto activeRightEnd = std::partition(boxes.begin() + rightStart, boxes.begin() + end, [=](const Box& a) {
      const float minV = dimension == 0 ? a.minX : a.minY;
      return minV <= spanMax;
    });
    const int rightOverlapEnd = static_cast<int>(std::distance(boxes.begin(), activeRightEnd));

    FindIntersectionsSweepBipartite(boxes, leftOverlapStart, leftEnd, leftEnd, rightStart, nextDimension,
                                    intersectingPairs);
    FindIntersectionsSweepBipartite(boxes, leftEnd, rightStart, rightStart, rightOverlapEnd, nextDimension,
                                    intersectingPairs);
  }
  // KD-Tree inherently requires recursion.
  // NOLINTNEXTLINE(misc-no-recursion)
  static void FindIntersectionsRecursive(std::vector<Box>& boxes, const int begin, const int end, int dimension,
                                         const int depth, std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    const int count = end - begin;
    if (count <= 1) {
      return;
    }

    if (count < kBruteforceCutoff || depth >= kMaxRecursionDepth) {
      FindIntersectionsBruteForce(boxes, begin, end, intersectingPairs);
      return;
    }

    const int mid = (begin + end) / 2;
    float medianValue = 0.0f;
    {
      ACCUMULATE_PROFILE_SCOPE("Find Median");
      std::nth_element(boxes.begin() + begin, boxes.begin() + mid, boxes.begin() + end,
                       [dimension](const Box& a, const Box& b) {
                         if (dimension == 0) return a.maxX + a.minX < b.maxX + b.minX;
                         return a.maxY + a.minY < b.maxY + b.minY;
                       });
      medianValue = (dimension == 0)
                        ? (boxes[static_cast<size_t>(mid)].minX + boxes[static_cast<size_t>(mid)].maxX) * kHalf
                        : (boxes[static_cast<size_t>(mid)].minY + boxes[static_cast<size_t>(mid)].maxY) * kHalf;
    }

    const auto [leftEnd, rightStart] = PartitionBoxes(boxes, begin, end, dimension, medianValue);
    const int nextDimension = (dimension + 1) % kMaxDimensions;

    FindIntersectionsRecursive(boxes, begin, leftEnd, dimension, depth + 1, intersectingPairs);
    FindIntersectionsRecursive(boxes, rightStart, end, dimension, depth + 1, intersectingPairs);
    FindIntersectionsRecursive(boxes, leftEnd, rightStart, nextDimension, depth + 1, intersectingPairs);

    ProcessSpanningBipartite(boxes, begin, end, leftEnd, rightStart, dimension, nextDimension, intersectingPairs);
  }
};
