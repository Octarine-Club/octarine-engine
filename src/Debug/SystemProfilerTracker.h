#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Registry;

namespace octarine::debug {

struct SystemTimingStats {
  std::size_t orderIndex = 0;
  std::string name{};
  double lastUs = 0.0;
  double avgUs = 0.0;
  double p95Us = 0.0;
  double maxUs = 0.0;
  float shareOfUpdate = 0.0f;  // 0.0 .. 1.0
};

struct ProfilerSnapshot {
  double lastUpdateMs = 0.0;
  double avgUpdateMs = 0.0;
  double p95UpdateMs = 0.0;
  double maxUpdateMs = 0.0;

  double lastFlushUs = 0.0;
  double avgFlushUs = 0.0;
  double p95FlushUs = 0.0;
  double maxFlushUs = 0.0;

  std::vector<SystemTimingStats> systems{};
  std::vector<float> updateMsHistory{};
};

class SystemProfilerTracker {
 public:
  static constexpr std::size_t kDefaultSampleWindow = 120;
  static constexpr float kDefaultRefreshInterval = 0.25f;  // 250ms (4 Hz)

  explicit SystemProfilerTracker(std::size_t sampleWindow = kDefaultSampleWindow,
                                 float refreshInterval = kDefaultRefreshInterval);

  void RecordFrame(const Registry& registry, float deltaTime);

  void Reset();

  void SetPaused(bool paused) { paused_ = paused; }
  [[nodiscard]] bool IsPaused() const { return paused_; }

  void SetRefreshInterval(float seconds) { refresh_interval_ = seconds; }
  [[nodiscard]] float GetRefreshInterval() const { return refresh_interval_; }

  [[nodiscard]] const ProfilerSnapshot& GetSnapshot() const { return snapshot_; }

 private:
  void RecomputeSnapshot(const Registry& registry);

  std::size_t sample_window_;
  float refresh_interval_;
  float refresh_timer_ = 0.0f;
  bool paused_ = false;
  bool first_frame_ = true;

  struct SystemRingBuffer {
    std::vector<float> samples{};
    std::size_t head = 0;
    float peakUs = 0.0f;
    float lastUs = 0.0f;
  };

  std::vector<SystemRingBuffer> system_buffers_{};
  SystemRingBuffer flush_buffer_{};
  SystemRingBuffer total_update_buffer_{};

  ProfilerSnapshot snapshot_{};
};

}  // namespace octarine::debug
