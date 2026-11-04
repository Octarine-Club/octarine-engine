#pragma once

#include <cstdint>
#include <type_traits>

struct ChunkResidentComponent {
  int32_t chunkX = 0;
  int32_t chunkY = 0;
  uint32_t propIndex = 0;

  ChunkResidentComponent() = default;
  ChunkResidentComponent(const int32_t cx, const int32_t cy, const uint32_t idx = 0)
      : chunkX(cx), chunkY(cy), propIndex(idx) {}
};

static_assert(std::is_trivially_copyable_v<ChunkResidentComponent>);
static_assert(sizeof(ChunkResidentComponent) == 12);
