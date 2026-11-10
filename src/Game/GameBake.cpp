#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "AssetManager/AssetCatalog.h"
#include "AssetManager/AssetPak.h"
#include "AssetManager/AtlasBaker.h"
#include "AssetManager/AudioNormalizer.h"
#include "AssetManager/SdlFileReader.h"
#include "AssetManager/TextureAtlasBaker.h"
#include "Crypto/LuaProtect.h"
#include "Engine/EngineBootstrap.h"
#include "Engine/GameModuleHooks.h"
#include "Game/Game.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "Lua/Bindings/InputSystemLuaBinding.h"
#include "Lua/Bindings/LuaSystemRegistry.h"
#include "Project/ProjectIni.h"
#include "Systems/InputSystem.h"
#include "Systems/ScriptSystem.h"
#include "UI/UIRuntime.h"

namespace {
constexpr const char* kStartupModeBake = "bake";
constexpr const char* kAtlasesDirName = "atlases";
constexpr const char* kAtlasPngSuffix = ".atlas.png";
constexpr const char* kAtlasLuaSuffix = ".atlas.lua";
constexpr const char* kNormalizedDirName = "normalized";
constexpr const char* kScriptsDirName = "scripts";
constexpr const char* kLuaExtension = ".lua";

inline void LoadGameForBake(sol::state& lua, const AssetManager& assetManager, const GameConfig& gameConfig) {
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
}  // namespace

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

// End-to-end headless asset baking pipeline (catalog scan, UI validation, atlas packing, audio normalization, pak).
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
  engine_bootstrap::SetCommonLuaGlobals(lua, gameConfig, startup_mode_, dev_mode_);
  game_module::RegisterDebugUI(*registry_);

  auto& assetManager = registry_->Get<AssetManager>();
  assetManager.LoadGameConfig(gameConfig);

  // Always re-derive the catalog from the real files — never round-trip an existing (possibly
  // stale) manifest sitting in the project dir.
  if (!assetManager.GetCatalog().ScanFilesystem(assetManager.GetBasePath(), lua, gameConfig)) {
    Logger::Error("Bake: catalog scan failed (id collisions). Aborting.");
    return false;
  }
  assetManager.GetCatalog().InstallLuaIdTable(lua);
  bake_validation_failures_ += UIRuntime::ValidateCatalogUI(assetManager);

  // Run the startup script. Its scene loads call acquire_scene_assets / load_asset, which in bake
  // mode validate every referenced id against the catalog and tally failures on this Game instead
  // of uploading to a GPU. This is the CI gate: a typo'd or missing asset reference fails the bake.
  LoadGameForBake(lua, assetManager, gameConfig);

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
    const std::filesystem::path atlasDir = std::filesystem::exists(std::filesystem::path(assetPath) / "assets")
                                               ? std::filesystem::path(assetPath) / "assets" / kAtlasesDirName
                                               : std::filesystem::path(assetPath) / kAtlasesDirName;
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
      size_t iniSize = 0;
      if (void* iniData = SDL_LoadFile(projectIniFile.string().c_str(), &iniSize)) {
        const std::string iniStr(static_cast<const char*>(iniData), iniSize);
        SDL_free(iniData);
        const auto ini = octarine::project::ProjectIni::Parse(iniStr);
        if (!ini.icon.empty()) {
          const auto iconPath = (std::filesystem::path(assetPath) / ini.icon).lexically_normal();
          if (std::filesystem::is_regular_file(iconPath, ec)) {
            extraFiles.push_back(iconPath.string());
          }
        }
      }
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
