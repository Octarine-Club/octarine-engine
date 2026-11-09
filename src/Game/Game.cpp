#include "Game/Game.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <sol/sol.hpp>
#include <string>
#include <string_view>
#include <unordered_set>

#include "AssetManager/AssetCatalog.h"
#include "AssetManager/AssetHotReload.h"
#include "AssetManager/AssetPak.h"
#include "Components/BoxColliderComponent.h"
#include "Components/CameraComponents.h"
#include "Components/CapsulePrimitiveComponent.h"
#include "Components/CirclePrimitiveComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/LinePrimitiveComponent.h"
#include "Components/PivotComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/RotationComponent.h"
#include "Components/ScaleComponent.h"
#include "Components/ScriptComponent.h"
#include "Components/SpriteComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "Components/TextLabelComponent.h"
#include "Components/TrianglePrimitiveComponent.h"
#include "Components/ViewportInfo.h"
#include "Debug/DebugMenuRegistry.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Engine/EngineBootstrap.h"
#include "Engine/LuaProtect.h"
#include "Engine/Platform/PlatformPaths.h"
#include "Engine/SdlFileReader.h"
#include "Engine/SimulationSystems.h"
#include "Events/KeyInputEvent.h"
#include "Events/MouseInputEvent.h"
#include "Events/MouseWheelEvent.h"
#include "Game/GameConfig.h"
#include "Game/UserSettings.h"
#include "General/Logger.h"
#include "General/PerfUtils.h"
#include "General/Rect.h"
#include "Project/ProjectIni.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/Renderer.h"
#include "Renderer/SpriteRenderCache.h"
#include "UI/UIRuntime.h"
// InputSystemLuaBinding specializes LuaSystemBinding<InputSystem> — needed below where
// LuaSystemRegistry::registerSystem(inputSystem) instantiates the lookup. Don't drop.
#include "Components/AudioActiveTag.h"
#include "Components/AudioListenerComponent.h"
#include "Components/LifetimeComponent.h"
#include "Engine/GameModuleHooks.h"
#include "Lua/Bindings/InputSystemLuaBinding.h"
#include "Lua/Bindings/LuaSystemRegistry.h"
#include "Lua/HotReload/ScriptHotReload.h"
#include "Lua/LuaApiManifest.h"
#include "Lua/LuaEntityLoader.h"
#include "Systems/AnimationSystem.h"
#include "Systems/AudioCullingSystem.h"
#include "Systems/AudioSystem.h"
#include "Systems/CameraFollowSystem.h"
#include "Systems/DopplerSystem.h"
#include "Systems/DrawColliderSystem.h"
#include "Systems/InputSystem.h"
#include "Systems/KDTreeCollisionSystem.h"
#include "Systems/LifetimeSystem.h"
#include "Systems/OffScreenDespawnSystem.h"
#include "Systems/RenderCapsulePrimitiveSystem.h"
#include "Systems/RenderCirclePrimitiveSystem.h"
#include "Systems/RenderColorGridSystem.h"
#include "Systems/RenderLinePrimitiveSystem.h"
#include "Systems/RenderPrimitiveSystem.h"
#include "Systems/RenderSpriteSystem.h"
#include "Systems/RenderTextSystem.h"
#include "Systems/RenderTrianglePrimitiveSystem.h"
#include "Systems/RenderUIPrimitiveSystem.h"
#include "Systems/RenderUISpriteSystem.h"
#include "Systems/ScriptCollisionSystem.h"
#include "Systems/ScriptSystem.h"
#include "Systems/SpatialAudioSystem.h"
#include "Systems/TilemapSystem.h"
#include "Systems/TransformSystem.h"
#include "Systems/UIButtonSystem.h"
#include "Systems/UILayoutSystem.h"
#include "Systems/UpdateListenerTransformSystem.h"
#include "Systems/VelocityIntegrationSystem.h"

#ifdef OCTARINE_WITH_IMGUI
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#endif

#ifndef OCTARINE_SHIPPED
#include "Dev/DevListenDispatch.h"
#include "Dev/DevListenServer.h"
#endif

inline void LoadGame(sol::state& lua, const AssetManager& assetManager, const GameConfig& gameConfig) {
  const auto filePath = assetManager.GetFullPath(gameConfig.GetStartupScript());

  Logger::Info("Loading entry script: " + filePath);
  auto bytes = ReadFileViaSDL(filePath, &assetManager);
  if (!bytes) {
    Logger::Error("Failed to read entry script: " + filePath);
    return;
  }
  const bool requireProtected = (assetManager.GetAssetPak() != nullptr);
  if (!DecryptLuaBytes(*bytes, requireProtected)) {
    Logger::Error("Failed to decrypt or verify entry script: " + filePath);
    return;
  }

  sol::protected_function_result result;
  try {
    result = lua.safe_script(*bytes, sol::script_pass_on_error, "@" + filePath);
  } catch (const std::exception& ex) {
    std::fill(bytes->begin(), bytes->end(), '\0');
    bytes->clear();
    Logger::Error(ex.what());
    return;
  }
  std::fill(bytes->begin(), bytes->end(), '\0');
  bytes->clear();

  if (!result.valid()) {
    const sol::error err = result;
    Logger::Error("Failed to load entry script '" + filePath + "': " + err.what());
    return;
  }

  Logger::Info("Just opened entry script: " + filePath);
}

Game::Game() {
  registry_ = std::make_unique<Registry>();
  event_bus_ = std::make_unique<EventBus>();
  renderer_ = std::make_unique<Renderer>();
  scene_loader_ = std::make_unique<SceneLoader>(registry_.get(), lua);
#ifndef OCTARINE_SHIPPED
  // Expose the scene loader as a Registry singleton so the dev-iteration listener's reload_scene
  // op can reach it from its main-thread dispatch context. Game still owns the instance; this is a
  // non-owning pointer handle.
  registry_->Set<SceneLoader*>(scene_loader_.get());
#endif
  frame_loop_ = std::make_unique<FrameLoop>(this, registry_.get(), event_bus_.get(), renderer_.get(), &runtime_, lua);
  // Generational GC collects the short-lived objects that dominate game-loop Lua allocations
  // (per-frame script tables, vectors, closures) cheaply in minor cycles, reserving full sweeps
  // for objects that actually survive. This reduces the p99 frame spikes caused by GC pauses.
  lua_gc(lua.lua_state(), LUA_GCGEN, 0, 0);
  Logger::Info("Game Constructor called.");
}

Game::~Game() { Logger::Info("Game Destructor called."); }

namespace {
constexpr const char* kHeadlessEnvVar = "OCTARINE_HEADLESS";
constexpr const char* kSavePathEnvVar = "OCTARINE_SAVE_PATH";
constexpr const char* kDefaultOrgName = "Octarine";
constexpr const char* kDefaultAppName = "Engine";
constexpr const char* kDefaultWindowTitle = "Octarine Engine";
#ifdef OCTARINE_WITH_IMGUI
constexpr const char* kImGuiIniFilename = "imgui.ini";
#endif

bool CheckHeadlessEnv() {
#ifdef _MSC_VER
  char* envVal = nullptr;
  size_t envLen = 0;
  if (_dupenv_s(&envVal, &envLen, kHeadlessEnvVar) == 0 && envVal != nullptr) {
    const std::string s(envVal);
    free(envVal);
    return s == "1" || s == "true";
  }
  return false;
#else
  if (const char* envVal = std::getenv(kHeadlessEnvVar)) {
    const std::string s(envVal);
    return s == "1" || s == "true";
  }
  return false;
#endif
}

void ApplyHeadlessOptions(EngineOptions& opts) {
  opts.headless = true;
  opts.fpsTarget = 0;
  opts.audioEnabled = false;
  opts.showDebugGUI = false;
  opts.hotReloadEnabled = false;
}

void InitUserSettings(Registry& registry, GameConfig& gameConfig, const std::string& saveDataPath) {
  octarine::UserSettings userSettings;
  auto& opts = gameConfig.GetEngineOptions();
  userSettings.builtInDefaults.windowWidth =
      gameConfig.HasLoadedConfig() ? gameConfig.GetDefaultWidth() : Constants::kDefaultWindowWidth;
  userSettings.builtInDefaults.windowHeight =
      gameConfig.HasLoadedConfig() ? gameConfig.GetDefaultHeight() : Constants::kDefaultWindowHeight;
  userSettings.builtInDefaults.windowMode = opts.windowMode;
  userSettings.builtInDefaults.presentationMode = opts.presentationMode;
  if (gameConfig.HasLoadedConfig() && gameConfig.GetDefaultScaleMode().has_value()) {
    if (const auto parsed = ParseScaleModeString(*gameConfig.GetDefaultScaleMode())) {
      userSettings.builtInDefaults.textureScaleMode = toScaleModeString(*parsed);
    }
  }
  userSettings.builtInDefaults.vsync = opts.vsync;
  userSettings.builtInDefaults.windowResizable = opts.windowResizable;
  userSettings.builtInDefaults.fpsTarget = opts.fpsTarget;
  userSettings.builtInDefaults.masterVolume = opts.masterVolume;
  userSettings.builtInDefaults.audioMuted = !opts.audioEnabled;

  // Seed initial state from GameConfig defaults before loading user overrides.
  userSettings.SetWindowMode(opts.windowMode);

  if (!saveDataPath.empty()) {
    static_cast<void>(userSettings.Load(saveDataPath));
  }
  opts.masterVolume = userSettings.masterVolume;
  opts.audioEnabled = !userSettings.audioMuted;
  opts.windowMode = userSettings.GetEffectiveWindowMode();
  if (userSettings.presentationMode.has_value()) {
    opts.presentationMode = *userSettings.presentationMode;
  }
  if (userSettings.textureScaleMode.has_value() && !userSettings.textureScaleMode->empty()) {
    gameConfig.SetDefaultScaleMode(*userSettings.textureScaleMode);
  }
  if (userSettings.vsync.has_value()) {
    opts.vsync = *userSettings.vsync;
  }
  if (userSettings.windowResizable.has_value()) {
    opts.windowResizable = *userSettings.windowResizable;
  }
  if (userSettings.fpsTarget.has_value()) {
    opts.fpsTarget = *userSettings.fpsTarget;
  }
  registry.Set<octarine::UserSettings>(userSettings);
}

#if !defined(OCTARINE_SHIPPED) || defined(OCTARINE_ALLOW_LOOSE_ASSETS)
std::string ResolveProjectIniPath(const std::string& assetBase) {
  if (assetBase.empty()) {
    return octarine::project::ProjectIni::kFilename;
  }
  if (assetBase.back() == '/' || assetBase.back() == '\\') {
    return assetBase + octarine::project::ProjectIni::kFilename;
  }
  return assetBase + "/" + octarine::project::ProjectIni::kFilename;
}
#endif

std::string ReadSavePathEnv() {
  std::string path;
#ifdef _MSC_VER
  char* envVal = nullptr;
  size_t envLen = 0;
  if (_dupenv_s(&envVal, &envLen, kSavePathEnvVar) == 0 && envVal != nullptr) {
    path = envVal;
    free(envVal);
  }
#else
  if (const char* envVal = std::getenv(kSavePathEnvVar)) {
    path = envVal;
  }
#endif
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  return path;
}

bool HasExplicitSavePathEnv() { return !ReadSavePathEnv().empty(); }

std::optional<std::string> ReadProjectIniBytes([[maybe_unused]] const GameConfig& config, const AssetPak* pak) {
  if (pak != nullptr && pak->IsOpen() && pak->Contains(octarine::project::ProjectIni::kFilename)) {
    if (SDL_IOStream* io = pak->OpenIO(octarine::project::ProjectIni::kFilename)) {
      size_t dataSize = 0;
      if (void* data = SDL_LoadFile_IO(io, &dataSize, true)) {
        std::string text(static_cast<const char*>(data), dataSize);
        SDL_free(data);
        return text;
      }
    }
  }
#if !defined(OCTARINE_SHIPPED) || defined(OCTARINE_ALLOW_LOOSE_ASSETS)
  const std::string iniPath = ResolveProjectIniPath(config.GetAssetPath());
  size_t dataSize = 0;
  if (void* data = SDL_LoadFile(iniPath.c_str(), &dataSize)) {
    std::string text(static_cast<const char*>(data), dataSize);
    SDL_free(data);
    return text;
  }
#endif
  return std::nullopt;
}

std::pair<std::string, std::string> ResolveSaveDataIdentity(const GameConfig* config, const AssetPak* pak = nullptr) {
  std::string org = kDefaultOrgName;
  std::string app = kDefaultAppName;
  if (config == nullptr || !config->HasLoadedConfig()) {
    return {org, app};
  }
  if (!config->GetGameTitle().empty()) {
    app = config->GetGameTitle();
  }
  if (const auto iniBytes = ReadProjectIniBytes(*config, pak)) {
    const auto ini = octarine::project::ProjectIni::Parse(*iniBytes);
    if (!ini.vendor.empty()) {
      org = ini.vendor;
    }
    if (!ini.name.empty()) {
      app = ini.name;
    }
  }
  return {org, app};
}

bool LoadProjectConfig(Registry& registry, GameConfig& gameConfig, const std::string& effectivePath, bool useManifest) {
  bool allowPak = useManifest;
#ifdef OCTARINE_SHIPPED
  allowPak = true;
#endif

  if (allowPak) {
    const std::string pakPath = (std::filesystem::path(effectivePath) / AssetPak::kDefaultFilename).string();
    auto& pak = registry.Set<AssetPak>(AssetPak());
    if (pak.Open(pakPath) && pak.Contains(GameConfig::kConfigFileName)) {
      if (SDL_IOStream* io = pak.OpenIO(GameConfig::kConfigFileName)) {
        if (gameConfig.LoadConfigFromStream(io, effectivePath)) {
          Logger::Info(std::string("Loaded ") + GameConfig::kConfigFileName + " directly from " +
                       AssetPak::kDefaultFilename);
          return true;
        }
      }
    }
#if defined(OCTARINE_SHIPPED) && !defined(OCTARINE_ALLOW_LOOSE_ASSETS)
    Logger::Error(std::string("Shipped build requires a valid ") + AssetPak::kDefaultFilename + " containing " +
                  GameConfig::kConfigFileName + " at: " + effectivePath);
    return false;
#endif
  }

  if (engine_bootstrap::platform::ShouldAttemptProjectLoad(effectivePath)) {
    if (gameConfig.LoadConfigFromFile(effectivePath)) {
      return true;
    }
    Logger::Warn("Failed to load project from path: " + effectivePath);
  }
  return false;
}

void SetupAssetCatalogAndPak(Registry& registry, AssetManager& assetManager, sol::state& lua,
                             const GameConfig& gameConfig, bool useManifest) {
  assetManager.LoadGameConfig(gameConfig);

  bool allowManifest = useManifest;
#ifdef OCTARINE_SHIPPED
  allowManifest = true;
#endif

  const AssetPak* activePak = nullptr;
  if (allowManifest) {
    if (auto* existingPak = registry.TryGet<AssetPak>(); existingPak != nullptr && existingPak->IsOpen()) {
      activePak = existingPak;
    } else {
      const std::string pakPath =
          (std::filesystem::path(assetManager.GetBasePath()) / AssetPak::kDefaultFilename).string();
      auto& pak = registry.Set<AssetPak>(AssetPak());
      if (pak.Open(pakPath)) {
        activePak = &pak;
      }
    }
    if (activePak != nullptr) {
      assetManager.SetAssetPak(activePak);
    }
  }

  assetManager.GetCatalog().Build(assetManager.GetBasePath(), lua, gameConfig, allowManifest, activePak);
  assetManager.GetCatalog().DumpToLog();
  assetManager.GetCatalog().InstallLuaIdTable(lua);
}
}  // namespace

bool Game::Initialize(const std::string& assetPath) {
  if (!headless_) {
    headless_ = CheckHeadlessEnv();
  }

  if (!runtime_.InitSubsystems(headless_)) {
    return false;
  }

  registry_->Set<GameConfig>(GameConfig());
  auto& gameConfig = registry_->Get<GameConfig>();

  StartDevListenServer();

  std::string effectivePath = assetPath;
  engine_bootstrap::platform::ApplyDefaultBasePath(effectivePath);

  const bool projectLoaded = LoadProjectConfig(*registry_, gameConfig, effectivePath, use_manifest_);

  InitUserSettings(*registry_, gameConfig, GetSaveDataPath());

  if (!projectLoaded) {
    Logger::Error("Failed to load project from path: '" + effectivePath + "'. A valid project directory containing " +
                  GameConfig::kConfigFileName + " is required.");
    return false;
  }

  if (headless_) {
    ApplyHeadlessOptions(gameConfig.GetEngineOptions());
  }

  if (!CreateWindowAndScene(projectLoaded)) {
    return false;
  }

  InitImGuiBackend();

  // Populate the engine-level resource bundle now that SDL is up. AssetManager + mixer are
  // filled in by Setup once those exist; consumers read the same instance via
  // Registry::Get<EngineContext>().
  EngineContext ctx;
  ctx.sdlWindow = runtime_.Window();
  ctx.sdlRenderer = runtime_.SdlRenderer();
  ctx.eventBus = event_bus_.get();
  ctx.config = &gameConfig;
  ctx.userSettings = registry_->TryGet<octarine::UserSettings>();
  registry_->Set<EngineContext>(ctx);

  s_is_running_ = true;
  return true;
}

void Game::InitImGuiBackend() {
#ifdef OCTARINE_WITH_IMGUI
  auto& gameConfig = registry_->Get<GameConfig>();
  static std::string imguiIniPath;
  if (gameConfig.HasLoadedConfig()) {
    imguiIniPath = gameConfig.GetAssetPath() + "/" + kImGuiIniFilename;
  } else {
    imguiIniPath = kImGuiIniFilename;
  }
  runtime_.InitImGui(imguiIniPath.c_str());
  octarine::debug::InstallImGuiSettingsHandler(*registry_);

  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->AddFontDefault();
#endif
}

void Game::StartDevListenServer() {
#ifndef OCTARINE_SHIPPED
  // DevListenServer: TCP listener on 127.0.0.1:<port> when --dev-listen given. Compile-stripped
  // from shipped builds. Available in dev player presets so the standalone player can
  // host the dev-iterate loop too.
  auto& devListen = registry_->Set<octarine::dev::DevListenServer>(octarine::dev::DevListenServer());
  // Install the engine handler so parked eval/reload/push ops apply on the main thread in Pump().
  devListen.SetCommandHandler(octarine::dev::MakeEngineCommandHandler(*registry_, lua, this));
  if (dev_listen_port_ > 0) {
    octarine::dev::ServerOptions opts;
    opts.port = static_cast<std::uint16_t>(dev_listen_port_);
    if (!devListen.Start(opts)) {
      Logger::Error("DevListenServer failed to start; --dev-listen flag had no effect.");
    }
  }
#endif
}

namespace {
std::pair<int, int> ResolveInitialWindowSize([[maybe_unused]] Registry& registry, const GameConfig& gameConfig,
                                             [[maybe_unused]] const bool headless) {
  int initialWidth = gameConfig.windowWidth;
  int initialHeight = gameConfig.windowHeight;
#ifndef OCTARINE_PLATFORM_MOBILE
  if (!headless) {
    if (const auto* userSettings = registry.TryGet<octarine::UserSettings>()) {
      if (userSettings->windowWidth > 0 && userSettings->windowHeight > 0) {
        initialWidth = userSettings->windowWidth;
        initialHeight = userSettings->windowHeight;
      }
    }
  }
#endif
  return {initialWidth, initialHeight};
}

void ApplyInitialWindowSettings(Registry& registry, const GameConfig& gameConfig, EngineRuntime& runtime,
                                const bool headless) {
  if (headless) return;
  if (auto* userSettings = registry.TryGet<octarine::UserSettings>()) {
    userSettings->ApplyToWindow(runtime.Window(), gameConfig.windowWidth, gameConfig.windowHeight);
  }
  if (runtime.SdlRenderer() != nullptr) {
    Renderer::ApplyVSync(runtime.SdlRenderer(), gameConfig.GetEngineOptions().vsync);
  }
}

void ApplyProjectWindowIcon(Registry& registry, const GameConfig& gameConfig, EngineRuntime& runtime,
                            const bool headless) {
  if (headless) return;
  const auto* pak = registry.TryGet<AssetPak>();
  const auto iniBytes = ReadProjectIniBytes(gameConfig, pak);
  if (!iniBytes) return;
  const auto ini = octarine::project::ProjectIni::Parse(*iniBytes);
  if (ini.icon.empty()) return;

  if (pak != nullptr && pak->IsOpen() && pak->Contains(ini.icon)) {
    if (SDL_IOStream* io = pak->OpenIO(ini.icon)) {
      size_t dataSize = 0;
      if (void* data = SDL_LoadFile_IO(io, &dataSize, true)) {
        runtime.SetWindowIcon(data, dataSize);
        SDL_free(data);
        return;
      }
    }
  }
#if !defined(OCTARINE_SHIPPED) || defined(OCTARINE_ALLOW_LOOSE_ASSETS)
  const std::filesystem::path iconPath = std::filesystem::path(gameConfig.GetAssetPath()) / ini.icon;
  size_t dataSize = 0;
  if (void* data = SDL_LoadFile(iconPath.string().c_str(), &dataSize)) {
    runtime.SetWindowIcon(data, dataSize);
    SDL_free(data);
  }
#endif
}
}  // namespace

bool Game::CreateWindowAndScene(bool projectLoaded) {
  auto& gameConfig = registry_->Get<GameConfig>();
  gameConfig.windowWidth = projectLoaded ? gameConfig.GetDefaultWidth() : Constants::kDefaultWindowWidth;
  gameConfig.windowHeight = projectLoaded ? gameConfig.GetDefaultHeight() : Constants::kDefaultWindowHeight;
  const std::string title = projectLoaded ? gameConfig.GetGameTitle() : kDefaultWindowTitle;

  const auto [initialWidth, initialHeight] = ResolveInitialWindowSize(*registry_, gameConfig, headless_);
  const bool resizable = gameConfig.GetEngineOptions().windowResizable;
  if (!runtime_.CreateWindow(title, initialWidth, initialHeight, headless_, resizable)) {
    return false;
  }

  ApplyInitialWindowSettings(*registry_, gameConfig, runtime_, headless_);
  if (projectLoaded) {
    ApplyProjectWindowIcon(*registry_, gameConfig, runtime_, headless_);
  }

  return renderer_->CreateScene(runtime_.SdlRenderer(), gameConfig.windowWidth, gameConfig.windowHeight);
}

std::string Game::GetProjectPath() const {
  const auto* config = registry_->TryGet<GameConfig>();
  return config != nullptr ? config->GetAssetPath() : std::string{};
}

std::string Game::GetSaveDataPath() {
  if (!save_data_path_.empty()) {
    return save_data_path_;
  }

  // Environment override: allows development runs, CI, or portable builds to isolate save data.
  if (std::string envPath = ReadSavePathEnv(); !envPath.empty()) {
    save_data_path_ = std::move(envPath);
    return save_data_path_;
  }

  // Identity precedence: project.ini vendor/name > config.ini GameTitle > engine defaults.
  // project.ini is read from AssetPak when open, or via SDL so it resolves inside an APK or .app
  // bundle the same way config.ini does.
  const auto [org, app] = ResolveSaveDataIdentity(registry_->TryGet<GameConfig>(), registry_->TryGet<AssetPak>());

  if (char* prefPath = SDL_GetPrefPath(org.c_str(), app.c_str())) {
    save_data_path_ = prefPath;
    SDL_free(prefPath);
  } else {
    Logger::Error("Game::GetSaveDataPath: SDL_GetPrefPath failed: " + std::string(SDL_GetError()));
  }
  return save_data_path_;
}

bool Game::SaveUserSettings() {
  if (!registry_ || bake_mode_) return false;
  auto* userSettings = registry_->TryGet<octarine::UserSettings>();
  if (userSettings == nullptr) return false;

  const auto* config = registry_->TryGet<GameConfig>();
  const bool isHeadless = headless_ || (config != nullptr && config->GetEngineOptions().headless);
  if (!isHeadless) {
    if (runtime_.Window() != nullptr) {
      userSettings->CaptureWindow(runtime_.Window());
    }
    if (config != nullptr) {
      userSettings->masterVolume = config->GetEngineOptions().masterVolume;
      userSettings->audioMuted = !config->GetEngineOptions().audioEnabled;
    }
  }

  const std::string savePath = GetSaveDataPath();
  if (savePath.empty()) return false;
  return userSettings->Save(savePath);
}

void Game::Destroy() {
  if (registry_) {
#ifndef OCTARINE_SHIPPED
    // Stop the dev listener before the registry teardown drops it so the listener thread
    // joins cleanly while the engine is still alive.
    if (auto* devListen = registry_->TryGet<octarine::dev::DevListenServer>()) {
      devListen->Stop();
    }
#endif

    if (!bake_mode_ && (!headless_ || HasExplicitSavePathEnv())) {
      static_cast<void>(SaveUserSettings());
    }
  }

#ifdef OCTARINE_WITH_IMGUI
  if (ImGui::GetCurrentContext() != nullptr) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.IniFilename != nullptr && io.IniFilename[0] != '\0') {
      ImGui::SaveIniSettingsToDisk(io.IniFilename);
    }
    octarine::debug::UninstallImGuiSettingsHandler();
  }
#endif

  // Tear registry/event bus down BEFORE SDL_Quit so owned systems (AudioSystem -> MIX_Quit,
  // AssetManager -> SDL_DestroyTexture) can still call into a live SDL.
  registry_.reset();
  event_bus_.reset();

  // Free the off-screen scene target while the SDL renderer is still live (EngineRuntime owns
  // the ImGui backend + window/renderer destroy + SDL_Quit, in that order).
  if (runtime_.SdlRenderer()) {
    renderer_->DestroyScene();
  }
  runtime_.Shutdown();
}

void Game::Run() {
  if (!is_setup_) {
    Setup();
    frame_loop_->Begin();
    is_setup_ = true;
  }

  while (s_is_running_) {
    frame_loop_->ProcessInput();
    const float deltaTime = frame_loop_->WaitTime();
    // current_tick_ counts simulation steps (not rendered frames), so sub-stepped fast-forward
    // and paused frames are accounted exactly.
    current_tick_ += static_cast<uint64_t>(frame_loop_->Update(deltaTime));
    frame_loop_->Render(deltaTime);
  }
}

void Game::Step(const float deltaTime) { StepTicks(1, deltaTime); }

void Game::StepTicks(const uint32_t count, const float deltaTime) {
  if (!is_setup_) {
    Setup();
    frame_loop_->Begin();
    is_setup_ = true;
  }
  if (count == 0) {
    return;
  }

  // Each requested tick runs one FrameLoop::Update, which itself may execute simSubSteps steps.
  for (uint32_t i = 0; i < count && s_is_running_; ++i) {
    frame_loop_->ProcessInput();
    if (registry_ && registry_->TryGet<GameConfig>()) {
      registry_->Get<GameConfig>().GetEngineOptions().stepFrame = true;
    }
    current_tick_ += static_cast<uint64_t>(frame_loop_->Update(deltaTime));
    if (i + 1 == count || !s_is_running_) {
      frame_loop_->Render(deltaTime);
    }
  }
}

void Game::SetPaused(const bool paused) {
  if (registry_ && registry_->TryGet<GameConfig>()) {
    registry_->Get<GameConfig>().GetEngineOptions().isPaused = paused;
  }
}

bool Game::IsPaused() const {
  if (registry_ && registry_->TryGet<GameConfig>()) {
    return registry_->Get<GameConfig>().GetEngineOptions().isPaused;
  }
  return false;
}

namespace {
void ApplyBenchModeOverlayOverrides(Registry& /*registry*/, GameConfig& gameConfig,
                                    const std::string& /*startupMode*/) {
  auto& options = gameConfig.GetEngineOptions();
  options.showDebugGUI = false;
  options.showEntityInfo = false;
  options.showImGuiDemoWindow = false;
  options.drawColliders = false;
}

void RegisterEventAndHotReloadSingletons(Registry& registry, const std::unique_ptr<EventBus>& eventBus) {
  auto& uiButtonSystem = registry.Set<UIButtonSystem>(UIButtonSystem());
  auto& scriptCollisionSystem = registry.Set<ScriptCollisionSystem>(ScriptCollisionSystem());
  uiButtonSystem.Init(&registry, eventBus);
  scriptCollisionSystem.Init(&registry, eventBus);
  game_module::RegisterEventSystems(registry, eventBus);
  registry.Set<ScriptHotReload>(ScriptHotReload());
  registry.Set<AssetHotReload>(AssetHotReload());
}
}  // namespace

void Game::Setup() {
  auto& gameConfig = registry_->Get<GameConfig>();

  engine_bootstrap::InstallCoreSingletons(*registry_, registry_->Get<EngineContext>(), gameConfig.windowWidth,
                                          gameConfig.windowHeight, /*withFramePathCaches=*/true, &lua);

  auto scriptSystem = registry_->RegisterSystem<ScriptComponent>(ScriptSystem());
  auto& inputSystem = engine_bootstrap::InstallInputSystem(*registry_, event_bus_);
  engine_bootstrap::RegisterAllComponentBindings();
  LuaSystemRegistry::clear();
  LuaSystemRegistry::registerSystem(inputSystem);
  engine_bootstrap::InstallLuaLibraries(lua);
#ifndef OCTARINE_SHIPPED
  const auto preBindingGlobals = LuaApiManifest::SnapshotGlobals(lua);
#endif
  engine_bootstrap::InstallPoolAndSingletons(*registry_);
  scriptSystem.Func().CreateLuaBindings(lua);
  engine_bootstrap::InstallLuaModules(lua, *this);
  LuaSystemRegistry::bindAll(lua);
  engine_bootstrap::SetCommonLuaGlobals(lua, gameConfig, startup_mode_, dev_mode_);
  game_module::RegisterDebugUI(*registry_);

#ifndef OCTARINE_SHIPPED
  if (!LuaApiManifest::MaybeDumpFromEnv(lua, preBindingGlobals) && dev_mode_) {
    LuaApiManifest::WriteProjectStub(lua, preBindingGlobals, gameConfig.GetAssetPath());
  }
#endif

  if (IsBenchMode()) {
    ApplyBenchModeOverlayOverrides(*registry_, gameConfig, startup_mode_);
  }

  auto& assetManager = registry_->Get<AssetManager>();
  if (gameConfig.HasLoadedConfig()) {
    SetupAssetCatalogAndPak(*registry_, assetManager, lua, gameConfig, use_manifest_);
  }

  auto audioSystem = registry_->RegisterSystem<AudioSourceComponent>(AudioSystem());
  if (!audioSystem.Func().Init(registry_.get(), event_bus_)) {
    Logger::Error("AudioSystem failed to initialize; audio disabled this session.");
  }
  registry_->Get<EngineContext>().mixer = audioSystem.Func().Mixer();

  if (gameConfig.HasLoadedConfig()) {
    LoadGame(lua, assetManager, gameConfig);
  }

  const auto simulation = engine_bootstrap::RegisterSimulationSystems(*registry_, event_bus_);
  const auto& transform = simulation.transform;

  registry_->Set<AudioListenerCache>(AudioListenerCache{});
  auto listenerTransform = registry_->RegisterBulkSystem<GlobalTransformComponent>(UpdateListenerTransformSystem());
  registry_->Tag<AudioActiveTag>();
  auto audioCulling = registry_->RegisterSystem<GlobalTransformComponent, AudioSourceComponent>(AudioCullingSystem());
  auto spatialAudio = registry_->RegisterSystem<GlobalTransformComponent, AudioSourceComponent, AudioSinkComponent>(
      SpatialAudioSystem());
  auto doppler =
      registry_->RegisterSystem<GlobalTransformComponent, RigidBodyComponent, AudioSourceComponent, AudioSinkComponent>(
          DopplerSystem());

  auto cameraFollow = registry_->RegisterSystem<PositionComponent, CameraFollowComponent>(CameraFollowSystem());

  // UI layout pass: resolves UIRectComponent for all canvas descendants. Runs after TransformSystem
  // so world-parented canvases see up-to-date globals; runs before render systems that read UIRectComponent.
  auto uiLayout = registry_->RegisterBulkSystem(UILayoutSystem());
  registry_->Order(uiLayout).After(transform);

  auto tilemapRender = registry_->RegisterBulkSystem(TilemapRenderSystem());
  auto renderSprite =
      registry_->RegisterParallelSystem<GlobalTransformComponent, SpriteComponent>(RenderSpriteSystem());
  registry_->Order(tilemapRender).After(cameraFollow).Before(renderSprite);
  auto renderUISprite = registry_->RegisterSystem<UIRectComponent, SpriteComponent>(RenderUISpriteSystem());
  auto renderUIPrimitive = registry_->RegisterBulkSystem(RenderUIPrimitiveSystem());
  auto renderText = registry_->RegisterSystem<TextLabelComponent>(RenderTextSystem());
  registry_->Order(renderUISprite).After(uiLayout);
  registry_->Order(renderUIPrimitive).After(uiLayout);
  registry_->Order(renderText).After(uiLayout);
  registry_->RegisterParallelSystem<SquarePrimitiveComponent, GlobalTransformComponent>(RenderPrimitiveSystem());
  registry_->RegisterParallelSystem<CirclePrimitiveComponent, GlobalTransformComponent>(RenderCirclePrimitiveSystem());
  registry_->RegisterParallelSystem<LinePrimitiveComponent, GlobalTransformComponent>(RenderLinePrimitiveSystem());
  registry_->RegisterParallelSystem<TrianglePrimitiveComponent, GlobalTransformComponent>(
      RenderTrianglePrimitiveSystem());
  registry_->RegisterParallelSystem<CapsulePrimitiveComponent, GlobalTransformComponent>(
      RenderCapsulePrimitiveSystem());
  registry_->RegisterParallelSystem<ColorGridComponent, GlobalTransformComponent>(RenderColorGridSystem());

  registry_->Order(listenerTransform).After(transform);
  registry_->Order(audioCulling).After(audioSystem);
  registry_->Order(spatialAudio).After(transform).After(listenerTransform).After(audioCulling).After(audioSystem);
  registry_->Order(doppler).After(transform).After(listenerTransform).After(audioCulling).After(audioSystem);
  registry_->Order(cameraFollow).After(transform).After(scriptSystem);

  RegisterEventAndHotReloadSingletons(*registry_, event_bus_);
}
