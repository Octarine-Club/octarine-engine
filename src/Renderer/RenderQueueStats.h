#pragma once

#include <cstddef>

class RenderQueue;

struct RenderQueueStats {
  double keysPerBatch = 0.0;
  std::size_t commandCount = 0;
  std::size_t drawBatches = 0;
  std::size_t textureSwitches = 0;
  std::size_t blendSwitches = 0;
  std::size_t clipSwitches = 0;
  std::size_t topologySwitches = 0;
  std::size_t totalVertices = 0;
  std::size_t totalIndices = 0;
};

// Pure analysis of a sorted RenderQueue that models GPU draw batching, state switches,
// and vertex counts without requiring an active SDL_Renderer or GPU device.
[[nodiscard]] RenderQueueStats AnalyzeRenderQueue(const RenderQueue& queue, float renderScale = 1.0f) noexcept;
