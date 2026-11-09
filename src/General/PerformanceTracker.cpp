#include "General/PerformanceTracker.h"

#include <numeric>

#include "General/PerfUtils.h"
#include "General/SystemMemory.h"

PerformanceTracker::PerformanceTracker() {
  fps_samples_.reserve(kSampleBuffer);
  ms_samples_.reserve(kSampleBuffer);
  scratchpad_.reserve(kSampleBuffer);
}

void PerformanceTracker::RefreshWindows(const float fps, const float frameMs) {
  const float windowFps = window_seconds_ > 0.0F ? (static_cast<float>(window_frames_) / window_seconds_) : fps;
  window_frames_ = 0;
  window_seconds_ = 0.0F;

  scratchpad_ = fps_samples_;
  const float avgMs =
      ms_samples_.empty()
          ? frameMs
          : (std::accumulate(ms_samples_.begin(), ms_samples_.end(), 0.0F) / static_cast<float>(ms_samples_.size()));
  stats_.avgFps = avgMs > 0.0F ? (1000.0F / avgMs) : windowFps;
  stats_.p50Fps = PerfUtils::GetPercentileInPlace(scratchpad_, 0.5F);
  stats_.p95Fps = PerfUtils::GetPercentileInPlace(scratchpad_, 0.05F);
  stats_.p99Fps = PerfUtils::GetPercentileInPlace(scratchpad_, 0.01F);

  scratchpad_ = ms_samples_;
  stats_.avgFrameTimeMs = avgMs;
  stats_.p95FrameTimeMs = PerfUtils::GetPercentileInPlace(scratchpad_, 0.95F);
}

void PerformanceTracker::RecordFrame(const float deltaTime, const std::size_t entityCount) {
  const float fps = deltaTime > 0.0F ? (1.0F / deltaTime) : 0.0F;
  const float frameMs = deltaTime * 1000.0F;

  if (fps_samples_.size() < kSampleBuffer) {
    fps_samples_.push_back(fps);
    ms_samples_.push_back(frameMs);
  } else {
    fps_samples_[sample_index_] = fps;
    ms_samples_[sample_index_] = frameMs;
    sample_index_ = (sample_index_ + 1) % kSampleBuffer;
  }

  ++window_frames_;
  window_seconds_ += deltaTime;

  stats_.fps = fps;
  stats_.frameTimeMs = frameMs;
  stats_.entityCount = entityCount;

  refresh_timer_ -= deltaTime;
  if (refresh_timer_ <= 0.0F) {
    refresh_timer_ = kRefreshSeconds;
    RefreshWindows(fps, frameMs);
  }

  memory_timer_ -= deltaTime;
  if (memory_timer_ <= 0.0F) {
    memory_timer_ = kMemoryRefreshSeconds;
    stats_.memoryBytes = SystemMemory::GetResidentBytes();
    stats_.memoryMb = static_cast<double>(stats_.memoryBytes) / (1024.0 * 1024.0);
  }
}
