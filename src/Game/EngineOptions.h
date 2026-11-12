#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "General/AngleUnit.h"
#include "General/Color.h"
#include "General/Constants.h"

// Texture scale filtering. Mirrors SDL_ScaleMode without pulling in SDL headers.
enum class ScaleMode : std::uint8_t { Nearest, Linear, PixelArt };

[[nodiscard]] inline std::optional<ScaleMode> ParseScaleModeString(std::string_view str) {
  if (str == "nearest" || str == "point") return ScaleMode::Nearest;
  if (str == "linear" || str == "bilinear") return ScaleMode::Linear;
  if (str == "pixelart" || str == "pixel_art" || str == "pixel-art") return ScaleMode::PixelArt;
  return std::nullopt;
}

inline const char* toScaleModeString(ScaleMode mode) {
  switch (mode) {
    case ScaleMode::Linear:
      return "linear";
    case ScaleMode::PixelArt:
      return "pixelart";
    case ScaleMode::Nearest:
    default:
      return "nearest";
  }
}

// Window display mode. config.ini: DefaultWindowMode=windowed|borderless|exclusive
enum class WindowMode : std::uint8_t {
  Windowed = 0,
  BorderlessFullscreen = 1,
  ExclusiveFullscreen = 2,
};

// How the logical scene texture is scaled or expanded into the window backbuffer.
// config.ini: DefaultPresentationMode=letterbox|integer_scale|stretch|expand
enum class PresentationMode : std::uint8_t {
  Letterbox = 0,
  IntegerScale = 1,
  Stretch = 2,
  Expand = 3,
};

[[nodiscard]] inline std::optional<WindowMode> ParseWindowMode(std::string_view str) {
  if (str == "windowed" || str == "window") return WindowMode::Windowed;
  if (str == "borderless" || str == "borderless_fullscreen" || str == "desktop_fullscreen" ||
      str == "borderless_window") {
    return WindowMode::BorderlessFullscreen;
  }
  if (str == "exclusive" || str == "exclusive_fullscreen" || str == "fullscreen") {
    return WindowMode::ExclusiveFullscreen;
  }
  return std::nullopt;
}

[[nodiscard]] constexpr std::string_view WindowModeToString(WindowMode mode) {
  switch (mode) {
    case WindowMode::Windowed:
      return "windowed";
    case WindowMode::BorderlessFullscreen:
      return "borderless";
    case WindowMode::ExclusiveFullscreen:
      return "exclusive";
  }
  return "windowed";
}

[[nodiscard]] inline std::optional<PresentationMode> ParsePresentationMode(std::string_view str) {
  if (str == "letterbox" || str == "aspect" || str == "fit") return PresentationMode::Letterbox;
  if (str == "integer_scale" || str == "integer" || str == "pixel_perfect") return PresentationMode::IntegerScale;
  if (str == "stretch" || str == "fill") return PresentationMode::Stretch;
  if (str == "expand" || str == "adaptive") return PresentationMode::Expand;
  return std::nullopt;
}

[[nodiscard]] constexpr std::string_view PresentationModeToString(PresentationMode mode) {
  switch (mode) {
    case PresentationMode::Letterbox:
      return "letterbox";
    case PresentationMode::IntegerScale:
      return "integer_scale";
    case PresentationMode::Stretch:
      return "stretch";
    case PresentationMode::Expand:
      return "expand";
  }
  return "letterbox";
}

struct EngineOptions {
  // Frame-rate cap enforced by FrameLoop::WaitTime. config.ini: FpsTarget= (0 = uncapped — the
  // loop runs as fast as it can and deltaTime carries the real elapsed time).
  int fpsTarget = Constants::kFps;
  WindowMode windowMode = WindowMode::Windowed;
  PresentationMode presentationMode = PresentationMode::Letterbox;
  bool vsync = false;
  bool windowResizable = true;
  bool showDebugGUI = false;
  bool drawColliders = false;
  bool showEntityInfo = false;
  octarine::Color debugOverlayBackgroundColor = {0, 0, 0, 0};
  bool showImGuiDemoWindow = false;
  bool logInputEvents = false;
  bool audioEnabled = true;
  float masterVolume = 1.0F;
  int audioTrackPoolInitial = 32;
  int audioTrackPoolMax = 128;
  // Spatial sources beyond this radius from the listener are halted by AudioCullingSystem
  // (track returned to pool, playback offset stashed on the source for clean resume on
  // re-entry). Non-spatial sources ignore this cap.
  float audioListenerRadius = 1500.0F;
  bool isPaused = false;
  float timeScale = 1.0F;
  bool stepFrame = false;
  // Fast-forward: number of fixed simulation steps executed per rendered frame (1 = real time).
  int simSubSteps = 1;
  // Explicit fixed step in seconds (0 = variable dt * timeScale, or 1/60 when simSubSteps > 1).
  // When a fixed step is in effect, timeScale is ignored.
  float fixedStepDt = 0.0F;
  // Mirrors ThreadPool::IsForceSerial(): serial ECS iteration + synchronous collision for
  // bit-reproducible seeded runs (see set_deterministic in GameModuleLuaBinding).
  bool deterministic = false;
  // Headless simulation mode (--headless or OCTARINE_HEADLESS=1): uses SDL dummy video/audio
  // drivers, hides the window, uncaps FPS (fpsTarget = 0), and skips software rasterization
  // in FrameLoop::Render unless OCTARINE_CAPTURE_PATH is set.
  bool headless = false;
  // When true, a scene whose asset references fail validation aborts the load (dev gate). Off by
  // default so players never hard-fail; the bake step is the CI enforcement point.
  bool assetValidationFatal = false;
  // Lua script hot reload. Compiled out under OCTARINE_SHIPPED; in dev player builds this is the
  // runtime toggle. Poll cadence is mtime-based, single-threaded, on the main loop.
  bool hotReloadEnabled = true;
  float hotReloadPollSeconds = 0.25F;
  // Unit that angles are authored and displayed in (Lua tables, set_rotation).
  // Storage stays radians regardless. config.ini: AngleUnit=radians|degrees.
  octarine::AngleUnit angleUnit = octarine::AngleUnit::Degrees;
};
