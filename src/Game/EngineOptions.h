#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "General/AngleUnit.h"
#include "General/Color.h"
#include "General/Constants.h"

// Window display mode. config.ini: DefaultWindowMode=windowed|borderless|exclusive
enum class WindowMode : std::uint8_t {
  Windowed = 0,
  BorderlessFullscreen = 1,
  ExclusiveFullscreen = 2,
};

// How the fixed logical scene texture is scaled into the window backbuffer.
// config.ini: DefaultPresentationMode=letterbox|integer_scale|stretch
enum class PresentationMode : std::uint8_t {
  Letterbox = 0,
  IntegerScale = 1,
  Stretch = 2,
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
  }
  return "letterbox";
}

// Screen anchor for the built-in perf overlay. config.ini: PerfOverlayCorner=top-left|top-right|
// bottom-left|bottom-right.
enum class PerfOverlayCorner : std::uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };

// Which metrics the perf overlay draws. config.ini: PerfOverlayMetrics takes a comma-separated
// list of fps|frametime|entities|memory, or all ("both" accepted as a legacy alias).
enum class PerfOverlayMetrics : std::uint8_t {
  Fps = 1 << 0,
  FrameTime = 1 << 1,
  Entities = 1 << 2,
  Memory = 1 << 3,
  All = Fps | FrameTime | Entities | Memory
};

inline PerfOverlayMetrics operator|(PerfOverlayMetrics lhs, PerfOverlayMetrics rhs) {
  return static_cast<PerfOverlayMetrics>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
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
  bool showFpsCounter = true;
  bool showEntityInfo = false;
  // Built-in renderer FPS + frame-time overlay (no ImGui). Unlike the ImGui FPS window this ships in
  // packaged builds, so it's the player-facing perf readout. Off by default; opt in via the
  // `PerfOverlay` config.ini key, the editor settings toggle, or set_perf_overlay() from Lua.
  bool showPerfOverlay = false;
  // Placement + content of the perf overlay (config.ini only: PerfOverlayCorner=, PerfOverlayMetrics=).
  PerfOverlayCorner perfOverlayCorner = PerfOverlayCorner::TopLeft;
  PerfOverlayMetrics perfOverlayMetrics = PerfOverlayMetrics::All;
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
  // Lua script hot reload. Compiled out under OCTARINE_SHIPPED; in dev/editor builds this is the
  // runtime toggle. Poll cadence is mtime-based, single-threaded, on the main loop.
  bool hotReloadEnabled = true;
  float hotReloadPollSeconds = 0.25F;
  // Unit that angles are authored and displayed in (Lua tables, set_rotation, the editor
  // inspectors). Storage stays radians regardless. config.ini: AngleUnit=radians|degrees.
  octarine::AngleUnit angleUnit = octarine::AngleUnit::Degrees;
};
