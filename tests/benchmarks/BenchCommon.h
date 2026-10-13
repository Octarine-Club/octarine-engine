#pragma once

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "ECS/Registry.h"

#if defined(__GNUG__)
#include <cxxabi.h>
#endif

// Shared helpers for every OctarineBenchmarks translation unit.
//
// Naming convention (filterable with --benchmark_filter):
//   ECS/...       core registry primitives (iteration, transitions, churn, queries)
//   Sys/...       one engine system in isolation on a dash-vs-shaped population
//   Pipeline/...  a chain of systems run through Registry::Update
//   Frame/...     the full headless simulation tier (RegisterSimulationSystems + render prep)
//   Misc/...      peripheral subsystems (audio, text cache, pools)
//
// Standard counters (present wherever they make sense) so agents can compare runs uniformly:
//   entities        live user entities at the end of the run
//   archetypes      archetype count at the end of the run
//   ns_per_entity   wall time per processed entity per iteration
//   sys_<Name>_us   mean per-iteration time of a registered system (Pipeline/Frame tiers)
namespace bench {

constexpr float kDt = 1.0f / 60.0f;
constexpr double kBudget60Ns = 1.0e9 / 60.0;
constexpr double kBudget120Ns = 1.0e9 / 120.0;

using Clock = std::chrono::steady_clock;

inline double ElapsedNs(const Clock::time_point start) {
  return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

// Entity-count sweeps used across tiers. Upper end deliberately exceeds what dash-vs runs today so
// scaling cliffs show up before gameplay hits them.
inline void EntitySweep(benchmark::Benchmark* b) {
  for (const int n : {1000, 5000, 10000, 25000, 50000, 100000}) b->Arg(n);
}

inline void EntitySweepSmall(benchmark::Benchmark* b) {
  for (const int n : {1000, 5000, 10000, 25000}) b->Arg(n);
}

inline void ReportRegistry(benchmark::State& state, const Registry& registry) {
  state.counters["entities"] = static_cast<double>(registry.GetUserEntityCount());
  state.counters["archetypes"] = static_cast<double>(registry.GetArchetypeCount());
}

// Per-entity throughput. ns_per_entity is an inverted rate, so it is derived from whichever time
// the benchmark reports (cpu, real, or manual) and printed with time units on the console; in
// JSON it is in seconds per entity. Frame-budget share is derived from the iteration time by
// scripts/bench_compare.py rather than stored as a counter.
inline void ReportPerEntity(benchmark::State& state, const double entitiesPerIteration) {
  state.SetItemsProcessed(static_cast<int64_t>(static_cast<double>(state.iterations()) * entitiesPerIteration));
  state.counters["ns_per_entity"] = benchmark::Counter(entitiesPerIteration * static_cast<double>(state.iterations()),
                                                       benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// typeid-derived system names can be raw manglings for wrapper types
// (NSt3__117reference_wrapperI12CombatSystemEE) and carry namespaces; collapse them to the bare
// class name for counter keys.
inline std::string ShortSystemName(const std::string& raw) {
  std::string name = raw;
#if defined(__GNUG__)
  int status = 0;
  char* demangled = abi::__cxa_demangle(raw.c_str(), nullptr, nullptr, &status);
  if (status == 0 && demangled != nullptr) name = demangled;
  std::free(demangled);  // NOLINT(cppcoreguidelines-no-malloc) — __cxa_demangle allocates with malloc
#endif
  std::string_view view(name);
  if (const auto open = view.rfind('<'); open != std::string_view::npos) {
    const auto close = view.find('>', open);
    view = view.substr(open + 1, close == std::string_view::npos ? std::string_view::npos : close - open - 1);
  }
  if (const auto colon = view.rfind("::"); colon != std::string_view::npos) {
    view = view.substr(colon + 2);
  }
  std::string out(view);
  out.erase(std::remove_if(out.begin(), out.end(), [](char c) { return c == ' ' || c == '*' || c == '&'; }), out.end());
  return out;
}

// Accumulates Registry's opt-in per-system timings across iterations and publishes them as
// sys_<Name>_us counters (mean per iteration) plus flush_us for the end-of-frame despawn flush.
class SystemTimings {
 public:
  explicit SystemTimings(Registry& registry) : registry_(registry) { registry_.SetSystemTimingEnabled(true); }
  ~SystemTimings() { registry_.SetSystemTimingEnabled(false); }
  SystemTimings(const SystemTimings&) = delete;
  SystemTimings& operator=(const SystemTimings&) = delete;

  void Sample() {
    const auto& timings = registry_.GetLastSystemTimingsNs();
    if (totals_.size() < timings.size()) totals_.resize(timings.size(), 0.0);
    for (size_t i = 0; i < timings.size(); ++i) totals_[i] += static_cast<double>(timings[i]);
    flush_total_ += static_cast<double>(registry_.GetLastFlushTimingNs());
    ++samples_;
  }

  void Report(benchmark::State& state) const {
    if (samples_ == 0) return;
    const auto n = static_cast<double>(samples_);
    std::map<std::string, double> merged;  // Two registrations of one type (rare) sum together.
    for (size_t i = 0; i < totals_.size(); ++i) {
      merged["sys_" + ShortSystemName(registry_.GetSystemName(i)) + "_us"] += totals_[i] / n / 1000.0;
    }
    for (const auto& [key, us] : merged) state.counters[key] = us;
    state.counters["flush_us"] = flush_total_ / n / 1000.0;
  }

 private:
  Registry& registry_;
  std::vector<double> totals_;
  double flush_total_ = 0.0;
  int64_t samples_ = 0;
};

// Per-frame latency distribution for manual-time frame benchmarks: Google Benchmark only reports
// the mean, but frame pacing is governed by the tail.
class FrameStats {
 public:
  void Add(const double ns) { samples_.push_back(ns); }

  void Report(benchmark::State& state) {
    if (samples_.empty()) return;
    std::sort(samples_.begin(), samples_.end());
    const auto pct = [&](const double p) {
      const auto idx = static_cast<size_t>(p * static_cast<double>(samples_.size() - 1));
      return samples_[idx] / 1.0e6;
    };
    state.counters["p50_ms"] = pct(0.50);
    state.counters["p95_ms"] = pct(0.95);
    state.counters["p99_ms"] = pct(0.99);
    state.counters["max_ms"] = samples_.back() / 1.0e6;
    const auto over60 = static_cast<double>(
        std::count_if(samples_.begin(), samples_.end(), [](const double v) { return v > kBudget60Ns; }));
    state.counters["over60_pct"] = 100.0 * over60 / static_cast<double>(samples_.size());
  }

 private:
  std::vector<double> samples_;
};

}  // namespace bench
