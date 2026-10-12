#include "ECS/StateInspector.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

#include "Components/BoxColliderComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/HealthComponent.h"
#include "Components/LifetimeComponent.h"
#include "Components/NameComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/UIRectComponent.h"
#include "ECS/Registry.h"

namespace octarine::inspect {
namespace {

constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

inline uint64_t FnvHashBytes(uint64_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= kFnvPrime;
  }
  return hash;
}

inline uint64_t FnvHashInt64(uint64_t hash, int64_t val) { return FnvHashBytes(hash, &val, sizeof(val)); }

inline int64_t Quantize(float val, float scale) { return static_cast<int64_t>(std::round(val * scale)); }

std::string EscapeJsonString(const std::string& str) {
  std::ostringstream ss;
  for (const char c : str) {
    if (c == '"') {
      ss << "\\\"";
    } else if (c == '\\') {
      ss << "\\\\";
    } else if (c == '\b') {
      ss << "\\b";
    } else if (c == '\f') {
      ss << "\\f";
    } else if (c == '\n') {
      ss << "\\n";
    } else if (c == '\r') {
      ss << "\\r";
    } else if (c == '\t') {
      ss << "\\t";
    } else if (static_cast<unsigned char>(c) < 0x20) {
      ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
    } else {
      ss << c;
    }
  }
  return ss.str();
}

std::string FormatFloat(float val, const InspectOptions& opts) {
  if (opts.quantizeFloats) {
    const float quantized = std::round(val * opts.floatPrecision) / opts.floatPrecision;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4) << quantized;
    return ss.str();
  }
  std::ostringstream ss;
  ss << val;
  return ss.str();
}

void AppendPositionJson(const Registry& registry, Entity entity, const InspectOptions& opts,
                        std::vector<std::string>& comps) {
  if (!registry.HasComponent<PositionComponent>(entity)) return;
  const auto& pos = registry.GetComponent<PositionComponent>(entity);
  std::ostringstream ss;
  ss << "\"PositionComponent\": {\"x\": " << FormatFloat(pos.value.x, opts)
     << ", \"y\": " << FormatFloat(pos.value.y, opts) << "}";
  comps.push_back(ss.str());
}

void AppendRigidBodyJson(const Registry& registry, Entity entity, const InspectOptions& opts,
                         std::vector<std::string>& comps) {
  if (!registry.HasComponent<RigidBodyComponent>(entity)) return;
  const auto& rb = registry.GetComponent<RigidBodyComponent>(entity);
  std::ostringstream ss;
  ss << "\"RigidBodyComponent\": {\"vx\": " << FormatFloat(rb.velocity.x, opts)
     << ", \"vy\": " << FormatFloat(rb.velocity.y, opts) << "}";
  comps.push_back(ss.str());
}

void AppendGlobalTransformJson(const Registry& registry, Entity entity, const InspectOptions& opts,
                               std::vector<std::string>& comps) {
  if (!registry.HasComponent<GlobalTransformComponent>(entity)) return;
  const auto& gt = registry.GetComponent<GlobalTransformComponent>(entity);
  std::ostringstream ss;
  ss << "\"GlobalTransformComponent\": {\"x\": " << FormatFloat(gt.position.x, opts)
     << ", \"y\": " << FormatFloat(gt.position.y, opts) << ", \"scaleX\": " << FormatFloat(gt.scale.x, opts)
     << ", \"scaleY\": " << FormatFloat(gt.scale.y, opts) << ", \"rotation\": " << FormatFloat(gt.rotation, opts)
     << "}";
  comps.push_back(ss.str());
}

void AppendBoxColliderJson(const Registry& registry, Entity entity, const InspectOptions& opts,
                           std::vector<std::string>& comps) {
  if (!registry.HasComponent<BoxColliderComponent>(entity)) return;
  const auto& bc = registry.GetComponent<BoxColliderComponent>(entity);
  std::ostringstream ss;
  ss << "\"BoxColliderComponent\": {\"width\": " << bc.width << ", \"height\": " << bc.height
     << ", \"offsetX\": " << FormatFloat(bc.offset.x, opts) << ", \"offsetY\": " << FormatFloat(bc.offset.y, opts)
     << ", \"isFixed\": " << (bc.isFixed ? "true" : "false") << "}";
  comps.push_back(ss.str());
}

void AppendHealthJson(const Registry& registry, Entity entity, std::vector<std::string>& comps) {
  if (!registry.HasComponent<HealthComponent>(entity)) return;
  const auto& h = registry.GetComponent<HealthComponent>(entity);
  std::ostringstream ss;
  ss << "\"HealthComponent\": {\"currentHealth\": " << h.currentHealth << ", \"maxHealth\": " << h.maxHealth << "}";
  comps.push_back(ss.str());
}

void AppendNameJson(const Registry& registry, Entity entity, std::vector<std::string>& comps) {
  if (!registry.HasComponent<NameComponent>(entity)) return;
  const auto& n = registry.GetComponent<NameComponent>(entity);
  std::ostringstream ss;
  ss << "\"NameComponent\": {\"name\": \"" << EscapeJsonString(n.name) << "\"}";
  comps.push_back(ss.str());
}

void AppendLifetimeJson(const Registry& registry, Entity entity, const InspectOptions& opts,
                        std::vector<std::string>& comps) {
  if (!registry.HasComponent<LifetimeComponent>(entity)) return;
  const auto& lt = registry.GetComponent<LifetimeComponent>(entity);
  std::ostringstream ss;
  ss << "\"LifetimeComponent\": {\"remainingDuration\": " << FormatFloat(lt.remainingDuration, opts)
     << ", \"lifetimeDuration\": " << FormatFloat(lt.lifetimeDuration, opts) << "}";
  comps.push_back(ss.str());
}

void AppendUIRectJson(const Registry& registry, Entity entity, const InspectOptions& opts,
                      std::vector<std::string>& comps) {
  if (!registry.HasComponent<UIRectComponent>(entity)) return;
  const auto& rect = registry.GetComponent<UIRectComponent>(entity);
  std::ostringstream ss;
  ss << "\"UIRectComponent\": {\"left\": " << FormatFloat(rect.left, opts)
     << ", \"top\": " << FormatFloat(rect.top, opts) << ", \"right\": " << FormatFloat(rect.right, opts)
     << ", \"bottom\": " << FormatFloat(rect.bottom, opts) << ", \"layer\": " << rect.layer << "}";
  comps.push_back(ss.str());
}

bool EntityMatchesFilter(const Registry& registry, Entity entity, const std::vector<std::string>& filter) {
  for (const auto& compName : filter) {
    if (compName == "PositionComponent" && !registry.HasComponent<PositionComponent>(entity)) return false;
    if (compName == "RigidBodyComponent" && !registry.HasComponent<RigidBodyComponent>(entity)) return false;
    if (compName == "GlobalTransformComponent" && !registry.HasComponent<GlobalTransformComponent>(entity))
      return false;
    if (compName == "BoxColliderComponent" && !registry.HasComponent<BoxColliderComponent>(entity)) return false;
    if (compName == "HealthComponent" && !registry.HasComponent<HealthComponent>(entity)) return false;
    if (compName == "NameComponent" && !registry.HasComponent<NameComponent>(entity)) return false;
    if (compName == "LifetimeComponent" && !registry.HasComponent<LifetimeComponent>(entity)) return false;
    if (compName == "UIRectComponent" && !registry.HasComponent<UIRectComponent>(entity)) return false;
  }
  return true;
}

}  // namespace

std::string StateInspector::DumpSummary(const Registry& registry, uint64_t currentTick) {
  const auto userEntities = registry.GetUserEntities();
  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"tick\": " << currentTick << ",\n";
  ss << "  \"entity_count\": " << registry.GetEntityCount() << ",\n";
  ss << "  \"user_entity_count\": " << userEntities.size() << "\n";
  ss << "}";
  return ss.str();
}

std::string StateInspector::DumpEntities(const Registry& registry) {
  const auto userEntities = registry.GetUserEntities();
  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"count\": " << userEntities.size() << ",\n";
  ss << "  \"entities\": [\n";
  for (size_t i = 0; i < userEntities.size(); ++i) {
    const Entity e = userEntities[i];
    ss << "    {\"id\": " << e.GetId() << ", \"generation\": " << e.GetGeneration()
       << ", \"active\": " << (registry.IsActive(e) ? "true" : "false") << "}";
    if (i + 1 < userEntities.size()) ss << ",";
    ss << "\n";
  }
  ss << "  ]\n";
  ss << "}";
  return ss.str();
}

std::string StateInspector::DumpEntity(const Registry& registry, Entity entity, const InspectOptions& opts) {
  if (!registry.IsAlive(entity)) {
    return "{\"error\": \"Entity is not alive\"}";
  }

  std::vector<std::string> comps;
  AppendPositionJson(registry, entity, opts, comps);
  AppendRigidBodyJson(registry, entity, opts, comps);
  AppendGlobalTransformJson(registry, entity, opts, comps);
  AppendBoxColliderJson(registry, entity, opts, comps);
  AppendHealthJson(registry, entity, comps);
  AppendNameJson(registry, entity, comps);
  AppendLifetimeJson(registry, entity, opts, comps);
  AppendUIRectJson(registry, entity, opts, comps);

  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"id\": " << entity.GetId() << ",\n";
  ss << "  \"generation\": " << entity.GetGeneration() << ",\n";
  ss << "  \"active\": " << (registry.IsActive(entity) ? "true" : "false") << ",\n";
  ss << "  \"components\": {\n";
  for (size_t i = 0; i < comps.size(); ++i) {
    ss << "    " << comps[i];
    if (i + 1 < comps.size()) ss << ",";
    ss << "\n";
  }
  ss << "  }\n";
  ss << "}";
  return ss.str();
}

std::string StateInspector::QueryComponents(const Registry& registry, const std::vector<std::string>& componentNames,
                                            const InspectOptions& opts) {
  const auto userEntities = registry.GetUserEntities();
  std::vector<Entity> matches;
  for (const Entity e : userEntities) {
    if (EntityMatchesFilter(registry, e, componentNames)) {
      matches.push_back(e);
    }
  }

  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"matches\": " << matches.size() << ",\n";
  ss << "  \"entities\": [\n";
  for (size_t i = 0; i < matches.size(); ++i) {
    ss << DumpEntity(registry, matches[i], opts);
    if (i + 1 < matches.size()) ss << ",";
    ss << "\n";
  }
  ss << "  ]\n";
  ss << "}";
  return ss.str();
}

uint64_t StateInspector::ComputeStateHash(const Registry& registry, float floatScale) {
  uint64_t hash = kFnvOffsetBasis;
  const auto userEntities = registry.GetUserEntities();

  for (const Entity e : userEntities) {
    const uint32_t id = e.GetId();
    const uint32_t gen = e.GetGeneration();
    hash = FnvHashInt64(hash, static_cast<int64_t>(id));
    hash = FnvHashInt64(hash, static_cast<int64_t>(gen));
    hash = FnvHashInt64(hash, registry.IsActive(e) ? 1 : 0);

    if (registry.HasComponent<PositionComponent>(e)) {
      const auto& pos = registry.GetComponent<PositionComponent>(e);
      hash = FnvHashInt64(hash, Quantize(pos.value.x, floatScale));
      hash = FnvHashInt64(hash, Quantize(pos.value.y, floatScale));
    }
    if (registry.HasComponent<RigidBodyComponent>(e)) {
      const auto& rb = registry.GetComponent<RigidBodyComponent>(e);
      hash = FnvHashInt64(hash, Quantize(rb.velocity.x, floatScale));
      hash = FnvHashInt64(hash, Quantize(rb.velocity.y, floatScale));
    }
    if (registry.HasComponent<GlobalTransformComponent>(e)) {
      const auto& gt = registry.GetComponent<GlobalTransformComponent>(e);
      hash = FnvHashInt64(hash, Quantize(gt.position.x, floatScale));
      hash = FnvHashInt64(hash, Quantize(gt.position.y, floatScale));
      hash = FnvHashInt64(hash, Quantize(gt.scale.x, floatScale));
      hash = FnvHashInt64(hash, Quantize(gt.scale.y, floatScale));
      hash = FnvHashInt64(hash, Quantize(gt.rotation, floatScale));
    }
    if (registry.HasComponent<HealthComponent>(e)) {
      const auto& h = registry.GetComponent<HealthComponent>(e);
      hash = FnvHashInt64(hash, h.currentHealth);
      hash = FnvHashInt64(hash, h.maxHealth);
    }
  }

  return hash;
}

std::string StateInspector::DiffSnapshots(const std::string& expectedJson, const std::string& actualJson) {
  if (expectedJson == actualJson) return "";
  std::vector<std::string> expectedLines;
  std::vector<std::string> actualLines;
  std::istringstream issExp(expectedJson);
  std::istringstream issAct(actualJson);
  std::string line;
  while (std::getline(issExp, line)) expectedLines.push_back(line);
  while (std::getline(issAct, line)) actualLines.push_back(line);

  std::ostringstream ss;
  ss << "State Divergence Detected:\n";
  const size_t maxLines = std::max(expectedLines.size(), actualLines.size());
  for (size_t i = 0; i < maxLines; ++i) {
    const std::string& e = (i < expectedLines.size()) ? expectedLines[i] : "";
    const std::string& a = (i < actualLines.size()) ? actualLines[i] : "";
    if (e != a) {
      if (i < expectedLines.size()) ss << "- " << e << "\n";
      if (i < actualLines.size()) ss << "+ " << a << "\n";
    }
  }
  return ss.str();
}

}  // namespace octarine::inspect
