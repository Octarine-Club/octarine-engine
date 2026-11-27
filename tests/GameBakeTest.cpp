// Headless Game + bake pipeline checks.
// - Constructor allocates Registry/EventBus/Lua without touching SDL (matches the documented
//   "Game game;" shape used by the smoke test).
// - Game::Bake(fixtureDir) runs end-to-end against a minimal project fixture: loads config,
//   scans the catalog, runs the startup script, writes asset_manifest.lua. Verifies the
//   manifest lands on disk.
// - SceneAssetScanner::CollectRefs preserves duplicates + provenance (Collect's dedup wrapper
//   is exercised by AssetPipelineTest).

#include <filesystem>
#include <fstream>
#include <sol/sol.hpp>
#include <string>

#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetPak.h"
#include "AssetManager/AssetReference.h"
#include "AssetManager/SceneAssetScanner.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/PositionComponent.h"
#include "Components/ScaleComponent.h"
#include "Components/SpriteComponent.h"
#include "Debug/DebugMenuRegistry.h"
#include "ECS/Registry.h"
#include "Engine/EngineBootstrap.h"
#include "Engine/SceneLoader.h"
#include "EventBus/EventBus.h"
#include "Game/EngineContext.h"
#include "Game/Game.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "Lua/HotReload/ScriptHotReload.h"
#include "Lua/LuaEntityLoader.h"
#include "Renderer/RenderQueue.h"
#include "Systems/RenderTextSystem.h"
#include "Systems/ScriptSystem.h"
#include "Systems/TilemapSystem.h"
#include "TestHarness.h"
#include "UI/UIRuntime.h"

using octarine::test::Check;
using octarine::test::CheckEq;

int main() {
  // Bake calls Logger::Info widely (works without Init via spdlog's default), but the bound
  // Lua log_i / log_e in the startup script dereference lua_logger_ which Init sets up.
  Logger::Init();

  std::cout << "[headless Game construction]\n";
  {
    // The constructor must not open a window, init SDL, or create a renderer — only allocate
    // the registry/eventbus/lua state. Smoke + behavior tests both rely on this guarantee.
    Game game;
    Check(game.GetRegistry() != nullptr, "Game::GetRegistry() returns a live Registry");
    Check(game.GetWindow() == nullptr, "Game::GetWindow() is nullptr before Initialize()");
    Check(game.GetRenderer() == nullptr, "Game::GetRenderer() is nullptr before Initialize()");

    // Registry is usable: create an entity, verify counters.
    const Entity e = game.GetRegistry()->CreateEntity();
    Check(game.GetRegistry()->IsAlive(e), "Registry held by headless Game accepts CreateEntity");
    CheckEq(game.GetRegistry()->GetUserEntityCount(), 1ULL, "user entity count visible on headless Game");
  }

  std::cout << "[scanner: CollectRefs preserves duplicates + provenance]\n";
  {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    const auto scene = lua.script(R"lua(
          return {
            preload = { "shared-tex" },
            entities = {
              {
                components = {
                  sprite = { texture_asset_id = "shared-tex" },
                },
              },
              {
                components = {
                  sprite = { texture_asset_id = "shared-tex" },  -- duplicate id, distinct ref
                },
              },
              {
                components = {
                  audio_source = { clip_id = "blip" },
                },
              },
            },
          }
        )lua");

    const std::vector<AssetReference> refs = SceneAssetScanner::CollectRefs(scene);
    // 1 preload + 2 sprite refs + 1 audio = 4 references with the same dup id present twice.
    CheckEq(refs.size(), size_t{4}, "CollectRefs returns one entry per reference site");

    int sharedTexCount = 0;
    bool foundBlip = false;
    for (const auto& ref : refs) {
      if (ref.id == "shared-tex") ++sharedTexCount;
      if (ref.id == "blip") foundBlip = true;
      Check(!ref.context.empty(), "every ref carries provenance context");
    }
    CheckEq(sharedTexCount, 3, "shared-tex appears 3x in CollectRefs (preload + 2 sprites)");
    Check(foundBlip, "CollectRefs picks up audio_source.clip_id");

    // The dedup wrapper (Collect) is the production path for "what's in this scene".
    const auto ids = SceneAssetScanner::Collect(scene);
    CheckEq(ids.size(), size_t{2}, "Collect dedups CollectRefs down to 2 distinct ids");
  }

  std::cout << "[scanner: cross-scene delta — shared vs. exclusive ids]\n";
  {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    const auto sceneA = lua.script(R"lua(
          return {
            preload = { "shared", "only-in-a" },
            entities = {},
          }
        )lua");
    const auto sceneB = lua.script(R"lua(
          return {
            preload = { "shared", "only-in-b-1", "only-in-b-2" },
            entities = {},
          }
        )lua");

    const auto a = SceneAssetScanner::Collect(sceneA);
    const auto b = SceneAssetScanner::Collect(sceneB);
    Check(a.count("shared") == 1 && b.count("shared") == 1, "shared id present in both scenes");
    Check(a.count("only-in-a") == 1 && b.count("only-in-a") == 0, "only-in-a exclusive to scene A");
    Check(a.count("only-in-b-1") == 0 && b.count("only-in-b-1") == 1, "only-in-b-1 exclusive to scene B");

    // Acquire-before-release semantics from Game::LoadScene depend on this set difference being
    // correct: ids only in A get released, ids only in B get acquired, shared ids untouched.
    std::vector<std::string> toRelease;
    for (const auto& id : a) {
      if (b.find(id) == b.end()) toRelease.push_back(id);
    }
    std::vector<std::string> toAcquire;
    for (const auto& id : b) {
      if (a.find(id) == a.end()) toAcquire.push_back(id);
    }
    CheckEq(toRelease.size(), size_t{1}, "exactly one id to release across A→B swap");
    CheckEq(toAcquire.size(), size_t{2}, "exactly two new ids to acquire on A→B swap");
  }

#ifdef BAKE_FIXTURE_DIR
  std::cout << "[bake: end-to-end against fixture project]\n";
  {
    const std::filesystem::path fixtureDir = BAKE_FIXTURE_DIR;
    const std::filesystem::path manifestPath = fixtureDir / "asset_manifest.lua";

    // Bake writes several artifacts next to the project (manifest + asset pak + atlas output dir).
    // The test owns all of them: wipe before and after so the source tree stays clean on CI and
    // re-runs start from the same pre-state.
    const auto CleanBakeOutputs = [&] {
      std::error_code ec;
      std::filesystem::remove(manifestPath, ec);
      std::filesystem::remove(fixtureDir / "asset_bundle.pak", ec);
      std::filesystem::remove_all(fixtureDir / "atlases", ec);
      std::filesystem::remove_all(fixtureDir / "assets" / "atlases", ec);
    };

    CleanBakeOutputs();

    const bool ok = Game::Bake(fixtureDir.string());
    Check(ok, "Game::Bake returns true on a clean fixture project");
    Check(std::filesystem::exists(manifestPath), "Game::Bake wrote asset_manifest.lua next to the project");
    Check(std::filesystem::exists(fixtureDir / "asset_bundle.pak"),
          "Game::Bake wrote asset_bundle.pak next to the project");

    AssetPak pak;
    Check(pak.Open((fixtureDir / "asset_bundle.pak").string()), "AssetPak::Open succeeds on baked asset_bundle.pak");
    CheckEq(pak.Version(), 1U, "AssetPak is OCPK v1");
    Check(pak.IsEncrypted(), "AssetPak is encrypted with ChaCha20");
    Check(pak.Contains("main.lua"), "asset_bundle.pak contains main.lua script");
    Check(pak.Contains("config.ini"), "asset_bundle.pak embeds config.ini");
    Check(pak.Contains("asset_manifest.lua"), "asset_bundle.pak embeds asset_manifest.lua");

    // Verify on-the-fly seekable stream decryption matches original source
    SDL_IOStream* io = pak.OpenIO("main.lua");
    Check(io != nullptr, "OpenIO succeeds for main.lua");
    if (io != nullptr) {
      std::size_t loadedSize = 0;
      void* loadedData = SDL_LoadFile_IO(io, &loadedSize, true);
      Check(loadedData != nullptr, "SDL_LoadFile_IO successfully read main.lua from pak");
      if (loadedData != nullptr) {
        std::ifstream srcIn(fixtureDir / "main.lua", std::ios::binary);
        std::string srcContent((std::istreambuf_iterator<char>(srcIn)), std::istreambuf_iterator<char>());
        CheckEq(loadedSize, srcContent.size(), "decrypted main.lua matches source size");
        Check(std::memcmp(loadedData, srcContent.data(), loadedSize) == 0, "decrypted main.lua content matches source");
        SDL_free(loadedData);
      }
    }

    // Verify raw pak file has no plaintext filenames
    {
      std::ifstream rawPak(fixtureDir / "asset_bundle.pak", std::ios::binary);
      std::string pakBytes((std::istreambuf_iterator<char>(rawPak)), std::istreambuf_iterator<char>());
      Check(pakBytes.find("main.lua") == std::string::npos, "raw pak on disk contains zero plaintext filenames");
      Check(pakBytes.find("config.ini") == std::string::npos, "raw pak on disk does not leak config filename");
    }

    CleanBakeOutputs();
  }

  std::cout << "[bake: excludes *.preview.lua sidecars from asset_bundle.pak]\n";
  {
    const std::filesystem::path fixtureDir = BAKE_FIXTURE_DIR;
    const std::filesystem::path tempProj =
        std::filesystem::temp_directory_path() / "octarine_bake_preview_sidecar_test";
    std::error_code ec;
    std::filesystem::remove_all(tempProj, ec);
    std::filesystem::copy(fixtureDir, tempProj, std::filesystem::copy_options::recursive, ec);
    std::filesystem::create_directories(tempProj / "assets" / "scripts", ec);
    {
      std::ofstream f1((tempProj / "assets" / "root_ignored.preview.lua").string());
      f1 << "return { context = { ok = true } }\n";
      std::ofstream f2((tempProj / "assets" / "scripts" / "hud.preview.lua").string());
      f2 << "return { context = { ok = true } }\n";
    }
    Check(Game::Bake(tempProj.string()), "Game::Bake succeeds with *.preview.lua files present");
    AssetPak pak;
    Check(pak.Open((tempProj / "asset_bundle.pak").string()), "opens temp baked pak");
    Check(pak.Contains("main.lua"), "temp pak contains main.lua");
    Check(!pak.Contains("root_ignored.preview.lua"), "asset_bundle.pak excludes root *.preview.lua");
    Check(!pak.Contains("scripts/hud.preview.lua"), "asset_bundle.pak excludes scripts/**/*.preview.lua");
    std::filesystem::remove_all(tempProj, ec);
  }

  std::cout << "[bake: recursively packs non-scripts/ Lua dirs, package_include, and fails on entry script error]\n";
  {
    const std::filesystem::path fixtureDir = BAKE_FIXTURE_DIR;
    const std::filesystem::path tempProj = std::filesystem::temp_directory_path() / "octarine_bake_batch_b_test";
    std::error_code ec;
    std::filesystem::remove_all(tempProj, ec);
    std::filesystem::copy(fixtureDir, tempProj, std::filesystem::copy_options::recursive, ec);
    std::filesystem::create_directories(tempProj / "scenes", ec);
    std::filesystem::create_directories(tempProj / "ui", ec);
    std::filesystem::create_directories(tempProj / "data", ec);
    std::filesystem::create_directories(tempProj / "types", ec);
    std::filesystem::create_directories(tempProj / "build", ec);
    {
      std::ofstream(tempProj / "scenes" / "level1.lua") << "return { entities = {} }\n";
      std::ofstream(tempProj / "ui" / "hud_ctrl.lua") << "return {}\n";
      std::ofstream(tempProj / "data" / "waves.json") << "{ \"waves\": [1, 2, 3] }\n";
      std::ofstream(tempProj / "types" / "octarine_api.lua") << "---@meta\n";
      std::ofstream(tempProj / "build" / "stray.lua") << "error('should not pack')\n";
      std::ofstream(tempProj / "project.ini") << "name=BakeTest\npackage_id=com.test.bake\nversion_name=1.0.0\n"
                                              << "package_include=data/waves.json\n";
    }
    Check(Game::Bake(tempProj.string()), "Game::Bake succeeds with nested Lua dirs and package_include");
    AssetPak pak;
    Check(pak.Open((tempProj / "asset_bundle.pak").string()), "opens baked pak for recursive Lua + package_include");
    Check(pak.Contains("scenes/level1.lua"), "asset_bundle.pak packs scenes/level1.lua (ENG-RND-07)");
    Check(pak.Contains("ui/hud_ctrl.lua"), "asset_bundle.pak packs ui/hud_ctrl.lua (ENG-RND-07)");
    Check(pak.Contains("data/waves.json"), "asset_bundle.pak packs package_include data/waves.json (ENG-RND-07)");
    Check(!pak.Contains("types/octarine_api.lua"), "asset_bundle.pak excludes types/octarine_api.lua");
    Check(!pak.Contains("build/stray.lua"), "asset_bundle.pak excludes build/stray.lua");

    // Verify Bake fails immediately when the startup script throws a Lua error (ENG-LUA-01).
    { std::ofstream(tempProj / "main.lua") << "error('simulated startup script failure')\n"; }
    Check(!Game::Bake(tempProj.string()), "Game::Bake returns false when startup script errors (ENG-LUA-01)");
    std::filesystem::remove_all(tempProj, ec);
  }

  std::cout << "[batch-b: mid-frame load_entity GlobalTransform, deferred StopScene, and clearSceneEntities]\n";
  {
    const std::filesystem::path tempProj = std::filesystem::temp_directory_path() / "octarine_scene_lifecycle_b_test";
    std::error_code ec;
    std::filesystem::remove_all(tempProj, ec);
    std::filesystem::create_directories(tempProj, ec);
    {
      std::ofstream(tempProj / "config.ini") << "Title=BatchBTest\nStartupScript=good_scene.lua\n";
      std::ofstream(tempProj / "good_scene.lua") << R"lua(
        return {
          entities = {
            {
              components = {
                transform = { position = { x = 42.0, y = 84.0 }, scale = { x = 2.0, y = 3.0 } },
                sprite = { texture_asset_id = "dummy", width = 16, height = 16 },
              },
            },
          },
        }
      )lua";
      std::ofstream(tempProj / "bad_scene.lua") << "error('scene syntax/runtime failure')\n";
    }

    Game game;
    Registry* reg = game.GetRegistry();
    sol::state& lua = game.GetLua();
    GameConfig cfg;
    Check(cfg.LoadConfigFromFile(tempProj.string()), "loaded temp config.ini");
    reg->Set<GameConfig>(cfg);
    EngineContext ctx{};
    ctx.config = &reg->Get<GameConfig>();
    reg->Set<EngineContext>(ctx);
    engine_bootstrap::InstallCoreSingletons(*reg, reg->Get<EngineContext>(), 320, 240, true, &lua);
    engine_bootstrap::InstallPoolAndSingletons(*reg);
    engine_bootstrap::RegisterAllComponentBindings();
    engine_bootstrap::InstallLuaLibraries(lua);
    ScriptSystem scriptSys;
    scriptSys.CreateLuaBindings(lua);
    engine_bootstrap::InstallLuaModules(lua, game);
    reg->Get<AssetManager>().LoadGameConfig(reg->Get<GameConfig>());

    // 1. Mid-frame LuaEntityLoader initializes GlobalTransformComponent from local transform (ENG-ECS-08).
    {
      sol::table entTbl = lua.script(R"lua(
        return {
          components = {
            transform = { position = { x = 120.0, y = 240.0 } },
            sprite = { texture_asset_id = "dummy", width = 16, height = 16 },
          },
        }
      )lua");
      const auto spawned = LuaEntityLoader::LoadEntityFromLua(reg, entTbl);
      Check(spawned.has_value() && reg->HasComponent<GlobalTransformComponent>(*spawned),
            "LoadEntityFromLua attaches GlobalTransformComponent");
      if (spawned.has_value()) {
        const auto& gt = reg->GetComponent<GlobalTransformComponent>(*spawned);
        CheckEq(gt.position.x, 120.0F, "mid-frame spawned entity GlobalTransform.position.x initialized immediately");
        CheckEq(gt.position.y, 240.0F, "mid-frame spawned entity GlobalTransform.position.y initialized immediately");
      }
    }

    // 2. SceneLoader::LoadScene failure does not set scene_running_ = true and logs to RecentScriptErrors (ENG-LUA-01).
    SceneLoader loader(reg, lua);
    const std::size_t errsBefore = Logger::RecentScriptErrors().size();
    loader.LoadScene("bad_scene.lua");
    Check(!loader.IsSceneRunning(), "failed LoadScene leaves IsSceneRunning() == false (ENG-LUA-01)");
    Check(loader.LastLoadFailed(), "failed LoadScene sets LastLoadFailed() == true");
    Check(Logger::RecentScriptErrors().size() > errsBefore,
          "failed LoadScene routes error to Logger::ErrorLua / RecentScriptErrors (ENG-LUA-01)");

    // 3. Immediate StopScene when !deferred_swaps_ vs deferred StopScene when deferred_swaps_ (ENG-RND-01).
    loader.LoadScene("good_scene.lua");
    Check(loader.IsSceneRunning(), "good_scene.lua sets IsSceneRunning() == true");
    CheckEq(reg->GetUserEntityCount(), 1ULL, "good_scene.lua spawned 1 user entity");

    // Seed singletons that clearSceneEntities must clean up (ENG-LUA-05, ENG-LUA-06, ENG-RND-04).
    reg->Get<TilemapSystem>().SetPropSpawner(lua.script("return function() end").get<sol::protected_function>());
    reg->Get<octarine::debug::DebugMenuRegistry>().RegisterAction("Game/Test", [](Registry&) {}, "", true);
    reg->Get<octarine::LuaScriptHotReloadTracker>().watcher.Track((tempProj / "good_scene.lua").string());
    reg->Get<RenderQueue>().EmplaceSquare(0, 0.0F);
    auto& uiRuntime = reg->Get<UIRuntime>();
    {
      const sol::object viewObj =
          uiRuntime.Mount(sol::make_object(lua, "<Canvas><Label text='hi'/></Canvas>"), sol::nullopt);
      Check(viewObj.is<sol::table>(), "mounted inline OXML view before scene stop");
      CheckEq(uiRuntime.GetMountedViewsDebugInfo().size(), size_t{1}, "1 mounted UI view active");
    }

    // Before FlushPendingSceneLoad arms deferral, StopScene is immediate.
    loader.StopScene();
    Check(!loader.IsSceneRunning(), "StopScene is immediate when !deferred_swaps_ (ENG-RND-01)");
    CheckEq(reg->GetUserEntityCount(), 0ULL, "immediate StopScene cleared user entities");
    Check(uiRuntime.GetMountedViewsDebugInfo().empty(),
          "clearSceneEntities unmounted all UIRuntime views (ENG-LUA-05)");
    Check(reg->Get<octarine::debug::DebugMenuRegistry>().Actions().empty(),
          "clearSceneEntities cleared Lua-owned DebugMenuRegistry actions (ENG-LUA-06)");
    CheckEq(reg->Get<octarine::LuaScriptHotReloadTracker>().watcher.TrackedCount(), size_t{0},
            "clearSceneEntities cleared LuaScriptHotReloadTracker (ENG-LUA-06)");
    Check(reg->Get<RenderQueue>().IsEmpty(), "clearSceneEntities cleared RenderQueue (ENG-RND-01)");

    // Arm deferral via FlushPendingSceneLoad, then verify StopScene defers until next FlushPendingSceneLoad.
    loader.LoadScene("good_scene.lua");
    Check(loader.IsSceneRunning() && reg->GetUserEntityCount() == 1ULL, "reloaded good_scene.lua");
    loader.FlushPendingSceneLoad();  // arms deferred_swaps_ = true
    loader.StopScene();
    Check(loader.IsSceneRunning() && reg->GetUserEntityCount() == 1ULL,
          "StopScene is deferred while deferred_swaps_ is active (ENG-RND-01)");
    loader.FlushPendingSceneLoad();
    Check(!loader.IsSceneRunning() && reg->GetUserEntityCount() == 0ULL,
          "FlushPendingSceneLoad executes pending deferred StopScene (ENG-RND-01)");

    // 4. Game::Destroy is idempotent and safely closes lua before registry reset (ENG-ECS-01).
    game.Destroy();
    game.Destroy();
    Check(game.GetRegistry() == nullptr, "Game::Destroy resets registry_ after closing lua (ENG-ECS-01)");

    std::filesystem::remove_all(tempProj, ec);
  }
#else
  std::cerr << "  WARN BAKE_FIXTURE_DIR not defined; skipping Game::Bake check\n";
#endif

  return octarine::test::ReportSummary("Game bake test");
}
