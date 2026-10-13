#include <benchmark/benchmark.h>

#include <cstdlib>
#include <string>

#include "General/Logger.h"

// Custom main: identical to benchmark_main, plus quiet engine logging (EventBus/AssetManager log
// at info on every construction, which used to interleave ~80 lines into the results) and build
// context recorded into the JSON so baselines from different builds aren't compared blindly.
int main(int argc, char** argv) {
  const char* level = std::getenv("OCTARINE_BENCH_LOG_LEVEL");
  Logger::SetLevel(level != nullptr ? level : "error");

#ifdef OCTARINE_BENCH_BUILD_TYPE
  benchmark::AddCustomContext("octarine_build_type", OCTARINE_BENCH_BUILD_TYPE);
#endif
#ifdef OCTARINE_PROFILING
  benchmark::AddCustomContext("octarine_profiling", "on");
#else
  benchmark::AddCustomContext("octarine_profiling", "off");
#endif

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
