#pragma once

#include <benchmark/benchmark.h>

#include <cstdint>

namespace bench {

void SetHwCountersEnabled(bool enabled) noexcept;
[[nodiscard]] bool AreHwCountersEnabled() noexcept;

struct HwSnapshot {
  uint64_t instructions = 0;
  uint64_t cycles = 0;
  bool valid = false;
};

// RAII scope to sample hardware performance counters (instructions, cycles, IPC)
// via macOS proc_pid_rusage (RUSAGE_INFO_V4) or Linux perf_event_open.
class HwCounterScope {
 public:
  explicit HwCounterScope(double entitiesPerIteration = 0.0);
  ~HwCounterScope();
  HwCounterScope(const HwCounterScope&) = delete;
  HwCounterScope& operator=(const HwCounterScope&) = delete;

  void Stop();
  void Report(benchmark::State& state) const;

 private:
  double entitiesPerIteration_ = 0.0;
  HwSnapshot startSnapshot_{};
  HwSnapshot endSnapshot_{};
  bool stopped_ = false;
};

}  // namespace bench
