#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ECS/Entity.h"

class Registry;

namespace octarine::inspect {

constexpr float kDefaultFloatPrecision = 10000.0f;

struct InspectOptions {
  bool quantizeFloats = true;
  float floatPrecision = kDefaultFloatPrecision;
  std::vector<std::string> componentFilter;
};

class StateInspector {
 public:
  // Serializes high-level engine summary (entity count, active chunk metrics, current tick) into JSON.
  static std::string DumpSummary(const Registry& registry, uint64_t currentTick = 0);

  // Serializes the full catalog of alive entities and their attached component type names into JSON.
  static std::string DumpEntities(const Registry& registry);

  // Serializes all components and field values for a single entity into JSON.
  static std::string DumpEntity(const Registry& registry, Entity entity, const InspectOptions& opts = {});

  // Queries all entities that possess all specified component names and dumps their state into JSON.
  static std::string QueryComponents(const Registry& registry, const std::vector<std::string>& componentNames,
                                     const InspectOptions& opts = {});

  // Computes a deterministic 64-bit canonical FNV-1a state hash with float quantization.
  static uint64_t ComputeStateHash(const Registry& registry, float floatScale = kDefaultFloatPrecision);

  // Compares two JSON state snapshots line-by-line and formats a human-readable diff.
  static std::string DiffSnapshots(const std::string& expectedJson, const std::string& actualJson);
};

}  // namespace octarine::inspect
