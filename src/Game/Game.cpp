#include "Game.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

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
#include "AssetManager/AtlasBaker.h"
#include "AssetManager/AudioNormalizer.h"
#include "AssetManager/SceneAssetScanner.h"
#include "AssetManager/TextureAtlasBaker.h"
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
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Engine/EditorBootstrap.h"
#include "Engine/EngineBootstrap.h"
#include "Engine/LuaProtect.h"
#include "Engine/Platform/PlatformPaths.h"
#include "Engine/SdlFileReader.h"
#include "Engine/SimulationSystems.h"
#include "Events/KeyInputEvent.h"
#include "Events/MouseInputEvent.h"
#include "Events/MouseWheelEvent.h"
#include "Game/UserSettings.h"
#include "GameConfig.h"
#include "General/Logger.h"
#include "General/PerfUtils.h"
#include "General/Rect.h"
#include "Project/ProjectIni.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/Renderer.h"
#include "Renderer/SpriteRenderCache.h"
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
constexpr const char* kDefaultEditorWindowTitle = "Octarine Engine - Editor";
#ifdef OCTARINE_WITH_IMGUI
constexpr const char* kImGuiIniFilename = "imgui.ini";
constexpr const char* kImGuiEditorIniFilename = "imgui_editor.ini";
#endif
constexpr const char* kStartupModeBake = "bake";
constexpr const char* kStartupModeEditor = "editor";
constexpr const char* kAtlasesDirName = "atlases";
constexpr const char* kAtlasPngSuffix = ".atlas.png";
constexpr const char* kAtlasLuaSuffix = ".atlas.lua";
constexpr const char* kNormalizedDirName = "normalized";
constexpr const char* kScriptsDirName = "scripts";
constexpr const char* kLuaExtension = ".lua";

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
  opts.showPerfOverlay = false;
  opts.hotReloadEnabled = false;
}

void InitUserSettings(Registry& registry, GameConfig& gameConfig, const std::string& saveDataPath) {
  octarine::UserSettings userSettings;
  if (!saveDataPath.empty()) {
    static_cast<void>(userSettings.Load(saveDataPath));
  }
  gameConfig.GetEngineOptions().masterVolume = userSettings.masterVolume;
  gameConfig.GetEngineOptions().audioEnabled = !userSettings.audioMuted;
  if (userSettings.fpsTarget.has_value()) {
    gameConfig.GetEngineOptions().fpsTarget = *userSettings.fpsTarget;
  }
  if (userSettings.showPerfOverlay.has_value()) {
    gameConfig.GetEngineOptions().showPerfOverlay = *userSettings.showPerfOverlay;
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
      engine_bootstrap::editor::OnProjectLoaded(registry, effectivePath);
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
  // Editor builds Set their editor singletons (persistence/launcher/export) and may default
  // effectivePath to the last-opened project here; player builds no-op. See EditorBootstrap.
  engine_bootstrap::editor::InstallSingletons(*registry_, effectivePath);

  // With no explicit path (and, in editor builds, no remembered project) default the asset base to
  // the executable/bundle dir on desktop; Android keeps it empty so assets resolve against the APK
  // asset root. The Android/desktop fork lives in PlatformPaths, not here.
  engine_bootstrap::platform::ApplyDefaultBasePath(effectivePath);

  const bool projectLoaded = LoadProjectConfig(*registry_, gameConfig, effectivePath, use_manifest_);

  InitUserSettings(*registry_, gameConfig, GetSaveDataPath());

  // Editor builds default into editor mode (unless a non-editor mode was requested); player builds
  // never do and reject `--startup-mode editor` (ok=false → abort).
  const auto startupDecision = engine_bootstrap::editor::DecideStartupMode(startup_mode_);
  if (!startupDecision.ok) {
    return false;
  }

  if (!projectLoaded && !startupDecision.defaultToEditor) {
    Logger::Error("Failed to load project from path: '" + effectivePath + "'. A valid project directory containing " +
                  GameConfig::kConfigFileName + " is required.");
    return false;
  }
  if (startupDecision.defaultToEditor) {
    Logger::Info("Starting in Editor Mode.");
    gameConfig.SetIsEditorMode(true);
    engine_bootstrap::editor::ApplyAudioPrefs(*registry_);
    engine_bootstrap::editor::PauseForEditorSession(*registry_);
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
  // io.IniFilename holds the raw pointer for the context's lifetime, so the backing string must
  // outlive InitImGui — keep it static.
  static std::string imguiIniPath;
  if (gameConfig.HasLoadedConfig()) {
    imguiIniPath = gameConfig.GetAssetPath() + "/" + kImGuiIniFilename;
  } else if (char* prefPath = SDL_GetPrefPath(kDefaultOrgName, kDefaultAppName)) {
    imguiIniPath = std::string(prefPath) + kImGuiEditorIniFilename;
    SDL_free(prefPath);
  } else {
    imguiIniPath = kImGuiEditorIniFilename;
  }
  runtime_.InitImGui(imguiIniPath.c_str());

  // Editor: bundled Roboto at the persisted size + theme + the default layout on first run.
  // Player-with-ImGui: the built-in default font. The editor/player fork lives in EditorBootstrap.
  engine_bootstrap::editor::SetupEditorImGui(*registry_);
#endif
}

void Game::StartDevListenServer() {
#ifndef OCTARINE_SHIPPED
  // DevListenServer: TCP listener on 127.0.0.1:<port> when --dev-listen given. Compile-stripped
  // from shipped builds. Available in editor + player presets alike so the standalone player can
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
  if (!headless && !gameConfig.IsEditorMode()) {
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
}  // namespace

bool Game::CreateWindowAndScene(bool projectLoaded) {
  auto& gameConfig = registry_->Get<GameConfig>();
  gameConfig.windowWidth = projectLoaded ? gameConfig.GetDefaultWidth() : Constants::kDefaultWindowWidth;
  gameConfig.windowHeight = projectLoaded ? gameConfig.GetDefaultHeight() : Constants::kDefaultWindowHeight;
  const std::string title = projectLoaded
                                ? gameConfig.GetGameTitle()
                                : (gameConfig.IsEditorMode() ? kDefaultEditorWindowTitle : kDefaultWindowTitle);

  const auto [initialWidth, initialHeight] = ResolveInitialWindowSize(*registry_, gameConfig, headless_);
  if (!runtime_.CreateWindow(title, initialWidth, initialHeight, headless_)) {
    return false;
  }

  if (!headless_ && !gameConfig.IsEditorMode()) {
    if (auto* userSettings = registry_->TryGet<octarine::UserSettings>()) {
      userSettings->ApplyToWindow(runtime_.Window(), gameConfig.windowWidth, gameConfig.windowHeight);
    }
  }

  return renderer_->CreateScene(runtime_.SdlRenderer(), gameConfig.windowWidth, gameConfig.windowHeight);
}

bool Game::IsEditorMode() const {
  const auto* config = registry_->TryGet<GameConfig>();
  return config != nullptr && config->IsEditorMode();
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
  if (!isHeadless && !IsEditorMode()) {
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

    // GameConfig is only Set once Initialize gets past subsystem init; a failed Initialize
    // (e.g. SDL_Init with no video device) tears down here with the singleton absent, so guard
    // against it rather than throwing out of Destroy.
    if (registry_->TryGet<GameConfig>() != nullptr) {
      // Persist editor prefs (audio + per-project layout); no-op in player builds.
      engine_bootstrap::editor::SaveOnShutdown(*registry_);
    }
  }

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

bool Game::Bake(const std::string& assetPath, const std::string& scriptsOverrideDir) {
  if (assetPath.empty()) {
    Logger::Error("Bake: no project path provided.");
    return false;
  }

  // Minimal init: GameConfig::LoadConfigFromFile and the engine's file reads (SDL_IOFromFile /
  // SDL_LoadFile) expect the library to be initialized. No video/audio subsystem is needed —
  // the bake validates references and paths only, with no GPU/mixer upload.
  if (!SDL_Init(0)) {
    Logger::Error("Bake: SDL_Init failed: " + std::string(SDL_GetError()));
    return false;
  }

  bool ok = false;
  {
    Game game;  // SDL-free constructor — allocates Registry/EventBus/Renderer, opens no window.
    game.bake_mode_ = true;
    game.startup_mode_ = kStartupModeBake;
    ok = game.RunBakeValidation(assetPath, scriptsOverrideDir);
  }

  SDL_Quit();
  return ok;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
bool Game::RunBakeValidation(const std::string& assetPath, const std::string& scriptsOverrideDir) {
  registry_->Set<GameConfig>(GameConfig());
  auto& gameConfig = registry_->Get<GameConfig>();
  if (!gameConfig.LoadConfigFromFile(assetPath)) {
    Logger::Error("Bake: failed to load config from " + assetPath);
    return false;
  }

  // Bake mode: no window/renderer/mixer. Set the engine context first with the bits that do
  // exist; InstallCoreSingletons plumbs ctx.assets once it Sets the AssetManager.
  EngineContext bakeCtx;
  bakeCtx.eventBus = event_bus_.get();
  bakeCtx.config = &gameConfig;
  registry_->Set<EngineContext>(bakeCtx);

  // Shared bootstrap spine. withFramePathCaches=false — bake runs no frames and no audio systems,
  // so the SpriteRenderCache / AudioTrackCache slots are unused.
  engine_bootstrap::InstallCoreSingletons(*registry_, registry_->Get<EngineContext>(), gameConfig.windowWidth,
                                          gameConfig.windowHeight, /*withFramePathCaches=*/false, &lua);
  engine_bootstrap::InstallPoolAndSingletons(*registry_);
  auto& inputSystem = engine_bootstrap::InstallInputSystem(*registry_, event_bus_);
  engine_bootstrap::RegisterAllComponentBindings();
  LuaSystemRegistry::clear();
  LuaSystemRegistry::registerSystem(inputSystem);
  engine_bootstrap::InstallLuaLibraries(lua);
  // Bake doesn't register ScriptSystem as a per-frame system (no frames). A stack instance
  // is enough to install the primitive + component usertypes Lua scripts reference.
  ScriptSystem scriptSystem;
  scriptSystem.CreateLuaBindings(lua);
  engine_bootstrap::InstallLuaModules(lua, *this);
  LuaSystemRegistry::bindAll(lua);
  engine_bootstrap::SetCommonLuaGlobals(lua, gameConfig, startup_mode_);

  auto& assetManager = registry_->Get<AssetManager>();
  assetManager.LoadGameConfig(gameConfig);

  // Always re-derive the catalog from the real files — never round-trip an existing (possibly
  // stale) manifest sitting in the project dir.
  if (!assetManager.GetCatalog().ScanFilesystem(assetManager.GetBasePath(), lua, gameConfig)) {
    Logger::Error("Bake: catalog scan failed (id collisions). Aborting.");
    return false;
  }
  assetManager.GetCatalog().InstallLuaIdTable(lua);

  // Run the startup script. Its scene loads call acquire_scene_assets / load_asset, which in bake
  // mode validate every referenced id against the catalog and tally failures on this Game instead
  // of uploading to a GPU. This is the CI gate: a typo'd or missing asset reference fails the bake.
  LoadGame(lua, assetManager, gameConfig);

  // Pack atlases declared by `meta.atlas = <group>` sidecars into <project>/_atlases/<group>.png
  // and mutate member entries to record their slice within each. Runs after scene load so a
  // future `meta.atlas` injection from scene scripts could also be honored. Members tagged
  // `meta.no_atlas = true` skip the pack and ship as loose textures.
  if (!TextureAtlasBaker{}.Run(assetManager.GetCatalog(), assetPath)) {
    Logger::Error("Bake: atlas packing failed. Aborting.");
    return false;
  }

  const std::string manifestPath = (std::filesystem::path(assetPath) / AssetCatalog::kManifestFilename).string();
  const bool wrote = assetManager.GetCatalog().WriteManifest(manifestPath, assetPath);
  if (wrote) {
    Logger::Info("Bake: wrote " + std::to_string(assetManager.GetCatalog().Size()) + " entries to " + manifestPath);
  }

  // Glyph atlas pass: for every Font catalog entry, rasterize ASCII printable
  // into a packed PNG + Lua metrics sidecar under `<basePath>/atlases/<asset_id>.atlas.{png,lua}`.
  // Runtime probes for those alongside the .ttf and renders from the atlas when present, falling
  // back to TTF_RenderText_Blended when missing. TTF needs explicit init in bake (Bake() opens
  // SDL_Init(0) only — no video/audio subsystems).
  std::vector<std::string> atlasFiles;
  if (wrote && TTF_Init()) {
    const std::filesystem::path atlasDir = std::filesystem::path(assetPath) / kAtlasesDirName;
    std::error_code ec;
    std::filesystem::create_directories(atlasDir, ec);
    const auto codepoints = AtlasBaker::DefaultAsciiPrintable();
    for (const auto& [id, entry] : assetManager.GetCatalog().Entries()) {
      if (entry.type != AssetType::Font) continue;
      TTF_Font* font = TTF_OpenFont(entry.fullPath.c_str(), entry.fontSize);
      if (font == nullptr) {
        Logger::Warn("Bake: TTF_OpenFont failed for " + entry.fullPath + " @ " + std::to_string(entry.fontSize) +
                     " — skipping atlas.");
        continue;
      }
      const std::string pngPath = (atlasDir / (id + kAtlasPngSuffix)).string();
      const std::string luaPath = (atlasDir / (id + kAtlasLuaSuffix)).string();
      if (AtlasBaker::Bake(font, entry.fontSize, codepoints, pngPath, luaPath)) {
        atlasFiles.push_back(pngPath);
        atlasFiles.push_back(luaPath);
      }
      TTF_CloseFont(font);
    }
    TTF_Quit();
  }

  // Audio normalize pass: for every Audio catalog entry with `meta.normalize=true`,
  // run BS.1770 integrated loudness measurement + apply the gain to land at -16 LUFS, writing
  // the normalized WAV to `<basePath>/normalized/<rel>.wav`. The pak override map then routes
  // the catalog's original relPath to read bytes from the normalized variant — so shipped
  // builds carry the loudness-aligned audio while dev tree (and dev runs) stay untouched.
  std::map<std::string, std::string> audioOverrides;
  if (wrote) {
    const std::filesystem::path normRoot = std::filesystem::path(assetPath) / kNormalizedDirName;
    for (const auto& [id, entry] : assetManager.GetCatalog().Entries()) {
      if (entry.type != AssetType::Audio || !entry.normalize) continue;
      const std::filesystem::path src(entry.fullPath);
      const std::filesystem::path rel = std::filesystem::relative(src, std::filesystem::path(assetPath));
      const std::filesystem::path dst = normRoot / rel;
      std::error_code ec;
      std::filesystem::create_directories(dst.parent_path(), ec);
      if (AudioNormalizer::NormalizeWav(entry.fullPath, dst.string())) {
        audioOverrides[rel.generic_string()] = dst.string();
      }
    }
  }

  // Pack every cataloged asset (+ any derived atlas files, Lua scripts, w/ optional normalized-audio
  // and script overrides) into a single asset_bundle.pak alongside the manifest. Shipped builds
  // (OCTARINE_SHIPPED) open this in place of the loose asset tree; dev builds ignore it.
  // Manifest write is the source-of-truth gate — if that failed, skip the pak so a stale
  // archive doesn't ship.
  if (wrote) {
    std::vector<std::string> extraFiles = atlasFiles;
    std::map<std::string, std::string> pakOverrides = audioOverrides;

    // Collect Lua scripts: root *.lua (including asset_manifest.lua) and scripts/**/*.lua.
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(assetPath, ec)) {
      if (entry.is_regular_file(ec) && entry.path().extension() == kLuaExtension) {
        extraFiles.push_back(entry.path().lexically_normal().string());
      }
    }
    const std::filesystem::path scriptsDir = std::filesystem::path(assetPath) / kScriptsDirName;
    if (std::filesystem::is_directory(scriptsDir, ec)) {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(scriptsDir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == kLuaExtension) {
          extraFiles.push_back(entry.path().lexically_normal().string());
        }
      }
    }

    // Embed config.ini and project.ini into the pak so shipped builds need no loose config files.
    const std::filesystem::path configFile = std::filesystem::path(assetPath) / GameConfig::kConfigFileName;
    if (std::filesystem::is_regular_file(configFile, ec)) {
      extraFiles.push_back(configFile.lexically_normal().string());
    }
    const std::filesystem::path projectIniFile =
        std::filesystem::path(assetPath) / octarine::project::ProjectIni::kFilename;
    if (std::filesystem::is_regular_file(projectIniFile, ec)) {
      extraFiles.push_back(projectIniFile.lexically_normal().string());
    }

    // If an external scripts directory is provided (e.g. pre-compiled bytecode staged by CMake),
    // map the project-relative script paths to their compiled override locations.
    if (!scriptsOverrideDir.empty()) {
      const std::filesystem::path overrideRoot(scriptsOverrideDir);
      for (const auto& file : extraFiles) {
        if (std::filesystem::path(file).extension() == kLuaExtension) {
          const std::filesystem::path rel = std::filesystem::relative(file, assetPath, ec);
          if (!ec && !rel.empty()) {
            const std::filesystem::path staged = overrideRoot / rel;
            if (std::filesystem::exists(staged, ec)) {
              pakOverrides[rel.generic_string()] = staged.string();
            }
          }
        }
      }
    }

    const std::string pakPath = (std::filesystem::path(assetPath) / AssetPak::kDefaultFilename).string();
    if (!AssetPak::Pack(assetManager.GetCatalog(), pakPath, assetPath, extraFiles, pakOverrides)) {
      Logger::Error("Bake: AssetPak::Pack failed for " + pakPath);
      return false;
    }
  }

  if (bake_validation_failures_ > 0) {
    Logger::Error("Bake: " + std::to_string(bake_validation_failures_) +
                  " unresolved asset reference(s) across scenes. Build is broken.");
    return false;
  }
  return wrote;
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

void Game::Setup() {
  auto& gameConfig = registry_->Get<GameConfig>();

  // Shared bootstrap spine (see engine_bootstrap/EngineBootstrap.h). withFramePathCaches=true —
  // the live game loop reads SpriteRenderCache from the sprite-render pass and AudioTrackCache
  // from the spatial/Doppler/culling audio systems.
  engine_bootstrap::InstallCoreSingletons(*registry_, registry_->Get<EngineContext>(), gameConfig.windowWidth,
                                          gameConfig.windowHeight, /*withFramePathCaches=*/true, &lua);

  // ScriptSystem is registered as a per-frame system (vs. Bake's stack instance) so its
  // operator() runs each tick. CreateLuaBindings still happens below in the bootstrap spine.
  auto scriptSystem = registry_->RegisterSystem<ScriptComponent>(ScriptSystem());

  auto& inputSystem = engine_bootstrap::InstallInputSystem(*registry_, event_bus_);
  engine_bootstrap::RegisterAllComponentBindings();
  LuaSystemRegistry::clear();
  LuaSystemRegistry::registerSystem(inputSystem);
  engine_bootstrap::InstallLuaLibraries(lua);
#ifndef OCTARINE_SHIPPED
  // Snapshot stdlib globals before binding so the API manifest can diff out everything the
  // engine adds. Cheap; the actual dump below only fires in editor mode or via env override.
  const auto preBindingGlobals = LuaApiManifest::SnapshotGlobals(lua);
#endif
  engine_bootstrap::InstallPoolAndSingletons(*registry_);
  scriptSystem.Func().CreateLuaBindings(lua);
  engine_bootstrap::InstallLuaModules(lua, *this);
  LuaSystemRegistry::bindAll(lua);
  engine_bootstrap::SetCommonLuaGlobals(lua, gameConfig, startup_mode_);

#ifndef OCTARINE_SHIPPED
  // Emit the EmmyLua API stub once per Setup, after the full surface — globals, modules,
  // system tables — is installed. OCTARINE_DUMP_LUA_API overrides the destination; otherwise
  // editor sessions refresh <project>/types/octarine_api.lua so IDE autocomplete (LuaLS
  // workspace.library) tracks the live engine surface. Shipped builds compile this out.
  if (!LuaApiManifest::MaybeDumpFromEnv(lua, preBindingGlobals) &&
      (gameConfig.IsEditorMode() || startup_mode_ == kStartupModeEditor)) {
    LuaApiManifest::WriteProjectStub(lua, preBindingGlobals, gameConfig.GetAssetPath());
  }
#endif

  if (startup_mode_ == kStartupModeEditor) {
    gameConfig.SetIsEditorMode(true);
  }

  if (IsBenchMode() && startup_mode_ != kStartupModeEditor) {
    // Bench runs shouldn't pay for debug overlays. Force-zero every toggle that would add per-frame
    // work. These EngineOptions fields exist in every build (and read as no-ops without ImGui); the
    // editor-only window toggles move into EditorBootstrap (no-op in player builds).
    auto& options = gameConfig.GetEngineOptions();
    options.showDebugGUI = false;
    options.showFpsCounter = false;
    options.showEntityInfo = false;
    options.showImGuiDemoWindow = false;
    options.drawColliders = false;
    engine_bootstrap::editor::DisableBenchOverlays(*registry_);
  }

  auto& assetManager = registry_->Get<AssetManager>();
  if (gameConfig.HasLoadedConfig()) {
    SetupAssetCatalogAndPak(*registry_, assetManager, lua, gameConfig, use_manifest_);
  }

  // Audio must be live before LoadGame: the startup script may call load_asset for audio_clip
  // entries, which need the mixer. RegisterSystem owns the single AudioSystem instance — Init
  // runs against that instance so the EventBus subscription, cmd buffer, and per-frame Update
  // all share state.
  auto audioSystem = registry_->RegisterSystem<AudioSourceComponent>(AudioSystem());
  if (!audioSystem.Func().Init(registry_.get(), event_bus_)) {
    Logger::Error("AudioSystem failed to initialize; audio disabled this session.");
  }
  // Mixer is owned by AudioSystem; publish it on the context so asset loads + the master-
  // volume sync (in Game::Update) can reach it without going through the Registry's typed slot.
  registry_->Get<EngineContext>().mixer = audioSystem.Func().Mixer();

  if (gameConfig.HasLoadedConfig()) {
    LoadGame(lua, assetManager, gameConfig);
  }

  // Gameplay simulation tier (projectiles, steering, separation, combat, integration, transforms,
  // collision, lifetime) and its internal ordering edges. Shared with the headless benchmark
  // harness so the benchmarked frame is the shipped frame.
  const auto simulation = engine_bootstrap::RegisterSimulationSystems(*registry_, event_bus_);
  const auto& transform = simulation.transform;

  // Spatial audio: snapshot the listener entity (UpdateListenerTransformSystem) then mutate
  // gain + stereo pan on live spatial tracks (SpatialAudioSystem). AudioSystem (above) is the
  // one that adds the AudioSinkComponent and caches its MIX_Track in AudioTrackCache, which
  // these resolve per entity.
  registry_->Set<AudioListenerCache>(AudioListenerCache{});
  auto listenerTransform = registry_->RegisterBulkSystem<GlobalTransformComponent>(UpdateListenerTransformSystem());
  // Pre-register the AudioActiveTag so its tag entity exists
  // before any HasTag query fires (Tag<T>() is lazy otherwise). CullingSystem then gates
  // SpatialAudioSystem / DopplerSystem implicitly — culled emitters lose their sink, and
  // both downstream systems short-circuit on the missing sink.
  registry_->Tag<AudioActiveTag>();
  auto audioCulling = registry_->RegisterSystem<GlobalTransformComponent, AudioSourceComponent>(AudioCullingSystem());
  auto spatialAudio = registry_->RegisterSystem<GlobalTransformComponent, AudioSourceComponent, AudioSinkComponent>(
      SpatialAudioSystem());
  // Doppler is independent of spatial: governs frequency ratio via MIX_SetTrackFrequencyRatio.
  // Requires RigidBodyComponent on the emitter (no body → no velocity → no shift).
  auto doppler =
      registry_->RegisterSystem<GlobalTransformComponent, RigidBodyComponent, AudioSourceComponent, AudioSinkComponent>(
          DopplerSystem());

  auto cameraFollow = registry_->RegisterSystem<PositionComponent, CameraFollowComponent>(CameraFollowSystem());

  // UI layout pass: resolves UIRectComponent for all canvas descendants. Runs after TransformSystem
  // so world-parented canvases see up-to-date globals; runs before render systems that read UIRectComponent.
  auto uiLayout = registry_->RegisterBulkSystem(UILayoutSystem());
  registry_->Order(uiLayout).After(transform);

  // Render queue producers
  registry_->RegisterParallelSystem<GlobalTransformComponent, SpriteComponent>(RenderSpriteSystem());
  registry_->RegisterSystem<UIRectComponent, SpriteComponent>(RenderUISpriteSystem());
  registry_->RegisterBulkSystem(RenderUIPrimitiveSystem());
  registry_->RegisterSystem<TextLabelComponent>(RenderTextSystem());
  registry_->RegisterParallelSystem<SquarePrimitiveComponent, GlobalTransformComponent>(RenderPrimitiveSystem());
  registry_->RegisterParallelSystem<CirclePrimitiveComponent, GlobalTransformComponent>(RenderCirclePrimitiveSystem());
  registry_->RegisterParallelSystem<LinePrimitiveComponent, GlobalTransformComponent>(RenderLinePrimitiveSystem());
  registry_->RegisterParallelSystem<TrianglePrimitiveComponent, GlobalTransformComponent>(
      RenderTrianglePrimitiveSystem());
  registry_->RegisterParallelSystem<CapsulePrimitiveComponent, GlobalTransformComponent>(
      RenderCapsulePrimitiveSystem());
  registry_->RegisterParallelSystem<ColorGridComponent, GlobalTransformComponent>(RenderColorGridSystem());

  // Execution-order edges (topo-sorted in Registry::Update; registration order breaks ties).
  // Simulation-internal edges live in RegisterSimulationSystems.
  // Listener snapshot + spatial gain/pan need this-frame globals; spatial reads the listener
  // snapshot and is gated by culling (culled emitters lose their sink); both resolve tracks the
  // AudioSystem update cached.
  registry_->Order(listenerTransform).After(transform);
  registry_->Order(audioCulling).After(audioSystem);
  registry_->Order(spatialAudio).After(transform).After(listenerTransform).After(audioCulling).After(audioSystem);
  // Doppler needs this-frame emitter globals + listener snapshot, and sits behind the same
  // culling gate and AudioSystem-cached tracks as spatial.
  registry_->Order(doppler).After(transform).After(listenerTransform).After(audioCulling).After(audioSystem);
  // Camera follows after gameplay-driven transform updates.
  registry_->Order(cameraFollow).After(transform);

  // Event-driven systems with no per-frame Update — owned by the Registry instead of
  // living as parallel members on Game. Keeps the registry as the single source of truth.
  auto& uiButtonSystem = registry_->Set<UIButtonSystem>(UIButtonSystem());
  auto& scriptCollisionSystem = registry_->Set<ScriptCollisionSystem>(ScriptCollisionSystem());
  uiButtonSystem.Init(registry_.get(), event_bus_);
  scriptCollisionSystem.Init(registry_.get(), event_bus_);
  game_module::RegisterEventSystems(*registry_, event_bus_);

  // Hot reload owns its own FileWatcher + (path -> entities) discovery loop. Compiled out
  // under OCTARINE_SHIPPED; in dev/editor builds the runtime gate lives on EngineOptions.
  registry_->Set<ScriptHotReload>(ScriptHotReload());
  // Asset hot reload: same FileWatcher infra, watching resident texture/font/audio source files
  // and swapping the live SDL/MIX handles via AssetManager::ReloadByPath when they change on disk.
  registry_->Set<AssetHotReload>(AssetHotReload());
}
