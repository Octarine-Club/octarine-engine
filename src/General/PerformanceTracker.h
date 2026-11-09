#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct PerformanceStats {
  float fps = 0.0F;
  float avgFps = 0.0F;
  float p50Fps = 0.0F;
  float p95Fps = 0.0F;
  float p99Fps = 0.0F;
  float frameTimeMs = 0.0F;
  float avgFrameTimeMs = 0.0F;
  float p95FrameTimeMs = 0.0F;
  std::size_t entityCount = 0;
  std::uint64_t memoryBytes = 0;
  double memoryMb = 0.0;
};

class PerformanceTracker {
 public:
  PerformanceTracker();

  // Call once per rendered frame with the elapsed delta time and active user entity count.
  void RecordFrame(float deltaTime, std::size_t entityCount);

  [[nodiscard]] const PerformanceStats& GetStats() const { return stats_; }

 private:
  void RefreshWindows(float fps, float frameMs);

  static constexpr std::size_t kSampleBuffer = 120;
  static constexpr float kRefreshSeconds = 0.1F;
  static constexpr float kMemoryRefreshSeconds = 0.5F;

  std::vector<float> fps_samples_{};
  std::vector<float> ms_samples_{};
  std::vector<float> scratchpad_{};
  std::size_t sample_index_ = 0;

  float refresh_timer_ = 0.0F;
  float memory_timer_ = 0.0F;

  int window_frames_ = 0;
  float window_seconds_ = 0.0F;

  PerformanceStats stats_{};
};
