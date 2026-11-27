#pragma once

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>

#include "General/Constants.h"
#include "Renderer/RenderCommands.h"
#include "Renderer/RenderableType.h"

// Render entry - slim header (precomputed sortKey + type + clip/material/gradient ids) plus
// one POD payload in a union, selected by `type`. Total size is locked to 80 bytes so sorted
// iteration streams cache-aligned sequentially instead of gathering across per-type vectors.
//
// `sortKey` packs (layer, depthBand, materialId, type, blendMode, batchKey-hash) into 64 bits
// so RenderQueue::Sort can use a comparison-free radix sort. Layout, MSB -> LSB:
//   bits 63-48 : layer (16 bits, unsigned - 64K layers covers kDebugUIBaseLayer headroom)
//   bits 47-31 : depthBand + bias (17 bits, clamped - +-1M world pixels at 16px default bands)
//   bits 30-23 : materialId (8 bits - clusters custom shader/material passes within a band)
//   bits 22-19 : type (4 bits - preserves primitive/text precedence within a material/band)
//   bits 18-16 : blend mode (3 bits - clusters same-blend runs before texture clustering)
//   bits 15- 0 : batchKey hash (16 bits - clusters same-texture runs within band+blend)
struct RenderKey {
  static constexpr float kDefaultYBandPx = 16.0f;

  std::uint64_t sortKey{};
  RenderableType type{};
  std::uint8_t clipId{0};
  std::uint8_t materialId{0};
  std::uint16_t gradientId{0};

  union Payload {
    SpriteCommand sprite;
    SquareCommand square;
    TextCommand text;
    CircleCommand circle;
    LineCommand line;
    TriangleCommand triangle;
    CapsuleCommand capsule;
    TileChunkCommand tileChunk;

    // Leaves union storage uninitialized - the producing system always overwrites the
    // active member chosen by `type`. Skipping zero-init saves ~56 bytes of writes per
    // emplace, which is meaningful at 100K+ keys per frame.
    Payload() {}
  } payload;

  RenderKey() = default;

  static std::uint64_t ComputeSortKey(unsigned int layer, float depth, RenderableType type, const void* batchKey,
                                      std::uint8_t blendBits = 0, std::uint8_t materialId = 0,
                                      float yBandPx = kDefaultYBandPx, bool ySortEnabled = true) noexcept {
    std::int64_t rawBand = 0;
    if (ySortEnabled && yBandPx > 0.0f && std::isfinite(depth)) {
      rawBand = static_cast<std::int64_t>(std::floor(depth / yBandPx));
    }

    // Bias depthBand into a clamped non-negative 17-bit range so extreme world coordinates
    // saturate at band boundaries rather than wrapping into adjacent layers.
    constexpr std::int64_t kBandBias = 1LL << 16;
    constexpr std::int64_t kMaxBiasedBand = (1LL << 17) - 1LL;
    const auto layer16 = static_cast<std::uint64_t>(layer & 0xFFFFu);
    const auto band17 = static_cast<std::uint64_t>(std::clamp<std::int64_t>(rawBand + kBandBias, 0LL, kMaxBiasedBand));
    const auto mat8 = static_cast<std::uint64_t>(materialId) & 0xFFu;
    const auto type4 = static_cast<std::uint64_t>(static_cast<std::uint8_t>(type)) & 0xFu;
    const auto blend3 = static_cast<std::uint64_t>(blendBits) & 0x7u;

    // 16-bit pointer hash: shift off allocator alignment noise then XOR-fold upper bits.
    const auto ptr = reinterpret_cast<std::uintptr_t>(batchKey);
    const std::uint64_t hash16 = static_cast<std::uint64_t>((ptr >> 4) ^ (ptr >> 20)) & 0xFFFFu;

    constexpr int kLayerShift = 48;
    constexpr int kBandShift = 31;
    constexpr int kMaterialShift = 23;
    constexpr int kTypeShift = 19;
    constexpr int kBlendShift = 16;
    return (layer16 << kLayerShift) | (band17 << kBandShift) | (mat8 << kMaterialShift) | (type4 << kTypeShift) |
           (blend3 << kBlendShift) | hash16;
  }

  // Kept for completeness; not used by Sort (radix sort uses sortKey directly).
  auto operator<=>(const RenderKey& other) const { return sortKey <=> other.sortKey; }
  bool operator==(const RenderKey& other) const {
    return sortKey == other.sortKey && type == other.type && materialId == other.materialId;
  }
};
