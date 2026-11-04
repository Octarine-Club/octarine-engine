#pragma once

#include <cmath>
#include <cstdint>

namespace octarine::tilemap {

// Euclidean floor division: rounds toward -infinity for negative dividends (assuming b > 0).
[[nodiscard]] constexpr int32_t FloorDiv(const int32_t a, const int32_t b) noexcept {
  if (b == 0) {
    return 0;
  }
  int32_t q = a / b;
  const int32_t r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) {
    --q;
  }
  return q;
}

// Positive modulo in [0, |b| - 1]: never returns a negative remainder when b > 0.
[[nodiscard]] constexpr int32_t PositiveMod(const int32_t a, const int32_t b) noexcept {
  if (b == 0) {
    return 0;
  }
  const int32_t absB = (b < 0) ? -b : b;
  int32_t r = a % absB;
  if (r < 0) {
    r += absB;
  }
  return r;
}

// Converts a continuous world coordinate (pixels) into a global integer tile coordinate.
// Uses std::floor so [-tileSize, 0) maps cleanly to global tile -1 without mirroring around 0.
[[nodiscard]] inline int32_t WorldToGlobalTile(const float worldCoord, const int32_t tileSize) noexcept {
  if (tileSize <= 0) {
    return 0;
  }
  return static_cast<int32_t>(std::floor(worldCoord / static_cast<float>(tileSize)));
}

// Converts a global tile coordinate into a chunk coordinate.
[[nodiscard]] constexpr int32_t GlobalTileToChunk(const int32_t globalTile, const int32_t chunkGridSize) noexcept {
  return FloorDiv(globalTile, chunkGridSize);
}

// Converts a global tile coordinate into a chunk-local tile coordinate in [0, chunkGridSize - 1].
[[nodiscard]] constexpr int32_t GlobalTileToLocalTile(const int32_t globalTile, const int32_t chunkGridSize) noexcept {
  return PositiveMod(globalTile, chunkGridSize);
}

// Converts a continuous world coordinate directly into a chunk coordinate.
[[nodiscard]] inline int32_t WorldToChunk(const float worldCoord, const int32_t chunkPixelSize) noexcept {
  if (chunkPixelSize <= 0) {
    return 0;
  }
  return static_cast<int32_t>(std::floor(worldCoord / static_cast<float>(chunkPixelSize)));
}

// Packs (chunkX, chunkY) into a 64-bit key for O(1) hash map lookups.
[[nodiscard]] constexpr int64_t PackChunkKey(const int32_t chunkX, const int32_t chunkY) noexcept {
  const auto ux = static_cast<uint64_t>(static_cast<uint32_t>(chunkX));
  const auto uy = static_cast<uint64_t>(static_cast<uint32_t>(chunkY));
  return static_cast<int64_t>((ux << 32u) | uy);
}

// Deterministic, stateless 2D coordinate hash (SplitMix64-inspired) for ProceduralHash stitching.
[[nodiscard]] constexpr uint64_t HashChunkCoord(const int32_t chunkX, const int32_t chunkY,
                                                const uint32_t seed) noexcept {
  uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) * 0x9E3779B97F4A7C15ULL;
  h ^= static_cast<uint64_t>(static_cast<uint32_t>(chunkY)) * 0xBF58476D1CE4E5B9ULL;
  h ^= static_cast<uint64_t>(seed) * 0x94D049BB133111EBULL;
  h = (h ^ (h >> 30u)) * 0xBF58476D1CE4E5B9ULL;
  h = (h ^ (h >> 27u)) * 0x94D049BB133111EBULL;
  return h ^ (h >> 31u);
}

}  // namespace octarine::tilemap
