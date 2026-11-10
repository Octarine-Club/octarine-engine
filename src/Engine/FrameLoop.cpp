#include "Engine/FrameLoop.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_atomic.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

#include "AssetManager/AssetHotReload.h"
#include "AssetManager/AssetManager.h"
#include "Components/ViewportInfo.h"
#include "ECS/Registry.h"
#include "Engine/EngineRuntime.h"
#include "EventBus/EventBus.h"
#include "Events/GamepadButtonEvent.h"
#include "Events/KeyInputEvent.h"
#include "Events/MouseInputEvent.h"
#include "Events/MouseWheelEvent.h"
#include "Game/EngineContext.h"
#include "Game/Game.h"
#include "Game/GameConfig.h"
#include "Game/UserSettings.h"
#include "General/Logger.h"
#include "General/PerfUtils.h"
#include "Lua/HotReload/ScriptHotReload.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/RenderQueueStats.h"
#include "Renderer/Renderer.h"
#include "Systems/DrawColliderSystem.h"
#include "Systems/InputSystem.h"
#include "Systems/TilemapSystem.h"
#include "UI/UIRuntime.h"

#ifndef OCTARINE_SHIPPED
#include "Dev/DevListenServer.h"
#endif

#ifdef OCTARINE_WITH_IMGUI
#include "Systems/RenderDebugGUISystem.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#endif

namespace {
// Portable env read. MSVC deprecates std::getenv (C4996) and the build treats warnings as errors,
// so use _dupenv_s there — mirrors the split in Lua/LuaApiManifest.cpp. Returns "" when unset.
std::string GetEnvVar(const char* name) {
#ifdef _MSC_VER
  char* value = nullptr;
  size_t len = 0;
  const bool ok = _dupenv_s(&value, &len, name) == 0 && value != nullptr;
  const std::string result = ok ? std::string(value) : std::string();
  free(value);
  return result;
#else
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
#endif
}

std::optional<SDL_FRect> PrepareSceneTargetForWindow(Renderer& renderer, SDL_Renderer* sdlRenderer,
                                                     const GameConfig& gameConfig, const bool isHeadlessOrCapture) {
  if (sdlRenderer == nullptr) return std::nullopt;
  if (isHeadlessOrCapture) {
    renderer.EnsureSceneTargetSize(sdlRenderer, gameConfig.windowWidth, gameConfig.windowHeight);
    return std::nullopt;
  }
  int outPixelW = 0;
  int outPixelH = 0;
  if (!SDL_GetRenderOutputSize(sdlRenderer, &outPixelW, &outPixelH) || outPixelW <= 0 || outPixelH <= 0) {
    return std::nullopt;
  }
  const SDL_FRect compositeRect =
      Renderer::ComputeCompositeRect(gameConfig.windowWidth, gameConfig.windowHeight, outPixelW, outPixelH,
                                     gameConfig.GetEngineOptions().presentationMode);
  const int targetW = std::max(1, static_cast<int>(std::lround(compositeRect.w)));
  const int targetH = std::max(1, static_cast<int>(std::lround(compositeRect.h)));
  renderer.EnsureSceneTargetSize(sdlRenderer, targetW, targetH);
  return compositeRect;
}

void SyncViewportRectFromWindow(SDL_Window* window, SDL_Renderer* sdlRenderer, const GameConfig& config,
                                ViewportInfo& viewportInfo) {
  if (window == nullptr) return;
  int windowW = 0;
  int windowH = 0;
  SDL_GetWindowSize(window, &windowW, &windowH);
  int outPixelW = 0;
  int outPixelH = 0;
  if (sdlRenderer != nullptr && SDL_GetRenderOutputSize(sdlRenderer, &outPixelW, &outPixelH) && outPixelW > 0 &&
      outPixelH > 0 && windowW > 0 && windowH > 0) {
    const SDL_FRect physRect = Renderer::ComputeCompositeRect(config.windowWidth, config.windowHeight, outPixelW,
                                                              outPixelH, config.GetEngineOptions().presentationMode);
    const float sx = static_cast<float>(windowW) / static_cast<float>(outPixelW);
    const float sy = static_cast<float>(windowH) / static_cast<float>(outPixelH);
    viewportInfo.x = physRect.x * sx;
    viewportInfo.y = physRect.y * sy;
    viewportInfo.width = physRect.w * sx;
    viewportInfo.height = physRect.h * sy;
    return;
  }
  const SDL_FRect rect = Renderer::ComputeCompositeRect(config.windowWidth, config.windowHeight, windowW, windowH,
                                                        config.GetEngineOptions().presentationMode);
  viewportInfo.x = rect.x;
  viewportInfo.y = rect.y;
  viewportInfo.width = rect.w;
  viewportInfo.height = rect.h;
}

void CaptureWindowStateOnEvent(octarine::UserSettings& userSettings, SDL_Window* window, const Uint32 eventType) {
  if (eventType == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || eventType == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
    userSettings.CaptureWindow(window);
    return;
  }
  const auto savedMode = userSettings.windowMode;
  const bool savedFullscreen = userSettings.fullscreen;
  const int savedW = userSettings.windowWidth;
  const int savedH = userSettings.windowHeight;
  const int savedX = userSettings.windowX;
  const int savedY = userSettings.windowY;
  const bool savedHasPlacement = userSettings.hasWindowPlacement;
  userSettings.CaptureWindow(window);
  const bool wasNonWindowed = savedFullscreen || (savedMode.has_value() && *savedMode != WindowMode::Windowed);
  if (wasNonWindowed) {
    userSettings.windowWidth = savedW;
    userSettings.windowHeight = savedH;
    userSettings.windowX = savedX;
    userSettings.windowY = savedY;
    userSettings.hasWindowPlacement = savedHasPlacement;
  }
  if (savedMode.has_value()) {
    userSettings.SetWindowMode(*savedMode);
  } else {
    userSettings.fullscreen = savedFullscreen;
  }
}
}  // namespace

FrameLoop::FrameLoop(Game* game, Registry* registry, EventBus* eventBus, Renderer* renderer, EngineRuntime* runtime,
                     sol::state& lua)
    : game_(game), registry_(registry), event_bus_(eventBus), renderer_(renderer), runtime_(runtime), lua_(lua) {
  if (const std::string path = GetEnvVar("OCTARINE_CAPTURE_PATH"); !path.empty()) {
    capture_path_ = path;
    if (const std::string frame = GetEnvVar("OCTARINE_CAPTURE_FRAME"); !frame.empty()) {
      capture_frame_ = std::stol(frame);
    }
  }
}

void FrameLoop::Begin() {
  nanoseconds_previous_frame_ = SDL_GetTicksNS();
  int count = 0;
  if (SDL_JoystickID* gamepads = SDL_GetGamepads(&count)) {
    for (int i = 0; i < count; ++i) {
      SDL_OpenGamepad(gamepads[i]);
    }
    SDL_free(gamepads);
  }
}

void FrameLoop::ProcessInput() {
  PROFILE_NAMED_SCOPE("Game::ProcessInput");
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
#ifdef OCTARINE_WITH_IMGUI
    ImGui_ImplSDL3_ProcessEvent(&event);
    auto& io = ImGui::GetIO();
    float mouseX, mouseY;
    const unsigned int buttons = SDL_GetMouseState(&mouseX, &mouseY);
    io.MousePos = ImVec2(mouseX, mouseY);
    io.MouseDown[0] = buttons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT);
    io.MouseDown[1] = buttons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT);
#endif

    switch (event.type) {
      case SDL_EVENT_QUIT:
        Game::Quit();
        break;

      case SDL_EVENT_WILL_ENTER_BACKGROUND:
      case SDL_EVENT_TERMINATING: {
        // Mobile OSes (Android/iOS) frequently kill apps while backgrounded without firing
        // SDL_EVENT_QUIT, so flush user settings as soon as the app transitions to background.
        static_cast<void>(game_->SaveUserSettings());
        break;
      }

      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP: {
        KeyInputEvent keyInputEvent = GetKeyInputEvent(&event.key);
        event_bus_->EmitEvent<KeyInputEvent>(keyInputEvent);
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        SDL_MouseButtonEvent mouseButtonEvent = event.button;
        event_bus_->EmitEvent<MouseInputEvent>(mouseButtonEvent);
        break;
      }
      case SDL_EVENT_MOUSE_MOTION: {
        SDL_MouseButtonEvent motionAsMouse{};
        motionAsMouse.type = SDL_EVENT_MOUSE_MOTION;
        motionAsMouse.x = event.motion.x;
        motionAsMouse.y = event.motion.y;
        event_bus_->EmitEvent<MouseInputEvent>(motionAsMouse);
        break;
      }
      case SDL_EVENT_MOUSE_WHEEL: {
        event_bus_->EmitEvent<MouseWheelEvent>(event.wheel.x, event.wheel.y);
        break;
      }
      case SDL_EVENT_GAMEPAD_ADDED: {
        SDL_OpenGamepad(event.gdevice.which);
        break;
      }
      case SDL_EVENT_GAMEPAD_REMOVED: {
        if (SDL_Gamepad* gamepad = SDL_GetGamepadFromID(event.gdevice.which)) {
          SDL_CloseGamepad(gamepad);
        }
        break;
      }
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const auto btn = static_cast<SDL_GamepadButton>(event.gbutton.button);
        const char* rawName = SDL_GetGamepadStringForButton(btn);
        std::string name = rawName ? rawName : "";
        GamepadButtonEvent gamepadEvent(event.gbutton.which, btn, std::move(name), event.gbutton.down);
        event_bus_->EmitEvent<GamepadButtonEvent>(gamepadEvent);
        break;
      }
      case SDL_EVENT_WINDOW_MOVED:
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
      case SDL_EVENT_WINDOW_MAXIMIZED:
      case SDL_EVENT_WINDOW_RESTORED:
      case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
      case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: {
        HandleWindowEvent(event);
        break;
      }
      default:
        break;
    }
  }
}

void FrameLoop::HandleWindowEvent(const SDL_Event& event) {
  if (auto* userSettings = registry_->TryGet<octarine::UserSettings>()) {
    CaptureWindowStateOnEvent(*userSettings, runtime_->Window(), event.type);
  }
  if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
      event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
    auto* viewportInfo = registry_->TryGet<ViewportInfo>();
    const auto* config = registry_->TryGet<GameConfig>();
    if (viewportInfo != nullptr && config != nullptr) {
      SyncViewportRectFromWindow(runtime_->Window(), runtime_->SdlRenderer(), *config, *viewportInfo);
    }
    if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED && runtime_->SdlRenderer() != nullptr && config != nullptr) {
      Renderer::ApplyVSync(runtime_->SdlRenderer(), config->GetEngineOptions().vsync);
    }
  }
}

void FrameLoop::SyncAudioGain(const EngineOptions& options) {
  // Master volume + mute live at the mixer level so they apply to every track (including loops
  // already playing). Synced every frame — and outside the pause gate — so toggling mute reacts
  // immediately even while execution is paused.
  if (auto* mixer = registry_->Get<EngineContext>().mixer) {
    const float masterGain = options.audioEnabled ? std::max(0.0F, options.masterVolume) : 0.0F;
    MIX_SetMixerGain(mixer, masterGain);
  }
}

void FrameLoop::PollHotReload([[maybe_unused]] const EngineOptions& options, [[maybe_unused]] const float deltaTime) {
#ifndef OCTARINE_SHIPPED
  // Run hot reload even when paused — authors iterate code with the game paused all the time,
  // and the swap is cheap. Uses real deltaTime (not time-scaled) so the poll cadence is stable.
  if (options.hotReloadEnabled) {
    if (auto* hotReload = registry_->TryGet<ScriptHotReload>()) {
      hotReload->Tick(*registry_, lua_, deltaTime, options.hotReloadPollSeconds);
    }
    if (auto* assetReload = registry_->TryGet<AssetHotReload>()) {
      auto& ctx = registry_->Get<EngineContext>();
      assetReload->Tick(registry_->Get<AssetManager>(), ctx.sdlRenderer, ctx.mixer, deltaTime,
                        options.hotReloadPollSeconds);
    }
    if (auto* uiRuntime = registry_->TryGet<UIRuntime>()) {
      uiRuntime->PollHotReload(deltaTime, options.hotReloadPollSeconds);
    }
    if (auto* tilemap = registry_->TryGet<TilemapSystem>()) {
      tilemap->PollHotReload(*registry_, deltaTime, options.hotReloadPollSeconds);
    }
  }
#endif
}

int FrameLoop::ExecuteSimulationSteps(const float deltaTime, EngineOptions& options, InputSystem* inputSystem) {
  if (options.isPaused && !options.stepFrame) {
    return 0;
  }

  // dt precedence: an explicit fixedStepDt wins; otherwise sub-stepping implies the default
  // 1/60s fixed step; otherwise variable frame dt scaled by timeScale. timeScale is ignored
  // whenever a fixed step is in effect — fast-forward is expressed through simSubSteps instead.
  constexpr float kDefaultFixedStepDt = 1.0F / 60.0F;
  const int subSteps = std::max(1, options.simSubSteps);
  float stepDt = deltaTime * options.timeScale;
  if (options.fixedStepDt > 0.0F) {
    stepDt = options.fixedStepDt;
  } else if (subSteps > 1) {
    stepDt = kDefaultFixedStepDt;
  }

  auto& renderQueue = registry_->Get<RenderQueue>();
  int stepsExecuted = 0;
  for (int step = 0; step < subSteps; ++step) {
    if (step > 0) {
      game_->FlushPendingSceneLoad();
      if (options.isPaused && !options.stepFrame) {
        break;
      }
    }
    renderQueue.Clear();
    registry_->Update(stepDt);
    ++stepsExecuted;
    // Edge-triggered input (pressed/released, wheel) must be observed by exactly one sim step,
    // otherwise a single key press would re-fire on every sub-step of a fast-forwarded frame.
    if (step == 0 && subSteps > 1 && inputSystem) {
      inputSystem->ClearPerFrameInput();
    }
  }
  options.stepFrame = false;
  return stepsExecuted;
}

int FrameLoop::Update(const float deltaTime) {
#ifdef OCTARINE_PROFILING
  PerfUtils::ProfilingAccumulator::Clear();
  PerfUtils::PerfCounters::ResetValues();
#endif
  PROFILE_NAMED_SCOPE("Game::Update (total)");

  // Apply any scene swap requested during ProcessInput (UIButton clicks fire there) or by a script
  // last frame. Done here — before systems run, after event dispatch finished — so the swap is
  // outside every ForEach and the new scene's entities get their global transforms this same frame.
  // Runs regardless of pause so menu navigation works while the game is paused.
  game_->FlushPendingSceneLoad();

#ifndef OCTARINE_SHIPPED
  if (auto* devListen = registry_->TryGet<octarine::dev::DevListenServer>()) {
    devListen->Pump();
  }
#endif

  auto& options = registry_->Get<GameConfig>().GetEngineOptions();
  SyncAudioGain(options);

  if (auto* ctx = registry_->TryGet<EngineContext>()) {
    ctx->perfStats = &perf_tracker_.GetStats();
    const auto& gameConfig = registry_->Get<GameConfig>();
    const bool isHeadless = game_->IsHeadless() || options.headless;
    PrepareSceneTargetForWindow(*renderer_, runtime_->SdlRenderer(), gameConfig, isHeadless || !capture_path_.empty());
    ctx->sceneRenderScale = renderer_->GetRenderScale();
  }

  auto* inputSystem = registry_->TryGet<InputSystem>();
  if (inputSystem) {
    inputSystem->BeginFrame();
  }

  PollHotReload(options, deltaTime);

  const int stepsExecuted = ExecuteSimulationSteps(deltaTime, options, inputSystem);

  // Pressed/released keys and wheel deltas are per-frame edge signals — clear after every
  // system in this frame has observed them.
  if (inputSystem) {
    inputSystem->ClearPerFrameInput();
  }
  return stepsExecuted;
}

void FrameLoop::UpdateViewportInfo([[maybe_unused]] const bool showDebugGUI) {
  auto& viewportInfo = registry_->Get<ViewportInfo>();
  const auto& gameConfig = registry_->Get<GameConfig>();
  SyncViewportRectFromWindow(runtime_->Window(), runtime_->SdlRenderer(), gameConfig, viewportInfo);

#ifdef OCTARINE_WITH_IMGUI
  const auto& io = ImGui::GetIO();
  if (!showDebugGUI && !RenderDebugGUISystem::HasActiveScriptErrorToast()) {
    viewportInfo.isHovered = true;
    viewportInfo.isFocused = true;
  } else {
    viewportInfo.isHovered = !io.WantCaptureMouse;
    viewportInfo.isFocused = !io.WantCaptureKeyboard;
  }
#else
  (void)showDebugGUI;
  viewportInfo.isHovered = true;
  viewportInfo.isFocused = true;
#endif
}

void FrameLoop::CheckHeadlessCapture() {
  // Headless capture: once the target frame is reached, write the rendered scene to disk and quit.
  if (!capture_path_.empty() && !capture_done_) {
    if (frame_index_ >= capture_frame_) {
      if (renderer_->CaptureScene(runtime_->SdlRenderer(), capture_path_)) {
        Logger::Info("FrameLoop: captured frame " + std::to_string(frame_index_) + " to " + capture_path_);
      }
      capture_done_ = true;
      Game::Quit();
    }
    ++frame_index_;
  }
}

void FrameLoop::Render(const float deltaTime) {
  PROFILE_NAMED_SCOPE("Game::Render (total)");
  perf_tracker_.RecordFrame(deltaTime, registry_->GetUserEntityCount());
  auto& renderQueue = registry_->Get<RenderQueue>();
  auto& gameConfig = registry_->Get<GameConfig>();

  PROFILE_COUNTER_SET("RenderQueue: Size", static_cast<long long>(renderQueue.Size()));
  PROFILE_COUNTER_SET("Entities: User", static_cast<long long>(registry_->GetUserEntityCount()));

  const bool isHeadless = game_->IsHeadless() || gameConfig.GetEngineOptions().headless;
  if (isHeadless && capture_path_.empty()) {
    PROFILE_COUNTERS_REPORT();
    renderQueue.Clear();
    return;
  }

  const auto compositeRect = PrepareSceneTargetForWindow(*renderer_, runtime_->SdlRenderer(), gameConfig,
                                                         isHeadless || !capture_path_.empty());

  renderer_->BeginScene(runtime_->SdlRenderer());

#ifdef OCTARINE_PROFILING
  {
    PerfUtils::ScopedTimer sortTimer("Render: Sort");
    renderQueue.Sort();
  }
  const auto rqStats = AnalyzeRenderQueue(renderQueue, renderer_->GetRenderScale());
  PROFILE_COUNTER_SET("RenderQueue: Batches", static_cast<long long>(rqStats.drawBatches));
  PROFILE_COUNTER_SET("RenderQueue: TextureSwitches", static_cast<long long>(rqStats.textureSwitches));
  PROFILE_COUNTER_SET("RenderQueue: Vertices", static_cast<long long>(rqStats.totalVertices));
  {
    PerfUtils::ScopedTimer drawTimer("Render: Draw");
    renderer_->DrawQueue(renderQueue, runtime_->SdlRenderer());
  }
#else
  renderQueue.Sort();
  renderer_->DrawQueue(renderQueue, runtime_->SdlRenderer());
#endif

  if (gameConfig.GetEngineOptions().drawColliders) {
    draw_collider_system_.Draw(registry_);
  }

  renderer_->EndScene(runtime_->SdlRenderer());

  UpdateViewportInfo(gameConfig.GetEngineOptions().showDebugGUI);

  if (compositeRect.has_value()) {
    renderer_->CompositeSceneToWindow(runtime_->SdlRenderer(), &*compositeRect);
  } else {
    const auto& viewportInfo = registry_->Get<ViewportInfo>();
    const SDL_FRect destRect{viewportInfo.x, viewportInfo.y, viewportInfo.width, viewportInfo.height};
    renderer_->CompositeSceneToWindow(runtime_->SdlRenderer(), &destRect);
  }

  if (!game_->IsBenchMode()) {
#ifdef OCTARINE_WITH_IMGUI
    RenderDebugGUISystem::Render(registry_, runtime_->SdlRenderer(), deltaTime);
#endif
  }

#ifdef OCTARINE_PROFILING
  {
    PerfUtils::ScopedTimer presentTimer("Render: Present");
    renderer_->Present(runtime_->SdlRenderer());
  }
#else
  renderer_->Present(runtime_->SdlRenderer());
#endif
  // Emit per-frame counters as COUNTER lines so headless bench runs can capture them
  // alongside TIMER lines. All systems for this frame have already written their values.
  PROFILE_COUNTERS_REPORT();
  renderQueue.Clear();

  CheckHeadlessCapture();
}

float FrameLoop::WaitTime() {
  PROFILE_NAMED_SCOPE("Game::WaitTime");
  const int fpsTarget = registry_->Get<GameConfig>().GetEngineOptions().fpsTarget;
  if (fpsTarget > 0) {
    const Uint64 nsPerFrame = SDL_NS_PER_SECOND / static_cast<Uint64>(fpsTarget);
    const Uint64 elapsedTime = SDL_GetTicksNS() - nanoseconds_previous_frame_;
    if (elapsedTime < nsPerFrame) {
      const Uint64 remaining = nsPerFrame - elapsedTime;
      // OS sleep for the bulk of the wait, reserving ~1.5ms for spin-wait to avoid
      // OS timer interrupt jitter/oversleeping on macOS and Windows schedulers.
      constexpr Uint64 kSleepMarginNs = 1'500'000;
      if (remaining > kSleepMarginNs) {
        SDL_DelayNS(remaining - kSleepMarginNs);
      }
      while (SDL_GetTicksNS() - nanoseconds_previous_frame_ < nsPerFrame) {
        SDL_CPUPauseInstruction();
      }
    }
  }

  const Uint64 now = SDL_GetTicksNS();
  const auto intermediate =
      static_cast<double>(now - nanoseconds_previous_frame_) / static_cast<double>(SDL_NS_PER_SECOND);
  const auto deltaTime = static_cast<float>(intermediate);

  nanoseconds_previous_frame_ = now;

  return deltaTime;
}

KeyInputEvent FrameLoop::GetKeyInputEvent(SDL_KeyboardEvent* event) {
  bool isPressed = event->down;
  return {event->key, event->mod, isPressed};
}
