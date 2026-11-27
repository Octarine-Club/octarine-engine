#pragma once

#include <algorithm>
#include <cmath>

#include "General/Rect.h"

// Renderable viewport-cull test shared by all Render*System producers. World-space items
// are tested against the camera rect; screen-space (isFixed) items against the window rect.
// Signed width/height (e.g. mirrored entities with negative scale) are normalized to [min, max].
inline bool IsRenderableOutsideViewport(const float originX, const float originY, const float width, const float height,
                                        const bool isFixed, const octarine::Rect& camera, const float windowWidth,
                                        const float windowHeight) noexcept {
  const float minX = width >= 0.0f ? originX : (originX + width);
  const float maxX = width >= 0.0f ? (originX + width) : originX;
  const float minY = height >= 0.0f ? originY : (originY + height);
  const float maxY = height >= 0.0f ? (originY + height) : originY;
  if (isFixed) {
    return maxX < 0.0f || minX > windowWidth || maxY < 0.0f || minY > windowHeight;
  }
  return maxX < camera.x || minX > camera.x + camera.w || maxY < camera.y || minY > camera.y + camera.h;
}

// Viewport-cull test for quads that may be mirrored (negative width/height) and/or rotated
// by `rotation` radians around `(originX + pivotX, originY + pivotY)`.
inline bool IsRotatedQuadOutsideViewport(const float originX, const float originY, const float width,
                                         const float height, const float pivotX, const float pivotY,
                                         const float rotation, const bool isFixed, const octarine::Rect& camera,
                                         const float windowWidth, const float windowHeight) noexcept {
  if (rotation == 0.0f) {
    return IsRenderableOutsideViewport(originX, originY, width, height, isFixed, camera, windowWidth, windowHeight);
  }
  const float c = std::cos(rotation);
  const float s = std::sin(rotation);
  const float cx = originX + pivotX;
  const float cy = originY + pivotY;
  const float midLX = 0.5f * width - pivotX;
  const float midLY = 0.5f * height - pivotY;
  const float halfW = 0.5f * std::abs(width);
  const float halfH = 0.5f * std::abs(height);
  const float worldMidX = cx + midLX * c - midLY * s;
  const float worldMidY = cy + midLX * s + midLY * c;
  const float extX = std::abs(c) * halfW + std::abs(s) * halfH;
  const float extY = std::abs(s) * halfW + std::abs(c) * halfH;
  return IsRenderableOutsideViewport(worldMidX - extX, worldMidY - extY, 2.0f * extX, 2.0f * extY, isFixed, camera,
                                     windowWidth, windowHeight);
}
