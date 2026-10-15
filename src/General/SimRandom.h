#pragma once

#include <cstdint>
#include <random>

// Process-wide simulation RNG for engine systems that need randomness affecting gameplay state
// (e.g. CullDistanceSystem perimeter repositioning). Seeded from std::random_device by default;
// call Seed() (Lua: set_sim_seed) to make runs reproducible for deterministic bot simulations.
// Not thread-safe: only call from systems that run on the main simulation thread.
namespace sim_random {

inline std::mt19937& Engine() {
  static std::mt19937 engine{std::random_device{}()};
  return engine;
}

inline void Seed(const uint32_t seed) { Engine().seed(seed); }

inline float Float01() {
  std::uniform_real_distribution<float> dist(0.0F, 1.0F);
  return dist(Engine());
}

}  // namespace sim_random
