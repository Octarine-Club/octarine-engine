#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>

#include "Tilemap/ChunkTemplate.h"
#include "Tilemap/TilemapMath.h"

namespace octarine::tilemap {

struct TileRaycastHit {
  bool hit = false;
  float hitX = 0.0f;
  float hitY = 0.0f;
  float normalX = 0.0f;
  float normalY = 0.0f;
  float distance = 0.0f;
  uint8_t tileValue = kTileEmpty;
  int32_t tileX = 0;
  int32_t tileY = 0;
};

[[nodiscard]] constexpr bool MatchesCollisionMask(const uint8_t tileValue, const uint8_t mask) noexcept {
  if (tileValue == kTileEmpty || tileValue >= 8u) {
    return false;
  }
  return (mask & static_cast<uint8_t>(1u << tileValue)) != 0u;
}

namespace detail {

struct DdaState {
  int32_t tileX = 0;
  int32_t tileY = 0;
  int32_t stepX = 0;
  int32_t stepY = 0;
  float tMaxX = 0.0f;
  float tMaxY = 0.0f;
  float tDeltaX = 0.0f;
  float tDeltaY = 0.0f;
};

inline DdaState InitDda(const float x0, const float y0, const float dirX, const float dirY, const int32_t tileSize) {
  DdaState s;
  const auto ts = static_cast<float>(tileSize);
  s.tileX = WorldToGlobalTile(x0, tileSize);
  s.tileY = WorldToGlobalTile(y0, tileSize);

  constexpr float kInf = std::numeric_limits<float>::max();
  if (std::abs(dirX) > 1e-6f) {
    s.stepX = (dirX > 0.0f) ? 1 : -1;
    const float nextBoundX = static_cast<float>(s.tileX + (dirX > 0.0f ? 1 : 0)) * ts;
    s.tMaxX = (nextBoundX - x0) / dirX;
    s.tDeltaX = ts / std::abs(dirX);
  } else {
    s.tMaxX = kInf;
    s.tDeltaX = kInf;
  }

  if (std::abs(dirY) > 1e-6f) {
    s.stepY = (dirY > 0.0f) ? 1 : -1;
    const float nextBoundY = static_cast<float>(s.tileY + (dirY > 0.0f ? 1 : 0)) * ts;
    s.tMaxY = (nextBoundY - y0) / dirY;
    s.tDeltaY = ts / std::abs(dirY);
  } else {
    s.tMaxY = kInf;
    s.tDeltaY = kInf;
  }
  return s;
}

inline void ResolveCircleSingleTile(float& cx, float& cy, const float radius, const int32_t tx, const int32_t ty,
                                    const float ts) noexcept {
  const float minX = static_cast<float>(tx) * ts;
  const float minY = static_cast<float>(ty) * ts;
  const float maxX = minX + ts;
  const float maxY = minY + ts;

  const float closestX = std::clamp(cx, minX, maxX);
  const float closestY = std::clamp(cy, minY, maxY);
  const float dx = cx - closestX;
  const float dy = cy - closestY;
  const float distSq = dx * dx + dy * dy;

  if (distSq >= radius * radius) {
    return;
  }

  if (distSq > 1e-6f) {
    const float dist = std::sqrt(distSq);
    const float push = radius - dist;
    cx += (dx / dist) * push;
    cy += (dy / dist) * push;
    return;
  }

  // Circle center is inside the solid tile AABB: push out along shortest axis to tile edge.
  const float penLeft = (cx - minX) + radius;
  const float penRight = (maxX - cx) + radius;
  const float penTop = (cy - minY) + radius;
  const float penBottom = (maxY - cy) + radius;
  const float minPen = std::min({penLeft, penRight, penTop, penBottom});
  if (minPen == penLeft) {
    cx = minX - radius;
  } else if (minPen == penRight) {
    cx = maxX + radius;
  } else if (minPen == penTop) {
    cy = minY - radius;
  } else {
    cy = maxY + radius;
  }
}

inline void ResolveAABBSingleTile(float& x, float& y, const float w, const float h, const int32_t tx, const int32_t ty,
                                  const float ts) noexcept {
  const float tileMinX = static_cast<float>(tx) * ts;
  const float tileMinY = static_cast<float>(ty) * ts;
  const float tileMaxX = tileMinX + ts;
  const float tileMaxY = tileMinY + ts;

  const float overlapX = std::min(x + w, tileMaxX) - std::max(x, tileMinX);
  const float overlapY = std::min(y + h, tileMaxY) - std::max(y, tileMinY);
  if (overlapX <= 0.0f || overlapY <= 0.0f) {
    return;
  }

  const float centerX = x + w * 0.5f;
  const float centerY = y + h * 0.5f;
  const float tileCenterX = tileMinX + ts * 0.5f;
  const float tileCenterY = tileMinY + ts * 0.5f;

  if (overlapX < overlapY) {
    x += (centerX < tileCenterX) ? -overlapX : overlapX;
  } else {
    y += (centerY < tileCenterY) ? -overlapY : overlapY;
  }
}

template <typename TileGetterFn>
inline void ResolveCirclePass(float& worldX, float& worldY, const float radius, const float ts, const int32_t minTx,
                              const int32_t maxTx, const int32_t minTy, const int32_t maxTy, const int32_t centerTx,
                              const int32_t centerTy, const bool cardinalOnly, const uint8_t collisionMask,
                              TileGetterFn& getTileByGlobal) {
  for (int32_t ty = minTy; ty <= maxTy; ++ty) {
    for (int32_t tx = minTx; tx <= maxTx; ++tx) {
      const bool isCardinal = (tx == centerTx || ty == centerTy);
      if (isCardinal != cardinalOnly) continue;
      if (MatchesCollisionMask(getTileByGlobal(tx, ty), collisionMask)) {
        ResolveCircleSingleTile(worldX, worldY, radius, tx, ty, ts);
      }
    }
  }
}

}  // namespace detail

// 2D Amanatides-Woo DDA grid raycast across positive and negative global tile coordinates.
template <typename TileGetterFn>
[[nodiscard]] TileRaycastHit RaycastTilemap(const float x0, const float y0, const float x1, const float y1,
                                            const int32_t tileSize, const uint8_t collisionMask,
                                            TileGetterFn&& getTileByGlobal) {
  TileRaycastHit result{};
  result.hitX = x1;
  result.hitY = y1;
  if (tileSize <= 0) {
    return result;
  }

  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float maxDist = std::sqrt(dx * dx + dy * dy);
  result.distance = maxDist;

  int32_t curTx = WorldToGlobalTile(x0, tileSize);
  int32_t curTy = WorldToGlobalTile(y0, tileSize);
  const uint8_t startVal = getTileByGlobal(curTx, curTy);
  if (MatchesCollisionMask(startVal, collisionMask)) {
    result.hit = true;
    result.hitX = x0;
    result.hitY = y0;
    result.distance = 0.0f;
    result.tileValue = startVal;
    result.tileX = curTx;
    result.tileY = curTy;
    return result;
  }

  if (maxDist <= 1e-5f) {
    return result;
  }

  const float dirX = dx / maxDist;
  const float dirY = dy / maxDist;
  detail::DdaState dda = detail::InitDda(x0, y0, dirX, dirY, tileSize);

  constexpr int kMaxSteps = 4096;
  for (int step = 0; step < kMaxSteps; ++step) {
    float tEnter = 0.0f;
    float normX = 0.0f;
    float normY = 0.0f;
    if (dda.tMaxX < dda.tMaxY) {
      tEnter = dda.tMaxX;
      dda.tMaxX += dda.tDeltaX;
      dda.tileX += dda.stepX;
      normX = static_cast<float>(-dda.stepX);
    } else {
      tEnter = dda.tMaxY;
      dda.tMaxY += dda.tDeltaY;
      dda.tileY += dda.stepY;
      normY = static_cast<float>(-dda.stepY);
    }

    if (tEnter > maxDist) {
      break;
    }

    const uint8_t val = getTileByGlobal(dda.tileX, dda.tileY);
    if (MatchesCollisionMask(val, collisionMask)) {
      result.hit = true;
      result.distance = std::max(0.0f, tEnter);
      result.hitX = x0 + dirX * result.distance;
      result.hitY = y0 + dirY * result.distance;
      result.normalX = normX;
      result.normalY = normY;
      result.tileValue = val;
      result.tileX = dda.tileX;
      result.tileY = dda.tileY;
      return result;
    }
  }
  return result;
}

template <typename TileGetterFn>
[[nodiscard]] glm::vec2 ResolveCircleVsTilemap(float worldX, float worldY, const float radius, const int32_t tileSize,
                                               const uint8_t collisionMask, TileGetterFn&& getTileByGlobal) {
  if (tileSize <= 0 || radius <= 0.0f) {
    return {worldX, worldY};
  }
  const auto ts = static_cast<float>(tileSize);
  const int32_t minTx = WorldToGlobalTile(worldX - radius, tileSize);
  const int32_t maxTx = WorldToGlobalTile(worldX + radius, tileSize);
  const int32_t minTy = WorldToGlobalTile(worldY - radius, tileSize);
  const int32_t maxTy = WorldToGlobalTile(worldY + radius, tileSize);
  const int32_t centerTx = WorldToGlobalTile(worldX, tileSize);
  const int32_t centerTy = WorldToGlobalTile(worldY, tileSize);

  detail::ResolveCirclePass(worldX, worldY, radius, ts, minTx, maxTx, minTy, maxTy, centerTx, centerTy,
                            /*cardinalOnly=*/true, collisionMask, getTileByGlobal);
  detail::ResolveCirclePass(worldX, worldY, radius, ts, minTx, maxTx, minTy, maxTy, centerTx, centerTy,
                            /*cardinalOnly=*/false, collisionMask, getTileByGlobal);
  return {worldX, worldY};
}

template <typename TileGetterFn>
[[nodiscard]] glm::vec2 ResolveAABBVsTilemap(float worldX, float worldY, const float width, const float height,
                                             const int32_t tileSize, const uint8_t collisionMask,
                                             TileGetterFn&& getTileByGlobal) {
  if (tileSize <= 0 || width <= 0.0f || height <= 0.0f) {
    return {worldX, worldY};
  }
  const auto ts = static_cast<float>(tileSize);
  const int32_t minTx = WorldToGlobalTile(worldX, tileSize);
  const int32_t maxTx = WorldToGlobalTile(worldX + width - 1e-4f, tileSize);
  const int32_t minTy = WorldToGlobalTile(worldY, tileSize);
  const int32_t maxTy = WorldToGlobalTile(worldY + height - 1e-4f, tileSize);

  for (int32_t ty = minTy; ty <= maxTy; ++ty) {
    for (int32_t tx = minTx; tx <= maxTx; ++tx) {
      if (MatchesCollisionMask(getTileByGlobal(tx, ty), collisionMask)) {
        detail::ResolveAABBSingleTile(worldX, worldY, width, height, tx, ty, ts);
      }
    }
  }
  return {worldX, worldY};
}

}  // namespace octarine::tilemap
