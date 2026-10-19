#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <sol/sol.hpp>
#include <string>

#include "Components/BoxColliderComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "ECS/Query.h"
#include "EventBus/EventBus.h"
#include "Systems/PerfOverlaySystem.h"

class Game;
class Registry;
class Renderer;
class EngineRuntime;
struct EngineOptions;
class InputSystem;
// Defined in Events/KeyInputEvent.h. Forward-declared (not included) because that header is not
// self-contained — it derives from EventBus's Event without including it, so pulling it here
// would force a fragile include order on every consumer of FrameLoop.h.
struct KeyInputEvent;

// Owns the per-frame loop body Game::Run drives: poll input, wait out the frame budget, update
// the ECS, and render. Game::Run keeps the while(running) loop and Setup/teardown; FrameLoop is
// the four phases plus the key-event handler that toggles the debug GUI / requests quit.
//
// Holds non-owning pointers to the engine pieces it touches (registry, event bus, renderer, the
// SDL runtime) plus the Game it renders the editor overlay for. Constructed by Game once those
// exist; the debug-collider query is built here so it isn't reallocated per frame.
class FrameLoop {
 public:
  FrameLoop(Game* game, Registry* registry, EventBus* eventBus, Renderer* renderer, EngineRuntime* runtime,
            sol::state& lua);

  FrameLoop(const FrameLoop&) = delete;
  FrameLoop& operator=(const FrameLoop&) = delete;
  FrameLoop(FrameLoop&&) = delete;
  FrameLoop& operator=(FrameLoop&&) = delete;

  ~FrameLoop() = default;

  // Seed the previous-frame tick so the first WaitTime() yields a sane delta. Call once right
  // before entering the loop.
  void Begin();

  void ProcessInput();
  // Runs one rendered frame worth of simulation (simSubSteps fixed steps when fast-forwarding).
  // Returns the number of simulation steps actually executed (0 when paused).
  int Update(float deltaTime);
  void Render(float deltaTime);
  [[nodiscard]] float WaitTime();

 private:
  static KeyInputEvent GetKeyInputEvent(SDL_KeyboardEvent* event);
  void UpdateViewportInfo(bool editorSession, bool showDebugGUI);
  void CheckHeadlessCapture();
  void SyncAudioGain(const EngineOptions& options);
  void PollHotReload(const EngineOptions& options, float deltaTime);
  int ExecuteSimulationSteps(float deltaTime, EngineOptions& options, InputSystem* inputSystem);

  Game* game_;
  Registry* registry_;
  EventBus* event_bus_;
  Renderer* renderer_;
  EngineRuntime* runtime_;
  [[maybe_unused]] sol::state& lua_;
  // Nanosecond ticks (SDL_GetTicksNS) — ms granularity rounds sub-millisecond frames to a zero
  // deltaTime when FpsTarget=0 lets the loop run uncapped.
  Uint64 nanoseconds_previous_frame_ = 0;
  std::unique_ptr<ComponentQuery<GlobalTransformComponent, BoxColliderComponent>> collider_query_;
  // Built-in (no-ImGui) FPS + frame-time overlay. Holds a small per-line text-texture cache, so it
  // is a long-lived member rather than reconstructed each frame.
  PerfOverlaySystem perf_overlay_;

  // Headless frame-capture (env-driven). When OCTARINE_CAPTURE_PATH is set, the
  // loop renders up to OCTARINE_CAPTURE_FRAME (default 180 ≈ 3s @60fps, past the stress warmup),
  // writes that frame's scene texture to the path as BMP, then quits. Lets a headless run produce
  // a viewable rendered frame.
  static constexpr long kDefaultCaptureFrame = 180;  // ≈3s @60fps, past the stress warmup
  std::string capture_path_;
  long capture_frame_ = kDefaultCaptureFrame;
  long frame_index_ = 0;
  bool capture_done_ = false;
};
