#include "Lua/Modules/GameModuleLuaBinding.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <glm/glm.hpp>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

#include "Components/CameraComponents.h"
#include "ECS/Registry.h"
#include "ECS/StateInspector.h"
#include "Engine/EngineContext.h"
#include "Game/GameConfig.h"
#include "Game/UserSettings.h"
#include "General/FileSandbox.h"
#include "General/Logger.h"
#include "General/SimRandom.h"
#include "General/ThreadPool.h"
#include "Lua/LuaBindingContext.h"
#ifdef OCTARINE_WITH_IMGUI
#include "Systems/RenderDebugGUISystem.h"
#endif

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

octarine::UserSettings* GetOrCreateUserSettings(LuaBindingContext& ctx) {
  auto* registry = ctx.GetRegistry();
  if (registry == nullptr) return nullptr;
  if (auto* settings = registry->TryGet<octarine::UserSettings>()) {
    return settings;
  }
  return &registry->Set<octarine::UserSettings>(octarine::UserSettings{});
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

    if (kv.first.get_type() == sol::type::string) {
      result << kv.first.as<std::string>();
    } else if (kv.first.get_type() == sol::type::number) {
      result << "[" << kv.first.as<double>() << "]";
    } else {
      result << "[" << lua_typename(table.lua_state(), static_cast<int>(kv.first.get_type())) << "]";
    }

    result << " = ";

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

void SetPerfOverlayValue(LuaBindingContext& ctx, const bool enabled) {
  if (auto* config = ctx.GetRegistry()->TryGet<GameConfig>()) {
    config->GetEngineOptions().showPerfOverlay = enabled;
  }
  if (auto* settings = GetOrCreateUserSettings(ctx)) {
    settings->showPerfOverlay = enabled;
  }
}

void BindDiagnostics(sol::state& lua, LuaBindingContext& ctx) {
  lua.set_function("log_table", [](const sol::table& table, const sol::optional<std::string>& message) {
    std::string prefix = message.has_value() ? (message.value() + ": ") : "";
    Logger::InfoLua(prefix + FormatLuaTable(table));
  });

  lua.set_function("quit_game", [&ctx]() { ctx.RequestQuit(); });

  lua.set_function("set_perf_overlay", [&ctx](const bool enabled) { SetPerfOverlayValue(ctx, enabled); });

  lua.set_function("toggle_perf_overlay", [&ctx]() {
    auto& options = ctx.GetRegistry()->Get<GameConfig>().GetEngineOptions();
    options.showPerfOverlay = !options.showPerfOverlay;
    if (auto* settings = GetOrCreateUserSettings(ctx)) {
      settings->showPerfOverlay = options.showPerfOverlay;
    }
    return options.showPerfOverlay;
  });

  lua.set_function("set_debug_gui", [&ctx](const bool enabled) {
    auto& options = ctx.GetRegistry()->Get<GameConfig>().GetEngineOptions();
    options.showDebugGUI = enabled;
#ifdef OCTARINE_WITH_IMGUI
    if (!options.showDebugGUI) {
      RenderDebugGUISystem::ReturnFocusToGame(ctx.GetRegistry());
    }
#endif
  });

  lua.set_function("toggle_debug_gui", [&ctx]() {
    auto& options = ctx.GetRegistry()->Get<GameConfig>().GetEngineOptions();
    options.showDebugGUI = !options.showDebugGUI;
#ifdef OCTARINE_WITH_IMGUI
    if (!options.showDebugGUI) {
      RenderDebugGUISystem::ReturnFocusToGame(ctx.GetRegistry());
    }
#endif
    return options.showDebugGUI;
  });
}

void BindSubsteppingAndDt(sol::state& lua, LuaBindingContext& ctx) {
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

  lua.set_function("set_sim_seed",
                   [](const double seed) { sim_random::Seed(static_cast<uint32_t>(static_cast<int64_t>(seed))); });
}

void SetFpsTargetValue(LuaBindingContext& ctx, const int fps) {
  const int clamped = std::max(0, fps);
  if (auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>()) {
    gameConfig->GetEngineOptions().fpsTarget = clamped;
  }
  if (auto* settings = GetOrCreateUserSettings(ctx)) {
    settings->fpsTarget = clamped;
  }
}

void BindExecutionEnvironment(sol::state& lua, LuaBindingContext& ctx) {
  lua.set_function("set_deterministic", [&ctx](const bool enabled) {
    ThreadPool::SetForceSerial(enabled);
    if (auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>()) {
      gameConfig->GetEngineOptions().deterministic = enabled;
    }
  });

  lua.set_function("is_deterministic", []() -> bool { return ThreadPool::IsForceSerial(); });

  lua.set_function("set_fps_target", [&ctx](const int fps) { SetFpsTargetValue(ctx, fps); });

  lua.set_function("get_fps_target", [&ctx]() -> int {
    if (const auto* gameConfig = ctx.GetRegistry()->TryGet<GameConfig>()) {
      return gameConfig->GetEngineOptions().fpsTarget;
    }
    if (const auto* settings = GetOrCreateUserSettings(ctx); settings && settings->fpsTarget.has_value()) {
      return *settings->fpsTarget;
    }
    return 0;
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
}

void BindStateInspection(sol::state& lua, LuaBindingContext& ctx) {
  lua.set_function("get_state_hash", [&ctx]() -> std::string {
    const uint64_t hash = octarine::inspect::StateInspector::ComputeStateHash(*ctx.GetRegistry());
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
  });

  lua.set_function("write_state_dump", [&ctx](const std::string& relativePath) -> bool {
    const std::string root = ctx.GetProjectPath();
    if (root.empty()) return false;
    const std::string fullPath = file_sandbox::Resolve(root, relativePath);
    if (fullPath.empty()) return false;
    return file_sandbox::WriteFileAtomic(fullPath,
                                         octarine::inspect::StateInspector::DumpCanonicalState(*ctx.GetRegistry()));
  });

  lua.set_function("get_env", [](const std::string& name) -> sol::optional<std::string> {
    if (!IsAllowedEnvVar(name)) {
      Logger::Warn("get_env: '" + name + "' rejected (allowed prefixes: OCT_, OCTARINE_, GAME_, BOT_)");
      return sol::nullopt;
    }
    std::string val = ReadEnvVar(name);
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
}

void BindWorldAndCamera(sol::state& lua, LuaBindingContext& ctx) {
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
}

SDL_Window* TryGetWindow(LuaBindingContext& ctx) {
  auto* registry = ctx.GetRegistry();
  if (registry == nullptr) return nullptr;
  if (const auto* engineCtx = registry->TryGet<EngineContext>()) {
    return engineCtx->sdlWindow;
  }
  return nullptr;
}

bool MoveWindowToDisplay([[maybe_unused]] SDL_Window* window, [[maybe_unused]] int displayIndex) {
#ifdef OCTARINE_PLATFORM_MOBILE
  return false;
#else
  if (window == nullptr) return false;
  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  if (displays == nullptr || displayIndex < 0 || displayIndex >= count) {
    if (displays != nullptr) SDL_free(displays);
    return false;
  }

  const SDL_DisplayID targetDisplay = displays[displayIndex];
  SDL_free(displays);

  SDL_Rect usableBounds{0, 0, 0, 0};
  if (!SDL_GetDisplayUsableBounds(targetDisplay, &usableBounds)) {
    SDL_GetDisplayBounds(targetDisplay, &usableBounds);
  }

  int w = 0;
  int h = 0;
  SDL_GetWindowSize(window, &w, &h);
  if (w <= 0 || h <= 0) {
    w = 1280;
    h = 720;
  }
  const int x = usableBounds.x + (usableBounds.w - w) / 2;
  const int y = usableBounds.y + (usableBounds.h - h) / 2;

  SDL_SetWindowPosition(window, x, y);
  return true;
#endif
}

bool GetFullscreenState(LuaBindingContext& ctx) {
  if (auto* window = TryGetWindow(ctx)) {
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
  }
  if (const auto* settings = GetOrCreateUserSettings(ctx)) {
    return settings->fullscreen;
  }
  return false;
}

void SetFullscreenState(LuaBindingContext& ctx, const bool enabled) {
  if (auto* settings = GetOrCreateUserSettings(ctx)) {
    settings->fullscreen = enabled;
  }
#ifndef OCTARINE_PLATFORM_MOBILE
  if (auto* window = TryGetWindow(ctx)) {
    SDL_SetWindowFullscreen(window, enabled);
    if (auto* settings = GetOrCreateUserSettings(ctx)) {
      settings->CaptureWindow(window);
    }
  }
#endif
}

bool GetMaximizedState(LuaBindingContext& ctx) {
  if (auto* window = TryGetWindow(ctx)) {
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
  }
  if (const auto* settings = GetOrCreateUserSettings(ctx)) {
    return settings->maximized;
  }
  return false;
}

void SetMaximizedState(LuaBindingContext& ctx, const bool enabled) {
  if (auto* settings = GetOrCreateUserSettings(ctx)) {
    settings->maximized = enabled;
  }
#ifndef OCTARINE_PLATFORM_MOBILE
  if (auto* window = TryGetWindow(ctx)) {
    if (enabled) {
      SDL_MaximizeWindow(window);
    } else {
      SDL_RestoreWindow(window);
    }
    if (auto* settings = GetOrCreateUserSettings(ctx)) {
      settings->CaptureWindow(window);
    }
  }
#endif
}

std::tuple<int, int> GetWindowSizeState(LuaBindingContext& ctx) {
  if (auto* window = TryGetWindow(ctx)) {
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window, &w, &h);
    return {w, h};
  }
  if (const auto* settings = GetOrCreateUserSettings(ctx)) {
    return {settings->windowWidth, settings->windowHeight};
  }
  return {0, 0};
}

void SetWindowSizeState(LuaBindingContext& ctx, const int w, const int h) {
  if (w <= 0 || h <= 0) return;
  if (auto* settings = GetOrCreateUserSettings(ctx)) {
    settings->windowWidth = w;
    settings->windowHeight = h;
  }
#ifndef OCTARINE_PLATFORM_MOBILE
  if (auto* window = TryGetWindow(ctx)) {
    SDL_SetWindowSize(window, w, h);
    if (auto* settings = GetOrCreateUserSettings(ctx)) {
      settings->CaptureWindow(window);
    }
  }
#endif
}

std::tuple<int, int> GetWindowPositionState(LuaBindingContext& ctx) {
  if (auto* window = TryGetWindow(ctx)) {
    int x = 0;
    int y = 0;
    SDL_GetWindowPosition(window, &x, &y);
    return {x, y};
  }
  if (const auto* settings = GetOrCreateUserSettings(ctx)) {
    return {settings->windowX, settings->windowY};
  }
  return {0, 0};
}

void SetWindowPositionState(LuaBindingContext& ctx, const int x, const int y) {
  if (auto* settings = GetOrCreateUserSettings(ctx)) {
    settings->windowX = x;
    settings->windowY = y;
    settings->hasWindowPlacement = true;
  }
#ifndef OCTARINE_PLATFORM_MOBILE
  if (auto* window = TryGetWindow(ctx)) {
    SDL_SetWindowPosition(window, x, y);
    if (auto* settings = GetOrCreateUserSettings(ctx)) {
      settings->CaptureWindow(window);
    }
  }
#endif
}

std::optional<int> FindWindowDisplayIndex(SDL_Window* window) {
  if (window == nullptr) return std::nullopt;
  const SDL_DisplayID dispID = SDL_GetDisplayForWindow(window);
  if (dispID == 0) return std::nullopt;
  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  if (displays == nullptr) return std::nullopt;
  std::optional<int> found;
  for (int i = 0; i < count; ++i) {
    if (displays[i] == dispID) {
      found = i;
      break;
    }
  }
  SDL_free(displays);
  return found;
}

int GetCurrentDisplayIndexState(LuaBindingContext& ctx) {
  if (const auto idx = FindWindowDisplayIndex(TryGetWindow(ctx))) {
    return *idx;
  }
  if (const auto* settings = GetOrCreateUserSettings(ctx); settings && settings->displayIndex >= 0) {
    return settings->displayIndex;
  }
  return 0;
}

std::string GetCurrentDisplayNameState(LuaBindingContext& ctx) {
  if (auto* window = TryGetWindow(ctx)) {
    const SDL_DisplayID dispID = SDL_GetDisplayForWindow(window);
    if (dispID != 0) {
      if (const char* name = SDL_GetDisplayName(dispID)) {
        return name;
      }
    }
  }
  if (const auto* settings = GetOrCreateUserSettings(ctx)) {
    return settings->displayName;
  }
  return "";
}

bool SetWindowDisplayState(LuaBindingContext& ctx, const int displayIndex) {
  if (displayIndex < 0) return false;
  if (auto* settings = GetOrCreateUserSettings(ctx)) {
    settings->displayIndex = displayIndex;
  }
  auto* window = TryGetWindow(ctx);
  if (window == nullptr) return true;
  const bool ok = MoveWindowToDisplay(window, displayIndex);
  if (ok) {
    if (auto* settings = GetOrCreateUserSettings(ctx)) {
      settings->CaptureWindow(window);
    }
  }
  return ok;
}

float GetMasterVolumeState(LuaBindingContext& ctx) {
  if (const auto* config = ctx.GetRegistry()->TryGet<GameConfig>()) {
    return config->GetEngineOptions().masterVolume;
  }
  if (const auto* settings = GetOrCreateUserSettings(ctx)) {
    return settings->masterVolume;
  }
  return 1.0F;
}

void SetMasterVolumeState(LuaBindingContext& ctx, const float volume) {
  const float clamped = std::clamp(volume, 0.0F, 1.0F);
  if (auto* config = ctx.GetRegistry()->TryGet<GameConfig>()) {
    config->GetEngineOptions().masterVolume = clamped;
  }
  if (auto* settings = GetOrCreateUserSettings(ctx)) {
    settings->masterVolume = clamped;
  }
}

bool GetAudioMutedState(LuaBindingContext& ctx) {
  if (const auto* config = ctx.GetRegistry()->TryGet<GameConfig>()) {
    return !config->GetEngineOptions().audioEnabled;
  }
  if (const auto* settings = GetOrCreateUserSettings(ctx)) {
    return settings->audioMuted;
  }
  return false;
}

void SetAudioMutedState(LuaBindingContext& ctx, const bool muted) {
  if (auto* config = ctx.GetRegistry()->TryGet<GameConfig>()) {
    config->GetEngineOptions().audioEnabled = !muted;
  }
  if (auto* settings = GetOrCreateUserSettings(ctx)) {
    settings->audioMuted = muted;
  }
}

bool SaveSettingsToDisk(LuaBindingContext& ctx) {
  auto* settings = GetOrCreateUserSettings(ctx);
  if (settings == nullptr) return false;
  const auto* config = ctx.GetRegistry()->TryGet<GameConfig>();
  const bool isHeadless = config != nullptr && config->GetEngineOptions().headless;
  if (!isHeadless && !ctx.IsEditorMode()) {
    if (auto* window = TryGetWindow(ctx)) {
      settings->CaptureWindow(window);
    }
    if (config != nullptr) {
      settings->masterVolume = config->GetEngineOptions().masterVolume;
      settings->audioMuted = !config->GetEngineOptions().audioEnabled;
    }
  }
  const std::string savePath = ctx.GetSaveDataPath();
  return settings->Save(savePath);
}

sol::object SettingValueToLuaObject(const sol::state_view& lua, const octarine::UserSettings::SettingValue& val) {
  if (std::holds_alternative<bool>(val)) {
    return sol::make_object(lua, std::get<bool>(val));
  }
  if (std::holds_alternative<int64_t>(val)) {
    return sol::make_object(lua, std::get<int64_t>(val));
  }
  if (std::holds_alternative<double>(val)) {
    return sol::make_object(lua, std::get<double>(val));
  }
  return sol::make_object(lua, std::get<std::string>(val));
}

std::optional<octarine::UserSettings::SettingValue> LuaObjectToSettingValue(const sol::object& obj) {
  if (obj.get_type() == sol::type::boolean) {
    return obj.as<bool>();
  }
  if (obj.get_type() == sol::type::number) {
    if (obj.is<int64_t>()) {
      const double d = obj.as<double>();
      const auto i = obj.as<int64_t>();
      if (static_cast<double>(i) == d) {
        return i;
      }
    }
    return obj.as<double>();
  }
  if (obj.get_type() == sol::type::string) {
    return obj.as<std::string>();
  }
  return std::nullopt;
}

std::string NormalizeBuiltInKey(std::string_view key) {
  if (key == "window.fullscreen") return "fullscreen";
  if (key == "window.maximized") return "maximized";
  if (key == "window.width") return "window_width";
  if (key == "window.height") return "window_height";
  if (key == "window.x") return "window_x";
  if (key == "window.y") return "window_y";
  if (key == "window.display_index") return "display_index";
  if (key == "window.display_name") return "display_name";
  if (key == "audio.master_volume") return "master_volume";
  if (key == "audio.muted" || key == "audio.audio_muted" || key == "muted") return "audio_muted";
  if (key == "engine.fps_target") return "fps_target";
  if (key == "engine.perf_overlay") return "perf_overlay";
  return std::string(key);
}

bool TryGetBuiltInWindowSetting(LuaBindingContext& ctx, const std::string& key, const sol::state_view& lua,
                                sol::object& out) {
  if (key == "fullscreen") {
    out = sol::make_object(lua, GetFullscreenState(ctx));
    return true;
  }
  if (key == "maximized") {
    out = sol::make_object(lua, GetMaximizedState(ctx));
    return true;
  }
  if (key == "window_width") {
    out = sol::make_object(lua, std::get<0>(GetWindowSizeState(ctx)));
    return true;
  }
  if (key == "window_height") {
    out = sol::make_object(lua, std::get<1>(GetWindowSizeState(ctx)));
    return true;
  }
  if (key == "window_x") {
    out = sol::make_object(lua, std::get<0>(GetWindowPositionState(ctx)));
    return true;
  }
  if (key == "window_y") {
    out = sol::make_object(lua, std::get<1>(GetWindowPositionState(ctx)));
    return true;
  }
  if (key == "display_index") {
    out = sol::make_object(lua, GetCurrentDisplayIndexState(ctx));
    return true;
  }
  if (key == "display_name") {
    out = sol::make_object(lua, GetCurrentDisplayNameState(ctx));
    return true;
  }
  return false;
}

bool TryGetBuiltInSetting(LuaBindingContext& ctx, std::string_view rawKey, const sol::state_view& lua,
                          sol::object& out) {
  const std::string key = NormalizeBuiltInKey(rawKey);
  if (TryGetBuiltInWindowSetting(ctx, key, lua, out)) {
    return true;
  }
  if (key == "master_volume") {
    out = sol::make_object(lua, static_cast<double>(GetMasterVolumeState(ctx)));
    return true;
  }
  if (key == "audio_muted") {
    out = sol::make_object(lua, GetAudioMutedState(ctx));
    return true;
  }
  if (key == "fps_target") {
    if (const auto* config = ctx.GetRegistry()->TryGet<GameConfig>()) {
      out = sol::make_object(lua, config->GetEngineOptions().fpsTarget);
      return true;
    }
    const auto* s = GetOrCreateUserSettings(ctx);
    out = sol::make_object(lua, (s && s->fpsTarget.has_value()) ? *s->fpsTarget : 0);
    return true;
  }
  if (key == "perf_overlay") {
    if (const auto* config = ctx.GetRegistry()->TryGet<GameConfig>()) {
      out = sol::make_object(lua, config->GetEngineOptions().showPerfOverlay);
      return true;
    }
    const auto* s = GetOrCreateUserSettings(ctx);
    out = sol::make_object(lua, (s && s->showPerfOverlay.has_value()) ? *s->showPerfOverlay : false);
    return true;
  }
  return false;
}

bool TrySetBuiltInBool(LuaBindingContext& ctx, const std::string& key, const sol::object& val, bool& handled) {
  if (key != "fullscreen" && key != "maximized" && key != "audio_muted" && key != "perf_overlay") {
    handled = false;
    return false;
  }
  handled = true;
  if (val.get_type() != sol::type::boolean) return false;
  const bool b = val.as<bool>();
  if (key == "fullscreen") SetFullscreenState(ctx, b);
  if (key == "maximized") SetMaximizedState(ctx, b);
  if (key == "audio_muted") SetAudioMutedState(ctx, b);
  if (key == "perf_overlay") SetPerfOverlayValue(ctx, b);
  return true;
}

void ApplyWindowDimensionOrPosSetting(LuaBindingContext& ctx, const std::string& key, const int intVal) {
  if (key == "window_width" || key == "window_height") {
    auto [curW, curH] = GetWindowSizeState(ctx);
    if (curW <= 0) curW = 1280;
    if (curH <= 0) curH = 720;
    SetWindowSizeState(ctx, key == "window_width" ? intVal : curW, key == "window_height" ? intVal : curH);
    return;
  }
  auto [curX, curY] = GetWindowPositionState(ctx);
  SetWindowPositionState(ctx, key == "window_x" ? intVal : curX, key == "window_y" ? intVal : curY);
}

bool TrySetBuiltInNumber(LuaBindingContext& ctx, const std::string& key, const sol::object& val, bool& handled) {
  if (key != "master_volume" && key != "fps_target" && key != "display_index" && key != "window_width" &&
      key != "window_height" && key != "window_x" && key != "window_y") {
    handled = false;
    return false;
  }
  handled = true;
  if (val.get_type() != sol::type::number) return false;
  if (key == "master_volume") {
    SetMasterVolumeState(ctx, static_cast<float>(val.as<double>()));
    return true;
  }
  const int intVal = static_cast<int>(val.as<int64_t>());
  if (key == "fps_target") {
    SetFpsTargetValue(ctx, intVal);
    return true;
  }
  if (key == "display_index") {
    return SetWindowDisplayState(ctx, intVal);
  }
  ApplyWindowDimensionOrPosSetting(ctx, key, intVal);
  return true;
}

bool IsBuiltInSettingKey(std::string_view rawKey) {
  const std::string key = NormalizeBuiltInKey(rawKey);
  constexpr std::array<std::string_view, 12> kBuiltIns = {
      "fullscreen",    "maximized",    "window_width",  "window_height", "window_x",   "window_y",
      "display_index", "display_name", "master_volume", "audio_muted",   "fps_target", "perf_overlay"};
  return std::find(kBuiltIns.begin(), kBuiltIns.end(), key) != kBuiltIns.end();
}

// NOLINTNEXTLINE(misc-no-recursion)
bool DefineSettingsFromTable(octarine::UserSettings& settings, const sol::table& tbl, const std::string& prefix,
                             size_t depth = 0) {
  if (depth > kDefaultMaxDepth) return false;
  bool allOk = true;
  for (const auto& [k, v] : tbl) {
    if (k.get_type() != sol::type::string) continue;
    const std::string keyName = prefix.empty() ? k.as<std::string>() : (prefix + "." + k.as<std::string>());
    if (v.get_type() == sol::type::table) {
      allOk &= DefineSettingsFromTable(settings, v.as<sol::table>(), keyName, depth + 1);
      continue;
    }
    if (const auto parsed = LuaObjectToSettingValue(v)) {
      settings.DefineSetting(keyName, *parsed);
    } else {
      Logger::Warn("settings.define: unsupported default value type for key '" + keyName + "'");
      allOk = false;
    }
  }
  return allOk;
}

bool HandleSettingsDefine(LuaBindingContext& ctx, const sol::object& keyOrTable, const sol::object& defaultValue) {
  auto* settings = GetOrCreateUserSettings(ctx);
  if (settings == nullptr) return false;

  if (keyOrTable.get_type() == sol::type::table) {
    return DefineSettingsFromTable(*settings, keyOrTable.as<sol::table>(), "");
  }
  if (keyOrTable.get_type() == sol::type::string) {
    const std::string key = keyOrTable.as<std::string>();
    const auto parsed = LuaObjectToSettingValue(defaultValue);
    if (!parsed.has_value()) {
      Logger::Warn("settings.define: unsupported default value type for key '" + key + "'");
      return false;
    }
    settings->DefineSetting(key, *parsed);
    return true;
  }
  Logger::Warn("settings.define: expected (key, default_value) or (defaults_table)");
  return false;
}

sol::object HandleSettingsGet(LuaBindingContext& ctx, const std::string& key, const sol::object& fallback,
                              sol::this_state s) {
  sol::state_view lua(s);
  sol::object builtIn = sol::lua_nil;
  if (TryGetBuiltInSetting(ctx, key, lua, builtIn)) {
    return builtIn;
  }
  if (const auto* settings = GetOrCreateUserSettings(ctx)) {
    if (const auto val = settings->GetSetting(key)) {
      return SettingValueToLuaObject(lua, *val);
    }
  }
  return fallback.valid() ? fallback : sol::make_object(lua, sol::lua_nil);
}

bool HandleSettingsSet(LuaBindingContext& ctx, const std::string& key, const sol::object& value) {
  const std::string normKey = NormalizeBuiltInKey(key);
  bool handled = false;
  if (TrySetBuiltInBool(ctx, normKey, value, handled)) return true;
  if (handled) {
    Logger::Warn("settings.set: expected boolean for built-in key '" + key + "'");
    return false;
  }
  if (TrySetBuiltInNumber(ctx, normKey, value, handled)) return true;
  if (handled) {
    Logger::Warn("settings.set: expected number for built-in key '" + key + "'");
    return false;
  }

  auto* settings = GetOrCreateUserSettings(ctx);
  if (settings == nullptr) return false;
  const auto parsed = LuaObjectToSettingValue(value);
  if (!parsed.has_value()) {
    Logger::Warn("settings.set: unsupported value type for key '" + key + "'");
    return false;
  }
  return settings->SetSetting(key, *parsed);
}

sol::table HandleSettingsGetAll(LuaBindingContext& ctx, sol::this_state s) {
  sol::state_view lua(s);
  sol::table out = lua.create_table();

  constexpr std::array<std::string_view, 12> kBuiltIns = {
      "fullscreen",    "maximized",    "window_width",  "window_height", "window_x",   "window_y",
      "display_index", "display_name", "master_volume", "audio_muted",   "fps_target", "perf_overlay"};
  for (const auto k : kBuiltIns) {
    sol::object val = sol::lua_nil;
    if (TryGetBuiltInSetting(ctx, k, lua, val)) {
      out[std::string(k)] = val;
    }
  }

  if (const auto* settings = GetOrCreateUserSettings(ctx)) {
    for (const auto& [k, v] : settings->defaultSettings) {
      out[k] = SettingValueToLuaObject(lua, v);
    }
    for (const auto& [k, v] : settings->customSettings) {
      out[k] = SettingValueToLuaObject(lua, v);
    }
  }
  return out;
}

void BindUserSettings(sol::state& lua, LuaBindingContext& ctx) {
  sol::table settingsTbl = lua.create_table();

  settingsTbl.set_function("define", [&ctx](const sol::object& keyOrTable, const sol::object& defaultVal) -> bool {
    return HandleSettingsDefine(ctx, keyOrTable, defaultVal);
  });

  settingsTbl.set_function(
      "get", [&ctx](const std::string& key, const sol::object& fallback, sol::this_state s) -> sol::object {
        return HandleSettingsGet(ctx, key, fallback, s);
      });

  settingsTbl.set_function("set", [&ctx](const std::string& key, const sol::object& value) -> bool {
    return HandleSettingsSet(ctx, key, value);
  });

  settingsTbl.set_function("has", [&ctx](const std::string& key) -> bool {
    if (IsBuiltInSettingKey(key)) return true;
    const auto* settings = GetOrCreateUserSettings(ctx);
    return settings != nullptr && settings->HasSetting(key);
  });

  settingsTbl.set_function("get_default", [&ctx](const std::string& key, sol::this_state s) -> sol::object {
    sol::state_view luaView(s);
    if (const auto* settings = GetOrCreateUserSettings(ctx)) {
      if (const auto defVal = settings->GetDefaultSetting(key)) {
        return SettingValueToLuaObject(luaView, *defVal);
      }
    }
    return sol::make_object(luaView, sol::lua_nil);
  });

  settingsTbl.set_function("reset", [&ctx](const std::string& key, sol::this_state s) -> sol::object {
    if (auto* settings = GetOrCreateUserSettings(ctx)) {
      static_cast<void>(settings->ResetSetting(key));
    }
    return HandleSettingsGet(ctx, key, sol::lua_nil, s);
  });

  settingsTbl.set_function("reset_all", [&ctx]() {
    if (auto* settings = GetOrCreateUserSettings(ctx)) {
      settings->ResetAllSettings();
    }
  });

  settingsTbl.set_function("get_all", [&ctx](sol::this_state s) -> sol::table { return HandleSettingsGetAll(ctx, s); });

  settingsTbl.set_function("save", [&ctx]() -> bool { return SaveSettingsToDisk(ctx); });

  lua["settings"] = settingsTbl;

  lua.set_function("get_user_setting",
                   [&ctx](const std::string& key, const sol::optional<std::string>& defaultValue) -> std::string {
                     const auto* settings = GetOrCreateUserSettings(ctx);
                     if (settings == nullptr) return defaultValue.value_or("");
                     return settings->GetCustomSetting(key, defaultValue.value_or(""));
                   });

  lua.set_function("set_user_setting", [&ctx](const std::string& key, const sol::object& value) -> bool {
    return HandleSettingsSet(ctx, key, value);
  });

  lua.set_function("save_user_settings", [&ctx]() -> bool { return SaveSettingsToDisk(ctx); });
}

void BindWindowAndDisplay(sol::state& lua, LuaBindingContext& ctx) {
  lua.set_function("is_fullscreen", [&ctx]() -> bool { return GetFullscreenState(ctx); });
  lua.set_function("set_fullscreen", [&ctx](const bool enabled) { SetFullscreenState(ctx, enabled); });
  lua.set_function("is_maximized", [&ctx]() -> bool { return GetMaximizedState(ctx); });
  lua.set_function("set_maximized", [&ctx](const bool enabled) { SetMaximizedState(ctx, enabled); });
  lua.set_function("get_window_size", [&ctx]() -> std::tuple<int, int> { return GetWindowSizeState(ctx); });
  lua.set_function("set_window_size", [&ctx](const int w, const int h) { SetWindowSizeState(ctx, w, h); });
  lua.set_function("get_window_position", [&ctx]() -> std::tuple<int, int> { return GetWindowPositionState(ctx); });
  lua.set_function("set_window_position", [&ctx](const int x, const int y) { SetWindowPositionState(ctx, x, y); });

  lua.set_function("get_display_count", []() -> int {
    int count = 0;
    if (SDL_DisplayID* displays = SDL_GetDisplays(&count)) {
      SDL_free(displays);
      return count;
    }
    return 0;
  });

  lua.set_function("get_current_display_index", [&ctx]() -> int { return GetCurrentDisplayIndexState(ctx); });
  lua.set_function("get_current_display_name", [&ctx]() -> std::string { return GetCurrentDisplayNameState(ctx); });
  lua.set_function("set_window_display",
                   [&ctx](const int displayIndex) -> bool { return SetWindowDisplayState(ctx, displayIndex); });
}

void BindAudioSettings(sol::state& lua, LuaBindingContext& ctx) {
  lua.set_function("get_master_volume", [&ctx]() -> float { return GetMasterVolumeState(ctx); });
  lua.set_function("set_master_volume", [&ctx](const float volume) { SetMasterVolumeState(ctx, volume); });
  lua.set_function("is_audio_muted", [&ctx]() -> bool { return GetAudioMutedState(ctx); });
  lua.set_function("set_audio_muted", [&ctx](const bool muted) { SetAudioMutedState(ctx, muted); });
}
}  // namespace

void LuaModuleBinding<GameModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  BindDiagnostics(lua, ctx);
  BindSubsteppingAndDt(lua, ctx);
  BindExecutionEnvironment(lua, ctx);
  BindStateInspection(lua, ctx);
  BindWorldAndCamera(lua, ctx);
  BindUserSettings(lua, ctx);
  BindWindowAndDisplay(lua, ctx);
  BindAudioSettings(lua, ctx);
}
