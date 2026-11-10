#include "Debug/SystemProfilerTracker.h"

#include <algorithm>
#include <numeric>

#include "ECS/Registry.h"
#include "General/PerfUtils.h"

namespace octarine::debug {

namespace {

void PushSample(std::vector<float>& samples, std::size_t& head, const std::size_t maxCap, const float val) {
  if (samples.size() < maxCap) {
    samples.push_back(val);
  } else {
    samples[head] = val;
    head = (head + 1) % maxCap;
  }
}

double ComputeAvg(const std::vector<float>& samples) {
  if (samples.empty()) return 0.0;
  const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
  return sum / static_cast<double>(samples.size());
}

}  // namespace

SystemProfilerTracker::SystemProfilerTracker(const std::size_t sampleWindow, const float refreshInterval)
    : sample_window_(std::max<std::size_t>(1, sampleWindow)), refresh_interval_(refreshInterval) {
  snapshot_.updateMsHistory.reserve(sample_window_);
}

void SystemProfilerTracker::Reset() {
  system_buffers_.clear();
  flush_buffer_ = {};
  total_update_buffer_ = {};
  snapshot_ = {};
  snapshot_.updateMsHistory.reserve(sample_window_);
  refresh_timer_ = 0.0f;
  first_frame_ = true;
}

void SystemProfilerTracker::RecordFrame(const Registry& registry, const float deltaTime) {
  if (paused_) return;

  const auto& timings = registry.GetLastSystemTimingsNs();
  const uint64_t flushNs = registry.GetLastFlushTimingNs();
  const uint64_t totalNs = std::accumulate(timings.begin(), timings.end(), flushNs);

  const float flushUs = static_cast<float>(flushNs) / 1000.0f;
  const float totalMs = static_cast<float>(totalNs) / 1'000'000.0f;

  if (system_buffers_.size() < timings.size()) {
    system_buffers_.resize(timings.size());
  }

  for (size_t i = 0; i < timings.size(); ++i) {
    const float us = static_cast<float>(timings[i]) / 1000.0f;
    auto& buf = system_buffers_[i];
    buf.lastUs = us;
    buf.peakUs = std::max(buf.peakUs, us);
    PushSample(buf.samples, buf.head, sample_window_, us);
  }

  flush_buffer_.lastUs = flushUs;
  flush_buffer_.peakUs = std::max(flush_buffer_.peakUs, flushUs);
  PushSample(flush_buffer_.samples, flush_buffer_.head, sample_window_, flushUs);

  total_update_buffer_.lastUs = totalMs;
  total_update_buffer_.peakUs = std::max(total_update_buffer_.peakUs, totalMs);
  PushSample(total_update_buffer_.samples, total_update_buffer_.head, sample_window_, totalMs);

  refresh_timer_ -= deltaTime;
  if (first_frame_ || refresh_timer_ <= 0.0f || refresh_interval_ <= 0.0f) {
    first_frame_ = false;
    refresh_timer_ = refresh_interval_;
    RecomputeSnapshot(registry);
  }
}

void SystemProfilerTracker::RecomputeSnapshot(const Registry& registry) {
  snapshot_.lastUpdateMs = total_update_buffer_.lastUs;
  snapshot_.avgUpdateMs = ComputeAvg(total_update_buffer_.samples);
  snapshot_.p95UpdateMs = PerfUtils::GetPercentile(total_update_buffer_.samples, 0.95f);
  snapshot_.maxUpdateMs = total_update_buffer_.peakUs;

  snapshot_.lastFlushUs = flush_buffer_.lastUs;
  snapshot_.avgFlushUs = ComputeAvg(flush_buffer_.samples);
  snapshot_.p95FlushUs = PerfUtils::GetPercentile(flush_buffer_.samples, 0.95f);
  snapshot_.maxFlushUs = flush_buffer_.peakUs;

  snapshot_.updateMsHistory = total_update_buffer_.samples;

  const size_t systemCount = registry.GetSystemCount();
  snapshot_.systems.resize(systemCount);

  const double totalAvgUs = snapshot_.avgUpdateMs * 1000.0;

  for (size_t i = 0; i < systemCount; ++i) {
    auto& stats = snapshot_.systems[i];
    stats.orderIndex = i;
    stats.name = registry.GetSystemName(static_cast<SystemId>(i));

    if (i < system_buffers_.size()) {
      const auto& buf = system_buffers_[i];
      stats.lastUs = buf.lastUs;
      stats.avgUs = ComputeAvg(buf.samples);
      stats.p95Us = PerfUtils::GetPercentile(buf.samples, 0.95f);
      stats.maxUs = buf.peakUs;
      stats.shareOfUpdate = totalAvgUs > 0.0 ? static_cast<float>(stats.avgUs / totalAvgUs) : 0.0f;
    } else {
      stats.lastUs = 0.0;
      stats.avgUs = 0.0;
      stats.p95Us = 0.0;
      stats.maxUs = 0.0;
      stats.shareOfUpdate = 0.0f;
    }
  }
}

}  // namespace octarine::debug
