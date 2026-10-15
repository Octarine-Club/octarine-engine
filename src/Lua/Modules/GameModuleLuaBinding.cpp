#include "Lua/Modules/GameModuleLuaBinding.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <glm/glm.hpp>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#include "Components/CameraComponents.h"
#include "ECS/Registry.h"
#include "ECS/StateInspector.h"
#include "Game/GameConfig.h"
#include "General/FileSandbox.h"
#include "General/Logger.h"
#include "General/SimRandom.h"
#include "General/ThreadPool.h"
#include "Lua/LuaBindingContext.h"
#include "Systems/ProjectileEmitSystem.h"

namespace {
constexpr size_t kDefaultMaxDepth = 5;

bool IsAllowedEnvVar(const std::string_view name) {
  constexpr std::array<std::string_view, 4> kAllowedPrefixes = {"OCT_", "OCTARINE_", "GAME_", "BOT_"};
  return std::any_of(kAllowedPrefixes.begin(), kAllowedPrefixes.end(),
                     [name](const std::string_view prefix) { return name.starts_with(prefix); });
}

std::string ReadEnvVar(const std::string& name) {
#ifdef _MSC_VER
  char* value = nullptr;
  size_t len = 0;
  const bool ok = _dupenv_s(&value, &len, name.c_str()) == 0 && value != nullptr;
  const std::string result = ok ? std::string(value) : std::string();
  free(value);
  return result;
#else
  const char* value = std::getenv(name.c_str());
  return value ? std::string(value) : std::string();
#endif
}

// NOLINTNEXTLINE(misc-no-recursion)
std::string FormatLuaTable(const sol::table& table, size_t indent_level = 0, size_t max_depth = kDefaultMaxDepth) {
  if (indent_level > max_depth) {
    return "{ ... }";
  }
  std::ostringstream result;
  std::string indent(indent_level * 2, ' ');
  std::string inner_indent((indent_level + 1) * 2, ' ');
  result << "{\n";
  for (const auto& kv : table) {
    result << inner_indent;

    // Key
    if (kv.first.get_type() == sol::type::string) {
      result << kv.first.as<std::string>();
    } else if (kv.first.get_type() == sol::type::number) {
      result << "[" << kv.first.as<double>() << "]";
    } else {
      result << "[" << lua_typename(table.lua_state(), static_cast<int>(kv.first.get_type())) << "]";
    }

    result << " = ";

    // Value
    if (kv.second.get_type() == sol::type::string) {
      result << "\"" << kv.second.as<std::string>() << "\"";
    } else if (kv.second.get_type() == sol::type::number) {
      result << kv.second.as<double>();
    } else if (kv.second.get_type() == sol::type::boolean) {
      result << (kv.second.as<bool>() ? "true" : "false");
    } else if (kv.second.get_type() == sol::type::table) {
      result << FormatLuaTable(kv.second.as<sol::table>(), indent_level + 1, max_depth);
    } else {
      result << "<" << lua_typename(table.lua_state(), static_cast<int>(kv.second.get_type())) << ">";
    }
    result << ",\n";
  }
  result << indent << "}";
  return result.str();
}
}  // namespace

void LuaModuleBinding<GameModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  lua.set_function("log_table", [](const sol::table& table, const sol::optional<std::string>& message) {
    std::string prefix = message.has_value() ? (message.value() + ": ") : "";
    Logger::InfoLua(prefix + FormatLuaTable(table));
  });

  lua.set_function("quit_game", [&ctx]() { ctx.RequestQuit(); });

  // Toggle the built-in performance overlay (FPS + frame time) at runtime. Placement and which
  // metrics show are config.ini knobs (PerfOverlayCorner / PerfOverlayMetrics); this just flips it on
  // or off so scripts can gate it behind a debug menu or key.
  lua.set_function("set_perf_overlay", [&ctx](const bool enabled) {
    ctx.GetRegistry()->Get<GameConfig>().GetEngineOptions().showPerfOverlay = enabled;
  });

  // Flip the perf overlay and return the new state, so a debug-key callback doesn't have to track
  // the current value itself.
  lua.set_function("toggle_perf_overlay", [&ctx]() {
    auto& options = ctx.GetRegistry()->Get<GameConfig>().GetEngineOptions();
    options.showPerfOverlay = !options.showPerfOverlay;
    return options.showPerfOverlay;
  });

  lua.set_function("set_sim_substeps", [&ctx](const int substeps) {
    auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>();
    if (!gameConfig) return;
    gameConfig->GetEngineOptions().simSubSteps = std::clamp(substeps, 1, 64);
  });

  lua.set_function("get_sim_substeps", [&ctx]() -> int {
    const auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>();
    if (!gameConfig) return 1;
    return gameConfig->GetEngineOptions().simSubSteps;
  });

  lua.set_function("set_fixed_dt", [&ctx](const double dt) {
    auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>();
    if (!gameConfig) return;
    gameConfig->GetEngineOptions().fixedStepDt = std::max(0.0F, static_cast<float>(dt));
  });

  lua.set_function("get_fixed_dt", [&ctx]() -> double {
    const auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>();
    if (!gameConfig) return 0.0;
    return static_cast<double>(gameConfig->GetEngineOptions().fixedStepDt);
  });

  // Deterministic simulation mode: forces ECS ParallelForEach / ThreadPool::ParallelChunks to run
  // serially and KDTreeCollisionSystem to run synchronously, so a seeded run replays bit-identically.
  // Costs multi-core throughput; intended for bot simulations, replays and A/B experiments.
  lua.set_function("set_deterministic", [&ctx](const bool enabled) {
    ThreadPool::SetForceSerial(enabled);
    if (auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>()) {
      gameConfig->GetEngineOptions().deterministic = enabled;
    }
  });

  lua.set_function("is_deterministic", []() -> bool { return ThreadPool::IsForceSerial(); });

  lua.set_function("set_fps_target", [&ctx](const int fps) {
    if (auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>()) {
      gameConfig->GetEngineOptions().fpsTarget = std::max(0, fps);
    }
  });

  lua.set_function("get_fps_target", [&ctx]() -> int {
    const auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>();
    if (!gameConfig) return 0;
    return gameConfig->GetEngineOptions().fpsTarget;
  });

  lua.set_function("set_headless", [&ctx](const bool enabled) {
    if (auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>()) {
      auto& opts = gameConfig->GetEngineOptions();
      opts.headless = enabled;
      if (enabled) {
        opts.fpsTarget = 0;
        opts.audioEnabled = false;
      }
    }
  });

  lua.set_function("is_headless", [&ctx]() -> bool {
    const auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>();
    if (!gameConfig) return false;
    return gameConfig->GetEngineOptions().headless;
  });

  // Seeds the engine-side simulation RNG (e.g. CullDistanceSystem perimeter repositioning).
  // Lua's math.randomseed covers script randomness; this covers C++ systems.
  lua.set_function("set_sim_seed",
                   [](const double seed) { sim_random::Seed(static_cast<uint32_t>(static_cast<int64_t>(seed))); });

  // Canonical FNV-1a hash of all alive entities' component state (floats quantized). Returned as a
  // 16-char hex string because Lua integers cannot represent the full unsigned 64-bit range.
  lua.set_function("get_state_hash", [&ctx]() -> std::string {
    const uint64_t hash = octarine::inspect::StateInspector::ComputeStateHash(*ctx.GetRegistry());
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
  });

  // Writes the canonical per-entity state (fields covered by get_state_hash) to a project-relative
  // file so two diverging runs can be diffed entity-by-entity.
  lua.set_function("write_state_dump", [&ctx](const std::string& relativePath) -> bool {
    const std::string root = ctx.GetProjectPath();
    if (root.empty()) return false;
    const std::string fullPath = file_sandbox::Resolve(root, relativePath);
    if (fullPath.empty()) return false;
    return file_sandbox::WriteFileAtomic(fullPath,
                                         octarine::inspect::StateInspector::DumpCanonicalState(*ctx.GetRegistry()));
  });

  // Environment access is restricted to allowlisted prefixes so game/mod scripts cannot exfiltrate
  // arbitrary process secrets (API tokens, credentials). Harnesses should use these prefixes.
  lua.set_function("get_env", [](const std::string& name) -> sol::optional<std::string> {
    if (!IsAllowedEnvVar(name)) {
      Logger::Warn("get_env: '" + name + "' rejected (allowed prefixes: OCT_, OCTARINE_, GAME_, BOT_)");
      return sol::nullopt;
    }
    const std::string val = ReadEnvVar(name);
    if (val.empty()) return sol::nullopt;
    return val;
  });

  lua.set_function("write_project_file", [&ctx](const std::string& relativePath, const std::string& contents) -> bool {
    const std::string root = ctx.GetProjectPath();
    if (root.empty()) return false;
    const std::string fullPath = file_sandbox::Resolve(root, relativePath);
    if (fullPath.empty()) return false;
    return file_sandbox::WriteFileAtomic(fullPath, contents);
  });

  lua.set_function("set_game_map_dimensions", [&ctx](const double width, const double height) {
    auto& gameConfig = ctx.GetRegistry()->Get<GameConfig>();
    gameConfig.playableAreaHeight = static_cast<float>(height);
    gameConfig.playableAreaWidth = static_cast<float>(width);
  });

  lua.set_function("get_game_map_dimensions", [&ctx]() {
    const auto& gameConfig = ctx.GetRegistry()->Get<GameConfig>();
    return glm::vec2(gameConfig.playableAreaWidth, gameConfig.playableAreaHeight);
  });

  lua.set_function("get_camera_position", [&ctx]() {
    const auto& camera = ctx.GetRegistry()->Get<CameraComponent>();
    return glm::vec2(camera.viewport.x, camera.viewport.y);
  });

  lua.set_function("set_camera_zoom", [&ctx](const double zoom) {
    auto* registry = ctx.GetRegistry();
    auto* camera = registry->TryGet<CameraComponent>();
    if (!camera) return;
    camera->zoom = std::clamp(static_cast<float>(zoom), 0.1f, 10.0f);
    if (const auto* gameConfig = registry->TryGet<GameConfig>()) {
      camera->viewport.w = static_cast<float>(gameConfig->windowWidth) / camera->zoom;
      camera->viewport.h = static_cast<float>(gameConfig->windowHeight) / camera->zoom;
    }
  });

  lua.set_function("get_camera_zoom", [&ctx]() {
    const auto* camera = ctx.GetRegistry()->TryGet<CameraComponent>();
    if (!camera) return 1.0;
    return static_cast<double>(camera->zoom);
  });

  // Manual fire from Lua. Player-tagged emitters skip auto-fire — gameplay code drives shots
  // via input callbacks. dx/dy is a non-normalized aim vector; 0,0 falls back to the
  // emitter's configured velocity.
  lua.set_function(
      "fire_projectile", [&ctx](const Entity emitter, const sol::optional<double> dx, const sol::optional<double> dy) {
        auto* registry = ctx.GetRegistry();
        auto& projectileEmitSystem = registry->Get<ProjectileEmitSystem>();
        projectileEmitSystem.Fire(
            *registry, emitter, glm::vec2(static_cast<float>(dx.value_or(0.0)), static_cast<float>(dy.value_or(0.0))));
      });
}
