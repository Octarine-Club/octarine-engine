#include "BenchHwCounters.h"

#include <atomic>

#if defined(__APPLE__)
#include <libproc.h>
#include <unistd.h>

namespace {
bench::HwSnapshot PlatformSampleHw() {
  bench::HwSnapshot s;
  rusage_info_v4 ri{};
  if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&ri)) == 0) {
    s.instructions = ri.ri_instructions;
    s.cycles = ri.ri_cycles;
    s.valid = true;
  }
  return s;
}
}  // namespace
#elif defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
long PerfEventOpen(struct perf_event_attr* hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

bench::HwSnapshot PlatformSampleHw() {
  bench::HwSnapshot s;
  static int instFd = -1;
  static int cyclesFd = -1;
  static bool initialized = false;

  if (!initialized) {
    initialized = true;
    struct perf_event_attr pe {};
    pe.size = sizeof(struct perf_event_attr);
    pe.disabled = 0;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    pe.inherit = 1;

    pe.type = PERF_TYPE_HARDWARE;
    pe.config = PERF_COUNT_HW_INSTRUCTIONS;
    instFd = static_cast<int>(PerfEventOpen(&pe, 0, -1, -1, 0));

    pe.config = PERF_COUNT_HW_CPU_CYCLES;
    cyclesFd = static_cast<int>(PerfEventOpen(&pe, 0, -1, -1, 0));
  }

  if (instFd >= 0 && cyclesFd >= 0) {
    uint64_t instVal = 0;
    uint64_t cyclesVal = 0;
    if (read(instFd, &instVal, sizeof(instVal)) == sizeof(instVal) &&
        read(cyclesFd, &cyclesVal, sizeof(cyclesVal)) == sizeof(cyclesVal)) {
      s.instructions = instVal;
      s.cycles = cyclesVal;
      s.valid = true;
    }
  }
  return s;
}
}  // namespace
#else
namespace {
bench::HwSnapshot PlatformSampleHw() { return bench::HwSnapshot{}; }
}  // namespace
#endif

namespace bench {

static std::atomic<bool> g_hwCountersEnabled{false};

void SetHwCountersEnabled(const bool enabled) noexcept {
  g_hwCountersEnabled.store(enabled, std::memory_order_relaxed);
}

bool AreHwCountersEnabled() noexcept { return g_hwCountersEnabled.load(std::memory_order_relaxed); }

HwCounterScope::HwCounterScope(const double entitiesPerIteration) : entitiesPerIteration_(entitiesPerIteration) {
  if (AreHwCountersEnabled()) {
    startSnapshot_ = PlatformSampleHw();
  }
}

HwCounterScope::~HwCounterScope() {
  if (!stopped_) {
    Stop();
  }
}

void HwCounterScope::Stop() {
  if (stopped_) return;
  if (AreHwCountersEnabled() && startSnapshot_.valid) {
    endSnapshot_ = PlatformSampleHw();
  }
  stopped_ = true;
}

void HwCounterScope::Report(benchmark::State& state) const {
  if (!AreHwCountersEnabled() || !startSnapshot_.valid || !endSnapshot_.valid) return;
  const auto iters = static_cast<double>(state.iterations());
  if (iters <= 0.0) return;

  const uint64_t instDelta = endSnapshot_.instructions >= startSnapshot_.instructions
                                 ? (endSnapshot_.instructions - startSnapshot_.instructions)
                                 : 0;
  const uint64_t cycleDelta =
      endSnapshot_.cycles >= startSnapshot_.cycles ? (endSnapshot_.cycles - startSnapshot_.cycles) : 0;

  state.counters["inst_per_iter"] = static_cast<double>(instDelta) / iters;
  if (cycleDelta > 0) {
    state.counters["ipc"] = static_cast<double>(instDelta) / static_cast<double>(cycleDelta);
  }
  if (entitiesPerIteration_ > 0.0) {
    state.counters["inst_per_entity"] = static_cast<double>(instDelta) / (iters * entitiesPerIteration_);
    state.counters["cycles_per_entity"] = static_cast<double>(cycleDelta) / (iters * entitiesPerIteration_);
  }
}

}  // namespace bench
