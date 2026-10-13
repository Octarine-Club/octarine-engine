#include "Systems/SeparationSystem.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

#include "Components/PositionComponent.h"
#include "Components/SeparationComponent.h"
#include "General/PerfUtils.h"
#include "General/ThreadPool.h"

constexpr float kCellSize = 64.0f;
constexpr float kInvCellSize = 1.0f / kCellSize;
constexpr int kGridWidth = 128;
constexpr int kGridHeight = 128;
constexpr float kGridOriginX = kGridWidth * kCellSize * 0.5f;
constexpr float kGridOriginY = kGridHeight * kCellSize * 0.5f;
constexpr size_t kGridCellCount = static_cast<size_t>(kGridWidth) * kGridHeight;
constexpr size_t kSeparationParallelThreshold = 50000;

inline int GetCellIndex(float x, float y) {
  int cx = static_cast<int>((x + kGridOriginX) * kInvCellSize);
  int cy = static_cast<int>((y + kGridOriginY) * kInvCellSize);
  cx = std::clamp(cx, 0, kGridWidth - 1);
  cy = std::clamp(cy, 0, kGridHeight - 1);
  return cy * kGridWidth + cx;
}

void SeparationSystem::ResolvePair(int i, int j) {
  auto& a = grid_entities_[static_cast<size_t>(i)];
  auto& b = grid_entities_[static_cast<size_t>(j)];

  const float combinedRadius = a.radius + b.radius;
  float dx = a.cx - b.cx;
  float dy = a.cy - b.cy;
  float distSq = dx * dx + dy * dy;

  if (distSq < combinedRadius * combinedRadius) {
    if (distSq < 0.0001f) {
      dx = static_cast<float>((a.entity.GetId() ^ b.entity.GetId()) & 0xFF) * 0.01f;
      dy = static_cast<float>((a.entity.GetId() | b.entity.GetId()) & 0xFF) * 0.01f;
      distSq = dx * dx + dy * dy;
      if (distSq < 0.0001f) distSq = 0.0001f;
    }
    const float dist = std::sqrt(distSq);
    const float overlap = combinedRadius - dist;

    const float totalWeight = a.weight + b.weight;
    constexpr float kResolveFactor = 0.15f;
    const float moveA = (overlap * (b.weight / totalWeight)) * kResolveFactor;
    const float moveB = (overlap * (a.weight / totalWeight)) * kResolveFactor;

    const float dirX = dx / dist;
    const float dirY = dy / dist;

    float mxA = dirX * moveA;
    float myA = dirY * moveA;
    float mxB = dirX * moveB;
    float myB = dirY * moveB;

    std::atomic_ref<float>(accum_dx_[static_cast<size_t>(i)]).fetch_add(mxA, std::memory_order_relaxed);
    std::atomic_ref<float>(accum_dy_[static_cast<size_t>(i)]).fetch_add(myA, std::memory_order_relaxed);
    std::atomic_ref<float>(accum_dx_[static_cast<size_t>(j)]).fetch_sub(mxB, std::memory_order_relaxed);
    std::atomic_ref<float>(accum_dy_[static_cast<size_t>(j)]).fetch_sub(myB, std::memory_order_relaxed);
  }
}

void SeparationSystem::ResolveCell(int begin1, int end1) {
  for (int i = begin1; i < end1; ++i) {
    for (int j = i + 1; j < end1; ++j) {
      ResolvePair(i, j);
    }
  }
}

[[nodiscard]] static inline bool IsValidNeighbor(int cellIdx, int cx, int noff) noexcept {
  int nIdx = cellIdx + noff;
  if (static_cast<size_t>(nIdx) >= kGridCellCount) return false;
  if ((noff == 1 || noff == kGridWidth + 1) && cx == kGridWidth - 1) return false;
  if (noff == kGridWidth - 1 && cx == 0) return false;
  return true;
}

void SeparationSystem::ResolveNeighbors(int cellIdx, int cx, int begin1, int end1) {
  const int neighborOffsets[4] = {1, kGridWidth - 1, kGridWidth, kGridWidth + 1};
  for (int noff : neighborOffsets) {
    if (!IsValidNeighbor(cellIdx, cx, noff)) continue;

    const auto nIdx = static_cast<size_t>(cellIdx + noff);
    const int count2 = cell_counts_[nIdx];
    if (count2 == 0) continue;
    const int begin2 = cell_offsets_[nIdx];
    const int end2 = begin2 + count2;

    for (int i = begin1; i < end1; ++i) {
      for (int j = begin2; j < end2; ++j) {
        ResolvePair(i, j);
      }
    }
  }
}

void SeparationSystem::ResolveGrid(const size_t count) {
  PROFILE_NAMED_SCOPE("Grid Resolve");
  if (count < kSeparationParallelThreshold) {
    for (const int cellIdx : active_cells_) {
      const int cx = cellIdx % kGridWidth;
      const int begin1 = cell_offsets_[static_cast<size_t>(cellIdx)];
      const int end1 = begin1 + cell_counts_[static_cast<size_t>(cellIdx)];

      ResolveCell(begin1, end1);
      ResolveNeighbors(cellIdx, cx, begin1, end1);
    }
  } else {
    ThreadPool::ParallelChunks(active_cells_.size(), [&](size_t /*batchIdx*/, size_t beginChunk, size_t endChunk) {
      for (size_t iter = beginChunk; iter < endChunk; ++iter) {
        int cellIdx = active_cells_[iter];
        int cx = cellIdx % kGridWidth;
        int begin1 = cell_offsets_[static_cast<size_t>(cellIdx)];
        int end1 = begin1 + cell_counts_[static_cast<size_t>(cellIdx)];

        ResolveCell(begin1, end1);
        ResolveNeighbors(cellIdx, cx, begin1, end1);
      }
    });
  }
}

void SeparationSystem::ApplyForces(const size_t count) {
  PROFILE_NAMED_SCOPE("Apply Separation Forces");
  if (count < kSeparationParallelThreshold) {
    for (size_t i = 0; i < count; ++i) {
      grid_entities_[i].cx += accum_dx_[i];
      grid_entities_[i].cy += accum_dy_[i];
    }
  } else {
    ThreadPool::ParallelChunks(count, [&](size_t /*batchIdx*/, size_t beginChunk, size_t endChunk) {
      for (size_t i = beginChunk; i < endChunk; ++i) {
        grid_entities_[i].cx += accum_dx_[static_cast<size_t>(i)];
        grid_entities_[i].cy += accum_dy_[static_cast<size_t>(i)];
      }
    });
  }
}

void SeparationSystem::WritePositions() {
  PROFILE_NAMED_SCOPE("Write Positions");
  for (const auto& e : grid_entities_) {
    e.targetPos->value.x = e.cx;
    e.targetPos->value.y = e.cy;
  }
}

void SeparationSystem::operator()(const ContextFacade& ctx) {
  if (ctx.IsPaused()) return;
  auto* registry = ctx.GetRegistry();

  if (!query_) {
    query_ = registry->CreateQuery<PositionComponent, SeparationComponent>();
  }
  query_->Update();

  const size_t count = query_->GetCount();
  if (count < 2) return;

  entities_.resize(count);
  if (cell_counts_.empty()) {
    cell_counts_.assign(kGridCellCount, 0);
  } else {
    for (const int cellIdx : active_cells_) {
      cell_counts_[static_cast<size_t>(cellIdx)] = 0;
    }
  }
  active_cells_.clear();

  {
    PROFILE_NAMED_SCOPE("Extract & Count");
    std::atomic<size_t> nextIndex{0};
    query_->ParallelForEach(
        [&](Entity entity, PositionComponent& pos, const SeparationComponent& sep) {
          const size_t idx = nextIndex.fetch_add(1, std::memory_order_relaxed);
          entities_[idx] = {&pos, entity, pos.value.x, pos.value.y, sep.radius, sep.weight};
        },
        kSeparationParallelThreshold);

    for (const auto& e : entities_) {
      const auto cellIdx = static_cast<size_t>(GetCellIndex(e.cx, e.cy));
      if (cell_counts_[cellIdx] == 0) {
        active_cells_.push_back(static_cast<int>(cellIdx));
      }
      cell_counts_[cellIdx]++;
    }
  }

  cell_offsets_.resize(kGridCellCount);
  current_offsets_.resize(kGridCellCount);
  {
    PROFILE_NAMED_SCOPE("Prefix Sum");
    int total = 0;
    for (const int cellIdx : active_cells_) {
      const auto idx = static_cast<size_t>(cellIdx);
      cell_offsets_[idx] = total;
      current_offsets_[idx] = total;
      total += cell_counts_[idx];
    }
  }

  grid_entities_.resize(count);
  {
    PROFILE_NAMED_SCOPE("Fill Grid");
    for (const auto& e : entities_) {
      int cellIdx = GetCellIndex(e.cx, e.cy);
      int dstIdx = current_offsets_[static_cast<size_t>(cellIdx)]++;
      grid_entities_[static_cast<size_t>(dstIdx)] = e;
    }
  }

  accum_dx_.assign(count, 0.0f);
  accum_dy_.assign(count, 0.0f);

  ResolveGrid(count);
  ApplyForces(count);
  WritePositions();
}
