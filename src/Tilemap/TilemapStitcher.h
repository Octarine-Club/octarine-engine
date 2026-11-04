#pragma once

#include <algorithm>
#include <cstdint>

#include "Tilemap/ChunkTemplate.h"
#include "Tilemap/TilemapMath.h"

namespace octarine::tilemap {

class TilemapStitcher {
 public:
  [[nodiscard]] static const ChunkTemplate* SelectTemplate(const TilemapData& project, const StitchMode mode,
                                                           const uint32_t seed, const int32_t chunkX,
                                                           const int32_t chunkY) noexcept {
    if (project.templates.empty()) {
      return &project.borderTemplate;
    }

    switch (mode) {
      case StitchMode::Toroidal:
        return SelectToroidal(project, chunkX, chunkY);
      case StitchMode::ProceduralHash:
        return SelectProceduralHash(project, seed, chunkX, chunkY);
      case StitchMode::ArenaFixed:
        return SelectArenaFixed(project, chunkX, chunkY);
    }
    return &project.templates.front();
  }

 private:
  [[nodiscard]] static const ChunkTemplate* FindByGridCoord(const TilemapData& project, const int32_t gx,
                                                            const int32_t gy) noexcept {
    for (const auto& tmpl : project.templates) {
      if (tmpl.gridX == gx && tmpl.gridY == gy) {
        return &tmpl;
      }
    }
    return nullptr;
  }

  [[nodiscard]] static const ChunkTemplate* SelectToroidal(const TilemapData& project, const int32_t chunkX,
                                                           const int32_t chunkY) noexcept {
    const int32_t cols = std::max(1, project.gridCols);
    const int32_t rows = std::max(1, project.gridRows);
    const int32_t wrappedX = PositiveMod(chunkX, cols);
    const int32_t wrappedY = PositiveMod(chunkY, rows);
    if (const auto* match = FindByGridCoord(project, wrappedX, wrappedY)) {
      return match;
    }
    const auto count = static_cast<int32_t>(project.templates.size());
    const int32_t fallbackIdx = PositiveMod(wrappedY * cols + wrappedX, count);
    return &project.templates[static_cast<size_t>(fallbackIdx)];
  }

  [[nodiscard]] static bool ShouldFilterConstrainedSockets(const TilemapData& project) noexcept {
    bool hasUnconstrained = false;
    bool hasAnyEdgeSockets = false;
    for (const auto& tmpl : project.templates) {
      if (tmpl.HasEdgeSockets()) {
        hasAnyEdgeSockets = true;
      }
      if (tmpl.IsUnconstrainedSocketTemplate()) {
        hasUnconstrained = true;
      }
    }
    return hasAnyEdgeSockets && hasUnconstrained;
  }

  [[nodiscard]] static const ChunkTemplate* SelectProceduralHash(const TilemapData& project, const uint32_t seed,
                                                                 const int32_t chunkX, const int32_t chunkY) noexcept {
    const bool filterConstrained = ShouldFilterConstrainedSockets(project);

    float totalWeight = 0.0f;
    size_t candidateCount = 0;
    for (const auto& tmpl : project.templates) {
      if (filterConstrained && !tmpl.IsUnconstrainedSocketTemplate()) {
        continue;
      }
      totalWeight += std::max(0.0f, tmpl.weight);
      ++candidateCount;
    }
    const uint64_t h = HashChunkCoord(chunkX, chunkY, seed);
    if (totalWeight <= 0.0001f || candidateCount == 0) {
      return &project.templates[static_cast<size_t>(h % project.templates.size())];
    }

    // Use 24 bits of precision for uniform [0, totalWeight) selection.
    constexpr double kInv24Bit = 1.0 / static_cast<double>(1u << 24u);
    const float roll = static_cast<float>(static_cast<double>(h & 0xFFFFFFULL) * kInv24Bit) * totalWeight;
    float accum = 0.0f;
    const ChunkTemplate* lastCandidate = &project.templates.back();
    for (const auto& tmpl : project.templates) {
      if (filterConstrained && !tmpl.IsUnconstrainedSocketTemplate()) {
        continue;
      }
      lastCandidate = &tmpl;
      accum += std::max(0.0f, tmpl.weight);
      if (roll <= accum) {
        return &tmpl;
      }
    }
    return lastCandidate;
  }

  [[nodiscard]] static const ChunkTemplate* SelectArenaFixed(const TilemapData& project, const int32_t chunkX,
                                                             const int32_t chunkY) noexcept {
    const int32_t cols = std::max(1, project.gridCols);
    const int32_t rows = std::max(1, project.gridRows);
    if (chunkX < 0 || chunkX >= cols || chunkY < 0 || chunkY >= rows) {
      return &project.borderTemplate;
    }
    if (const auto* match = FindByGridCoord(project, chunkX, chunkY)) {
      return match;
    }
    return &project.borderTemplate;
  }
};

}  // namespace octarine::tilemap
