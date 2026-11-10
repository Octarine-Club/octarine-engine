#include <benchmark/benchmark.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "BenchAllocTracker.h"
#include "BenchHwCounters.h"
#include "General/Logger.h"

// Custom main: identical to benchmark_main, plus quiet engine logging (EventBus/AssetManager log
// at info on every construction, which used to interleave ~80 lines into the results) and build
// context recorded into the JSON so baselines from different builds aren't compared blindly.
int main(int argc, char** argv) {
  const char* level = std::getenv("OCTARINE_BENCH_LOG_LEVEL");
  Logger::SetLevel(level != nullptr ? level : "error");

  bool enableHwCounters = false;
  if (const char* envHw = std::getenv("OCTARINE_BENCH_HW_COUNTERS")) {
    if (std::strcmp(envHw, "1") == 0 || std::strcmp(envHw, "true") == 0) {
      enableHwCounters = true;
    }
  }

  // Filter out custom flags before Google Benchmark parses arguments.
  int newArgc = 1;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--hw-counters") == 0) {
      enableHwCounters = true;
    } else {
      argv[newArgc++] = argv[i];
    }
  }
  argc = newArgc;

  bench::SetHwCountersEnabled(enableHwCounters);

#ifdef OCTARINE_BENCH_BUILD_TYPE
  benchmark::AddCustomContext("octarine_build_type", OCTARINE_BENCH_BUILD_TYPE);
#endif
#ifdef OCTARINE_PROFILING
  benchmark::AddCustomContext("octarine_profiling", "on");
#else
  benchmark::AddCustomContext("octarine_profiling", "off");
#endif
  benchmark::AddCustomContext("octarine_hw_counters", enableHwCounters ? "on" : "off");
  benchmark::AddCustomContext("octarine_alloc_tracking",
#if !defined(OCTARINE_HAS_SANITIZER)
                              "on"
#else
                              "sanitizer_disabled"
#endif
  );

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
