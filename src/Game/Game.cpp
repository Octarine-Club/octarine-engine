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
#include "AssetManager/SdlFileReader.h"
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
#include "Crypto/LuaProtect.h"
#include "Debug/DebugMenuRegistry.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Engine/EngineBootstrap.h"
#include "Engine/Platform/PlatformPaths.h"
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
#include "Systems/RenderDebugGUISystem.h"
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
#include "UI/UIPreviewer.h"
#endif

inline bool LoadGame(sol::state& lua, const AssetManager& assetManager, const GameConfig& gameConfig) {
  const auto filePath = assetManager.GetFullPath(gameConfig.GetStartupScript());

  Logger::Info("Loading entry script: " + filePath);
  auto bytes = ReadFileViaSDL(filePath, &assetManager);
  if (!bytes) {
    Logger::ErrorLua("Failed to read entry script: " + filePath);
    return false;
  }
  const bool requireProtected = (assetManager.GetAssetPak() != nullptr);
  if (!DecryptLuaBytes(*bytes, requireProtected)) {
    Logger::ErrorLua("Failed to decrypt or verify entry script: " + filePath);
    return false;
  }

  sol::protected_function_result result;
  try {
    result = lua.safe_script(*bytes, sol::script_pass_on_error, "@" + filePath);
  } catch (const std::exception& ex) {
    std::fill(bytes->begin(), bytes->end(), '\0');
    bytes->clear();
    Logger::ErrorLua(ex.what());
    return false;
  }
  std::fill(bytes->begin(), bytes->end(), '\0');
  bytes->clear();

  if (!result.valid()) {
    const sol::error err = result;
    Logger::ErrorLua("Failed to load entry script '" + filePath + "': " + err.what());
    return false;
  }

  Logger::Info("Just opened entry script: " + filePath);
  return true;
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
  lua_gc(lua.lua_state(), LUA_GCGEN);
  Logger::Info("Game Constructor called.");
}

Game::~Game() {
  Destroy();
  Logger::Info("Game Destructor called.");
}

namespace {
constexpr const char* kHeadlessEnvVar = "OCTARINE_HEADLESS";
constexpr const char* kFailOnLuaErrorEnvVar = "OCTARINE_FAIL_ON_LUA_ERROR";
constexpr const char* kSavePathEnvVar = "OCTARINE_SAVE_PATH";
constexpr const char* kDefaultOrgName = "Octarine";
constexpr const char* kDefaultAppName = "Engine";
constexpr const char* kDefaultWindowTitle = "Octarine Engine";
#ifdef OCTARINE_WITH_IMGUI
constexpr const char* kImGuiIniFilename = "imgui.ini";
#endif

bool CheckBoolEnv(const char* name) {
#ifdef _MSC_VER
  char* envVal = nullptr;
  size_t envLen = 0;
  if (_dupenv_s(&envVal, &envLen, name) == 0 && envVal != nullptr) {
    const std::string s(envVal);
    free(envVal);
    return s == "1" || s == "true";
  }
  return false;
#else
  if (const char* envVal = std::getenv(name)) {
    const std::string s(envVal);
    return s == "1" || s == "true";
  }
  return false;
#endif
}

bool CheckHeadlessEnv() { return CheckBoolEnv(kHeadlessEnvVar); }
bool CheckFailOnLuaErrorEnv() { return CheckBoolEnv(kFailOnLuaErrorEnvVar); }

uint64_t LatestScriptErrorSeq() {
  const auto errs = Logger::RecentScriptErrors();
  return errs.empty() ? 0 : errs.back().sequence;
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
  if (!quit_on_error_) {
    quit_on_error_ = CheckFailOnLuaErrorEnv();
  }
  initial_script_error_seq_ = LatestScriptErrorSeq();

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
  static std::string imguiIniPath;
  const std::string saveDataPath = GetSaveDataPath();
  if (!saveDataPath.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(saveDataPath, ec);
    const char sep = (saveDataPath.back() == '/' || saveDataPath.back() == '\\') ? '\0' : '/';
    imguiIniPath = (sep == '\0') ? (saveDataPath + kImGuiIniFilename) : (saveDataPath + sep + kImGuiIniFilename);
  } else {
    auto& gameConfig = registry_->Get<GameConfig>();
    imguiIniPath =
        gameConfig.HasLoadedConfig() ? (gameConfig.GetAssetPath() + "/" + kImGuiIniFilename) : kImGuiIniFilename;
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
  int initialWidth = gameConfig.GetDefaultWidth();
  int initialHeight = gameConfig.GetDefaultHeight();
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
    const int defW = gameConfig.HasLoadedConfig() ? gameConfig.GetDefaultWidth() : Constants::kDefaultWindowWidth;
    const int defH = gameConfig.HasLoadedConfig() ? gameConfig.GetDefaultHeight() : Constants::kDefaultWindowHeight;
    userSettings->ApplyToWindow(runtime.Window(), defW, defH);
  }
  if (runtime.SdlRenderer() != nullptr) {
    Renderer::ApplyVSync(runtime.SdlRenderer(), gameConfig.GetEngineOptions().vsync);
  }
}

bool HasProjectWindowIcon(Registry& registry, const GameConfig& gameConfig) {
  const auto* pak = registry.TryGet<AssetPak>();
  const auto iniBytes = ReadProjectIniBytes(gameConfig, pak);
  if (!iniBytes) return false;
  return !octarine::project::ProjectIni::Parse(*iniBytes).icon.empty();
}

bool ApplyProjectWindowIcon(Registry& registry, const GameConfig& gameConfig, EngineRuntime& runtime,
                            const bool headless) {
  if (headless) return false;
  const auto* pak = registry.TryGet<AssetPak>();
  const auto iniBytes = ReadProjectIniBytes(gameConfig, pak);
  if (!iniBytes) return false;
  const auto ini = octarine::project::ProjectIni::Parse(*iniBytes);
  if (ini.icon.empty()) return false;

  if (pak != nullptr && pak->IsOpen() && pak->Contains(ini.icon)) {
    if (SDL_IOStream* io = pak->OpenIO(ini.icon)) {
      size_t dataSize = 0;
      if (void* data = SDL_LoadFile_IO(io, &dataSize, true)) {
        runtime.SetWindowIcon(data, dataSize);
        SDL_free(data);
        return true;
      }
    }
  }
#if !defined(OCTARINE_SHIPPED) || defined(OCTARINE_ALLOW_LOOSE_ASSETS)
  const std::filesystem::path iconPath = std::filesystem::path(gameConfig.GetAssetPath()) / ini.icon;
  size_t dataSize = 0;
  if (void* data = SDL_LoadFile(iconPath.string().c_str(), &dataSize)) {
    runtime.SetWindowIcon(data, dataSize);
    SDL_free(data);
    return true;
  }
#endif
  return false;
}
}  // namespace

bool Game::CreateWindowAndScene(bool projectLoaded) {
  auto& gameConfig = registry_->Get<GameConfig>();
  gameConfig.windowWidth = projectLoaded ? gameConfig.GetBaseWidth() : Constants::kDefaultWindowWidth;
  gameConfig.windowHeight = projectLoaded ? gameConfig.GetBaseHeight() : Constants::kDefaultWindowHeight;
  const std::string title = projectLoaded ? gameConfig.GetGameTitle() : kDefaultWindowTitle;

  auto [initialWidth, initialHeight] = ResolveInitialWindowSize(*registry_, gameConfig, headless_);
  bool skipSavedWindowSize = false;
#ifndef OCTARINE_SHIPPED
  const bool hasSingleShotCapture = !ui_preview_capture_path_.empty() || !ui_preview_audit_json_path_.empty();
  if (ui_preview_width_ > 0 && ui_preview_height_ > 0 && (headless_ || hasSingleShotCapture)) {
    initialWidth = ui_preview_width_;
    initialHeight = ui_preview_height_;
    gameConfig.windowWidth = ui_preview_width_;
    gameConfig.windowHeight = ui_preview_height_;
    skipSavedWindowSize = true;
  }
#endif
  const bool resizable = gameConfig.GetEngineOptions().windowResizable;
  const bool hasProjectIcon = projectLoaded && HasProjectWindowIcon(*registry_, gameConfig);
  if (!runtime_.CreateWindow(title, initialWidth, initialHeight, headless_, resizable, !hasProjectIcon)) {
    return false;
  }

  if (!skipSavedWindowSize) {
    ApplyInitialWindowSettings(*registry_, gameConfig, runtime_, headless_);
  } else if (!headless_ && runtime_.SdlRenderer() != nullptr) {
    Renderer::ApplyVSync(runtime_.SdlRenderer(), gameConfig.GetEngineOptions().vsync);
  }
  if (hasProjectIcon && !ApplyProjectWindowIcon(*registry_, gameConfig, runtime_, headless_) && !headless_) {
    runtime_.ApplyDefaultWindowIcon();
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

namespace {
void StopDevAndUiBeforeTeardown(Registry& registry) {
#ifndef OCTARINE_SHIPPED
  // Stop the dev listener before the registry teardown drops it so the listener thread
  // joins cleanly while the engine is still alive.
  if (auto* devListen = registry.TryGet<octarine::dev::DevListenServer>()) {
    devListen->Stop();
  }
  if (auto* previewer = registry.TryGet<UIPreviewer>()) {
    previewer->UnmountPreview(/*restoreIsolationState=*/false);
  }
#endif
  if (auto* uiRuntime = registry.TryGet<UIRuntime>()) {
    uiRuntime->UnmountAllViews();
    uiRuntime->ClearCustomWidgets();
  }
}

void ReleaseRegistryLuaOwnedState(Registry& registry) {
  registry.ClearUserEntities();
  if (auto* inputSystem = registry.TryGet<InputSystem>()) {
    inputSystem->ResetLuaState();
  }
  if (auto* tilemap = registry.TryGet<TilemapSystem>()) {
    tilemap->Unload(registry);
    tilemap->ClearPropSpawner();
  }
  if (auto* debugReg = registry.TryGet<octarine::debug::DebugMenuRegistry>()) {
    debugReg->ClearLuaOwned();
  }
  if (auto* hotReloadTracker = registry.TryGet<octarine::LuaScriptHotReloadTracker>()) {
    *hotReloadTracker = octarine::LuaScriptHotReloadTracker{};
  }
}

void SaveImGuiIniAndUninstallHandler() {
#ifdef OCTARINE_WITH_IMGUI
  if (ImGui::GetCurrentContext() == nullptr) return;
  const ImGuiIO& io = ImGui::GetIO();
  if (io.IniFilename != nullptr && io.IniFilename[0] != '\0') {
    ImGui::SaveIniSettingsToDisk(io.IniFilename);
  }
  octarine::debug::UninstallImGuiSettingsHandler();
#endif
}

void StopAndFlushSceneLoader(SceneLoader* sceneLoader) {
  if (sceneLoader == nullptr) return;
  if (sceneLoader->IsSceneRunning()) {
    sceneLoader->StopScene();
  }
  sceneLoader->FlushPendingSceneLoad();
}

void ClearLuaAssetsAndCollectGarbage(sol::state& luaState) {
  if (luaState.lua_state() == nullptr) return;
  lua_State* L = luaState.lua_state();
  lua_pushnil(L);
  lua_setfield(L, LUA_REGISTRYINDEX, "octarine_assets");
  luaState.collect_garbage();
}

void ShutdownRendererAndRuntime(EngineRuntime& runtime, Renderer* renderer) {
  if (runtime.SdlRenderer() != nullptr && renderer != nullptr) {
    renderer->DestroyScene();
  }
  if (runtime.SdlRenderer() != nullptr || runtime.Window() != nullptr) {
    runtime.Shutdown();
  }
}

bool ShouldAbortAfterTick(const bool quitOnError, const bool headless, const uint64_t initialScriptErrorSeq,
                          const SceneLoader* sceneLoader) {
  if (quitOnError && LatestScriptErrorSeq() > initialScriptErrorSeq) {
    return true;
  }
  return sceneLoader != nullptr && sceneLoader->LastLoadFailed() && (quitOnError || headless);
}
}  // namespace

void Game::Destroy() {
  if (is_destroyed_) {
    return;
  }
  is_destroyed_ = true;

  if (registry_) {
#ifndef OCTARINE_SHIPPED
    const bool hasOneShotPreviewExport =
        (startup_mode_ == "ui-preview") && (!ui_preview_capture_path_.empty() || !ui_preview_audit_json_path_.empty() ||
                                            !ui_preview_matrix_out_dir_.empty());
#else
    constexpr bool hasOneShotPreviewExport = false;
#endif
    if (!bake_mode_ && !hasOneShotPreviewExport && (!headless_ || HasExplicitSavePathEnv())) {
      static_cast<void>(SaveUserSettings());
    }
    StopDevAndUiBeforeTeardown(*registry_);
  }

  // Phase 1 (both Registry and lua_State* live): release all scene, UI, singleton, and ECS-held
  // sol::* references, nil out LUA_REGISTRYINDEX["octarine_assets"], and run a Lua GC pass so
  // any Lua __gc finalizers execute while engine singletons are still alive.
  StopAndFlushSceneLoader(scene_loader_.get());
  if (registry_) {
    ReleaseRegistryLuaOwnedState(*registry_);
  }

  LuaSystemRegistry::clear();
  ClearLuaAssetsAndCollectGarbage(lua);
  SaveImGuiIniAndUninstallHandler();

  // Phase 2 (lua_State* still live): tear down Registry/EventBus BEFORE closing Lua and BEFORE
  // SDL_Quit so any remaining sol::* refs in engine or downstream octarine_game_module singletons
  // or systems safely call luaL_unref on a live lua_State*, and SDL-backed systems can destroy
  // their handles while SDL is still live.
  frame_loop_.reset();
  scene_loader_.reset();
  registry_.reset();
  event_bus_.reset();

  // Phase 3: close Lua now that zero C++ objects hold sol::* references into it.
  if (lua.lua_state() != nullptr) {
    lua = sol::state{};
  }

  // Free the off-screen scene target while the SDL renderer is still live (EngineRuntime owns
  // the ImGui backend + window/renderer destroy + SDL_Quit, in that order).
  ShutdownRendererAndRuntime(runtime_, renderer_.get());
}

void Game::Run() {
  if (!is_setup_) {
    Setup();
    if (has_fatal_error_) {
      s_is_running_ = false;
      return;
    }
    frame_loop_->Begin();
    is_setup_ = true;
  }

  while (s_is_running_) {
    frame_loop_->ProcessInput();
    const float deltaTime = frame_loop_->WaitTime();
    // current_tick_ counts simulation steps (not rendered frames), so sub-stepped fast-forward
    // and paused frames are accounted exactly.
    current_tick_ += static_cast<uint64_t>(frame_loop_->Update(deltaTime));
    if (ShouldAbortAfterTick(quit_on_error_, headless_, initial_script_error_seq_, scene_loader_.get())) {
      has_fatal_error_ = true;
      s_is_running_ = false;
      break;
    }
    frame_loop_->Render(deltaTime);
  }
}

void Game::Step(const float deltaTime) { StepTicks(1, deltaTime); }

void Game::StepTicks(const uint32_t count, const float deltaTime) {
  if (!is_setup_) {
    Setup();
    if (has_fatal_error_) {
      s_is_running_ = false;
      return;
    }
    frame_loop_->Begin();
    is_setup_ = true;
  }
  if (count == 0) {
    return;
  }

  // Each requested tick runs one FrameLoop::Update, which itself may execute simSubSteps steps.
  for (uint32_t i = 0; i < count && s_is_running_; ++i) {
    frame_loop_->ProcessInput();
    if (auto* cfg = registry_ ? registry_->TryGet<GameConfig>() : nullptr) {
      cfg->GetEngineOptions().stepFrame = true;
    }
    current_tick_ += static_cast<uint64_t>(frame_loop_->Update(deltaTime));
    if (ShouldAbortAfterTick(quit_on_error_, headless_, initial_script_error_seq_, scene_loader_.get())) {
      has_fatal_error_ = true;
      s_is_running_ = false;
    }
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

void InstallUIButtonSystem(Registry& registry, const std::unique_ptr<EventBus>& eventBus) {
  auto& uiButtonSystem = registry.Set<UIButtonSystem>(UIButtonSystem());
  uiButtonSystem.Init(&registry, eventBus);
}

void RegisterEventAndHotReloadSingletons(Registry& registry, const std::unique_ptr<EventBus>& eventBus) {
  auto& scriptCollisionSystem = registry.Set<ScriptCollisionSystem>(ScriptCollisionSystem());
  scriptCollisionSystem.Init(&registry, eventBus);
  game_module::RegisterEventSystems(registry, eventBus);
  registry.Set<ScriptHotReload>(ScriptHotReload());
  registry.Set<AssetHotReload>(AssetHotReload());
}

#ifndef OCTARINE_SHIPPED
void ConfigureOneShotPreviewOutput(FrameLoop* frameLoop, const Game::UiPreviewCliOptions& previewOpts) {
  if (frameLoop == nullptr) return;
  if (!previewOpts.capturePath.empty() || !previewOpts.auditJsonPath.empty()) {
    frameLoop->ConfigureOneShotCapture(previewOpts.capturePath, previewOpts.auditJsonPath, /*captureFrame=*/2);
  }
  if (!previewOpts.matrixOutDir.empty()) {
    const std::string effectivePresets =
        (!previewOpts.presetsCsv.empty() || previewOpts.preset.empty()) ? previewOpts.presetsCsv : previewOpts.preset;
    frameLoop->ConfigureOneShotMatrix(previewOpts.matrixOutDir, previewOpts.resolutionsCsv, effectivePresets,
                                      /*captureFrame=*/2);
  }
}

void InitializeUiPreviewSession(Registry& registry, GameConfig& gameConfig, FrameLoop* frameLoop, const bool headless,
                                const Game::UiPreviewCliOptions& previewOpts) {
  auto& previewer = registry.Get<UIPreviewer>();
  previewer.SetRegistry(&registry);
  previewer.RefreshCatalogViews();
  previewer.SetBackdropMode(PreviewBackdropMode::DarkSlate);
  previewer.SetIsolatedMode(true, /*managePauseState=*/false);

  if (previewOpts.width > 0 && previewOpts.height > 0) {
    previewer.SetSimulatedViewport(previewOpts.width, previewOpts.height);
  }

  auto& options = gameConfig.GetEngineOptions();
  options.hotReloadEnabled = true;

  const bool oneShotExport =
      !previewOpts.capturePath.empty() || !previewOpts.auditJsonPath.empty() || !previewOpts.matrixOutDir.empty();
  if (!headless && !oneShotExport) {
    options.showDebugGUI = true;
    if (auto* debugReg = registry.TryGet<octarine::debug::DebugMenuRegistry>()) {
      debugReg->ApplyPersistedOpenState("engine.ui_previewer", true);
    }
  }

  const std::string resolvedTarget =
      !previewOpts.file.empty()
          ? previewOpts.file
          : (!previewer.CatalogViews().empty() ? previewer.CatalogViews().front().fullPath : std::string());
  if (!resolvedTarget.empty()) {
    static_cast<void>(previewer.MountPreviewWithMock(resolvedTarget, previewOpts.mockScript, previewOpts.preset));
  }

  ConfigureOneShotPreviewOutput(frameLoop, previewOpts);
}

bool ParsePreviewDimensions(const std::string& spec, int& outW, int& outH) {
  const auto sep = spec.find_first_of("xX");
  if (sep == std::string::npos || sep == 0 || sep + 1 >= spec.size()) return false;
  auto parseBounded = [](std::string_view sv, int& out) -> bool {
    int n = 0;
    for (const char c : sv) {
      if (std::isdigit(static_cast<unsigned char>(c)) == 0 || n > 100000) return false;
      n = n * 10 + (c - '0');
    }
    if (n <= 0 || n > 8192) return false;
    out = n;
    return true;
  };
  return parseBounded(std::string_view(spec).substr(0, sep), outW) &&
         parseBounded(std::string_view(spec).substr(sep + 1), outH);
}
#endif

void ApplyUiPreviewOptionValue([[maybe_unused]] const std::string& arg, [[maybe_unused]] const std::string& val,
                               [[maybe_unused]] Game::UiPreviewCliOptions& out, bool& parseError) {
#ifdef OCTARINE_SHIPPED
  Logger::Warn(arg + " is stripped from shipped builds; ignoring.");
  parseError = false;
#else
  if (arg == "--ui-file") {
    out.file = val;
  } else if (arg == "--ui-mock") {
    out.mockScript = val;
  } else if (arg == "--ui-preset") {
    out.preset = val;
  } else if (arg == "--ui-size") {
    if (!ParsePreviewDimensions(val, out.width, out.height)) {
      Logger::Error("Error: --ui-size requires <Width>x<Height> (got '" + val + "').");
      parseError = true;
    }
  } else if (arg == "--ui-capture") {
    out.capturePath = val;
  } else if (arg == "--ui-audit-json") {
    out.auditJsonPath = val;
  } else if (arg == "--ui-matrix") {
    out.matrixOutDir = val;
  } else if (arg == "--ui-resolutions") {
    out.resolutionsCsv = val;
  } else if (arg == "--ui-presets") {
    out.presetsCsv = val;
  }
#endif
}

}  // namespace

bool Game::TryConsumeUiPreviewFlag(const std::string& arg, const std::string& nextVal, const bool hasNextVal,
                                   UiPreviewCliOptions& out, bool& consumedNext, bool& parseError) {
  consumedNext = false;
  parseError = false;
  const bool isPreviewFlag = (arg == "--ui-file" || arg == "--ui-mock" || arg == "--ui-preset" || arg == "--ui-size" ||
                              arg == "--ui-capture" || arg == "--ui-audit-json" || arg == "--ui-matrix" ||
                              arg == "--ui-resolutions" || arg == "--ui-presets");
  if (!isPreviewFlag) return false;

  if (!hasNextVal) {
    Logger::Error("Error: " + arg + " flag requires an argument.");
    parseError = true;
    return true;
  }
  consumedNext = true;
  ApplyUiPreviewOptionValue(arg, nextVal, out, parseError);
  return true;
}

#ifndef OCTARINE_SHIPPED
void Game::SetUiPreviewOptions(const UiPreviewCliOptions& cliOpts) {
  ui_preview_file_ = cliOpts.file;
  ui_preview_mock_ = cliOpts.mockScript;
  ui_preview_preset_ = cliOpts.preset;
  ui_preview_width_ = cliOpts.width;
  ui_preview_height_ = cliOpts.height;
  ui_preview_capture_path_ = cliOpts.capturePath;
  ui_preview_audit_json_path_ = cliOpts.auditJsonPath;
  ui_preview_matrix_out_dir_ = cliOpts.matrixOutDir;
  ui_preview_resolutions_csv_ = cliOpts.resolutionsCsv;
  ui_preview_presets_csv_ = cliOpts.presetsCsv;
}

void Game::SetUiPreviewOptions(const std::string& file, const std::string& mockScript, const int width,
                               const int height, const std::string& capturePath, const std::string& auditJsonPath) {
  UiPreviewCliOptions cliOpts;
  cliOpts.file = file;
  cliOpts.mockScript = mockScript;
  cliOpts.width = width;
  cliOpts.height = height;
  cliOpts.capturePath = capturePath;
  cliOpts.auditJsonPath = auditJsonPath;
  SetUiPreviewOptions(cliOpts);
}

bool Game::CaptureFrameToFile(const std::string& path) {
  return frame_loop_ != nullptr && frame_loop_->RenderAndCaptureToFile(path);
}
#endif

SystemId Game::SetupLuaBindingsAndCatalog(GameConfig& gameConfig) {
  engine_bootstrap::InstallCoreSingletons(*registry_, registry_->Get<EngineContext>(), gameConfig.windowWidth,
                                          gameConfig.windowHeight, /*withFramePathCaches=*/true, &lua);

  auto scriptSystem = registry_->RegisterSystem<ScriptComponent>(ScriptSystem());
  auto& inputSystem = engine_bootstrap::InstallInputSystem(*registry_, event_bus_);
  InstallUIButtonSystem(*registry_, event_bus_);
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

  if (IsBenchMode()) {
    ApplyBenchModeOverlayOverrides(*registry_, gameConfig, startup_mode_);
  }

  auto& assetManager = registry_->Get<AssetManager>();
  if (gameConfig.HasLoadedConfig()) {
    SetupAssetCatalogAndPak(*registry_, assetManager, lua, gameConfig, use_manifest_);
  }

#ifndef OCTARINE_SHIPPED
  if (!LuaApiManifest::MaybeDumpFromEnv(lua, preBindingGlobals) && dev_mode_) {
    LuaApiManifest::WriteProjectStub(lua, preBindingGlobals, gameConfig.GetAssetPath());
  }
#endif
  return scriptSystem.Id();
}

void Game::RegisterRenderAndAudioSystems(const SystemId scriptSystemId) {
  auto audioSystem = registry_->RegisterSystem<AudioSourceComponent>(AudioSystem());
  if (!audioSystem.Func().Init(registry_.get(), event_bus_)) {
    Logger::Error("AudioSystem failed to initialize; audio disabled this session.");
  }
  registry_->Get<EngineContext>().mixer = audioSystem.Func().Mixer();

  auto& gameConfig = registry_->Get<GameConfig>();
  auto& assetManager = registry_->Get<AssetManager>();
#ifndef OCTARINE_SHIPPED
  auto& installedPreviewer = registry_->Set<UIPreviewer>(UIPreviewer(registry_.get()));
  installedPreviewer.SetCaptureCallback([this](const std::string& path) { return CaptureFrameToFile(path); });
  const bool isUiPreviewMode = (startup_mode_ == "ui-preview");
#else
  constexpr bool isUiPreviewMode = false;
#endif
  if (gameConfig.HasLoadedConfig() && !isUiPreviewMode) {
    if (!LoadGame(lua, assetManager, gameConfig) || (scene_loader_ && scene_loader_->LastLoadFailed()) ||
        (quit_on_error_ && LatestScriptErrorSeq() > initial_script_error_seq_)) {
      has_fatal_error_ = true;
      s_is_running_ = false;
    }
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

  // UI layout pass resolves UIRectComponent after TransformSystem and before UI render systems.
  auto uiLayout = registry_->RegisterBulkSystem(UILayoutSystem());
  registry_->Order(uiLayout).After(transform);

  auto tilemapRender = registry_->RegisterBulkSystem(TilemapRenderSystem());
  auto renderSprite =
      registry_->RegisterParallelSystem<GlobalTransformComponent, SpriteComponent>(RenderSpriteSystem());
  registry_->Order(tilemapRender).After(cameraFollow).Before(renderSprite);
  registry_->Order(renderSprite).After(transform).After(cameraFollow);

  auto renderUISprite = registry_->RegisterSystem<UIRectComponent, SpriteComponent>(RenderUISpriteSystem());
  auto renderUIPrimitive = registry_->RegisterBulkSystem(RenderUIPrimitiveSystem());
  auto renderText = registry_->RegisterSystem<TextLabelComponent>(RenderTextSystem());
  for (const auto uiSysId : {renderUISprite.Id(), renderUIPrimitive.Id(), renderText.Id()}) {
    registry_->Order(uiSysId).After(uiLayout);
  }

  auto renderSquare =
      registry_->RegisterParallelSystem<SquarePrimitiveComponent, GlobalTransformComponent>(RenderPrimitiveSystem());
  auto renderCircle = registry_->RegisterParallelSystem<CirclePrimitiveComponent, GlobalTransformComponent>(
      RenderCirclePrimitiveSystem());
  auto renderLine =
      registry_->RegisterParallelSystem<LinePrimitiveComponent, GlobalTransformComponent>(RenderLinePrimitiveSystem());
  auto renderTriangle = registry_->RegisterParallelSystem<TrianglePrimitiveComponent, GlobalTransformComponent>(
      RenderTrianglePrimitiveSystem());
  auto renderCapsule = registry_->RegisterParallelSystem<CapsulePrimitiveComponent, GlobalTransformComponent>(
      RenderCapsulePrimitiveSystem());
  auto renderColorGrid =
      registry_->RegisterParallelSystem<ColorGridComponent, GlobalTransformComponent>(RenderColorGridSystem());
  for (const auto sysId : {renderSquare.Id(), renderCircle.Id(), renderLine.Id(), renderTriangle.Id(),
                           renderCapsule.Id(), renderColorGrid.Id()}) {
    registry_->Order(sysId).After(transform).After(cameraFollow);
  }

  registry_->Order(listenerTransform).After(transform);
  registry_->Order(audioCulling).After(transform).After(listenerTransform).Before(audioSystem);
  registry_->Order(audioSystem).After(listenerTransform).After(audioCulling);
  registry_->Order(spatialAudio).After(transform).After(listenerTransform).After(audioCulling).After(audioSystem);
  registry_->Order(doppler).After(transform).After(listenerTransform).After(audioCulling).After(audioSystem);
  registry_->Order(cameraFollow).After(transform).After(scriptSystemId);

  if (renderer_ != nullptr) {
    game_module::InvokeRegisterRenderPasses(*registry_, *renderer_);
  }
}

void Game::Setup() {
  auto& gameConfig = registry_->Get<GameConfig>();
  const SystemId scriptSystemId = SetupLuaBindingsAndCatalog(gameConfig);
  RegisterRenderAndAudioSystems(scriptSystemId);
  RegisterEventAndHotReloadSingletons(*registry_, event_bus_);

#ifndef OCTARINE_SHIPPED
  if (startup_mode_ == "ui-preview") {
    UiPreviewCliOptions sessionOpts;
    sessionOpts.file = ui_preview_file_;
    sessionOpts.mockScript = ui_preview_mock_;
    sessionOpts.preset = ui_preview_preset_;
    sessionOpts.width = ui_preview_width_;
    sessionOpts.height = ui_preview_height_;
    sessionOpts.capturePath = ui_preview_capture_path_;
    sessionOpts.auditJsonPath = ui_preview_audit_json_path_;
    sessionOpts.matrixOutDir = ui_preview_matrix_out_dir_;
    sessionOpts.resolutionsCsv = ui_preview_resolutions_csv_;
    sessionOpts.presetsCsv = ui_preview_presets_csv_;
    InitializeUiPreviewSession(*registry_, gameConfig, frame_loop_.get(), headless_, sessionOpts);
  }
#endif
}

void Game::ReturnFocusToGame() {
#ifdef OCTARINE_WITH_IMGUI
  RenderDebugGUISystem::ReturnFocusToGame(GetRegistry());
#endif
}
