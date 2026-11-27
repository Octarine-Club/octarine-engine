// Headless scene-swap stress test (ENG-DX-07).
// Cycles 120+ scene load/reload/unload transitions across table-based and function-based scenes,
// exercising shared vs. exclusive textures/fonts, OXML UI mounts, sprite/text render caches,
// tilemap prop spawners, Lua debug registrations, and Lua GC collection.

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sol/sol.hpp>
#include <string>

#include "AssetManager/AssetManager.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/ScriptComponent.h"
#include "Components/SpriteComponent.h"
#include "Components/TextLabelComponent.h"
#include "Debug/DebugMenuRegistry.h"
#include "ECS/Registry.h"
#include "Engine/EngineBootstrap.h"
#include "Engine/SceneLoader.h"
#include "Engine/SimulationSystems.h"
#include "EventBus/EventBus.h"
#include "Game/EngineContext.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "Lua/Bindings/InputSystemLuaBinding.h"
#include "Lua/Bindings/LuaSystemRegistry.h"
#include "Lua/LuaBindingContext.h"
#include "Lua/Modules/RegisterAllModules.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/SpriteRenderCache.h"
#include "Systems/InputSystem.h"
#include "Systems/RenderSpriteSystem.h"
#include "Systems/RenderTextSystem.h"
#include "Systems/ScriptSystem.h"
#include "Systems/TilemapSystem.h"
#include "Systems/UILayoutSystem.h"
#include "TestHarness.h"
#include "UI/UIRuntime.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

constexpr unsigned char kMinimalTestTtf[] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x80, 0x00, 0x03, 0x00, 0x20, 0x4F, 0x53, 0x2F, 0x32, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xAC, 0x00, 0x00, 0x00, 0x56, 0x63, 0x6D, 0x61, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x04, 0x00, 0x00, 0x00, 0x2C, 0x67, 0x6C, 0x79, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x30, 0x00,
    0x00, 0x00, 0x22, 0x68, 0x65, 0x61, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x54, 0x00, 0x00, 0x00, 0x36,
    0x68, 0x68, 0x65, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x8C, 0x00, 0x00, 0x00, 0x24, 0x68, 0x6D, 0x74,
    0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xB0, 0x00, 0x00, 0x00, 0x06, 0x6C, 0x6F, 0x63, 0x61, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0xB8, 0x00, 0x00, 0x00, 0x06, 0x6D, 0x61, 0x78, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0xC0, 0x00, 0x00, 0x00, 0x20, 0x6E, 0x61, 0x6D, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xE0,
    0x00, 0x00, 0x00, 0x06, 0x70, 0x6F, 0x73, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xE8, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x01, 0x02, 0x58, 0x01, 0x90, 0x00, 0x05, 0x00, 0x00, 0x01, 0xF4, 0x01, 0xF4, 0x00, 0x00, 0x00, 0x64,
    0x01, 0xF4, 0x01, 0xF4, 0x00, 0x00, 0x01, 0x2C, 0x00, 0x32, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x54, 0x45, 0x53, 0x54, 0x00, 0x40, 0x00, 0x20, 0x00, 0x7E, 0x03, 0x20, 0xFF, 0x38, 0x00, 0x00, 0x03,
    0x20, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x04, 0x00, 0x20, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x7E, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x20, 0xFF, 0xFF, 0xFF, 0xE1, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x32, 0x00, 0x00, 0x02, 0x26, 0x02, 0xBC, 0x00, 0x03, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x32, 0x01, 0xF4, 0x00, 0x00, 0xFE, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x02, 0xBC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x0F, 0x3C, 0xF5, 0x00, 0x0B, 0x03, 0xE8, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00,
    0x02, 0x26, 0x02, 0xBC, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x03, 0x20, 0xFF, 0x38, 0x00, 0x00, 0x02, 0x58, 0x00, 0x32, 0x00, 0x00, 0x02, 0x26, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x58, 0x00, 0x32, 0x00,
    0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xCE, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void WriteTextFile(const std::filesystem::path& path, const std::string& content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

bool WriteSolidPng(const std::filesystem::path& path, const int w, const int h, const Uint8 r, const Uint8 g,
                   const Uint8 b) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  SDL_Surface* surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
  if (surf == nullptr) {
    return false;
  }
  const Uint32 pixel = SDL_MapSurfaceRGBA(surf, r, g, b, 255);
  SDL_FillSurfaceRect(surf, nullptr, pixel);
  const bool ok = IMG_SavePNG(surf, path.string().c_str());
  SDL_DestroySurface(surf);
  return ok;
}

bool CreateStressFixtureProject(const std::filesystem::path& root) {
  WriteTextFile(root / "config.ini",
                "Title=SceneSwapStress\nStartupScript=main.lua\nBaseResolutionWidth=640\nBaseResolutionHeight=360\n");
  WriteTextFile(root / "main.lua", "return {}\n");

  if (!WriteSolidPng(root / "sprites" / "shared_tex.png", 8, 8, 220, 220, 60) ||
      !WriteSolidPng(root / "sprites" / "tex_a.png", 8, 8, 60, 180, 240) ||
      !WriteSolidPng(root / "sprites" / "tex_b.png", 8, 8, 240, 80, 120) ||
      !WriteSolidPng(root / "sprites" / "tex_c.png", 8, 8, 90, 220, 120)) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(root / "fonts", ec);
  {
    std::ofstream fontOut(root / "fonts" / "hud.ttf", std::ios::binary | std::ios::trunc);
    fontOut.write(reinterpret_cast<const char*>(kMinimalTestTtf), sizeof(kMinimalTestTtf));
  }

  WriteTextFile(root / "ui" / "hud.oxml", R"OXML(
<OXML>
  <Canvas id="stress-hud" width="640px" height="360px">
    <Column width="100%" height="100%" gap="4">
      <Label text="Cycle {cycle}" font="hud-16" />
      <Panel each="item in {items}" width="64px" height="20px">
        <Label text="{item}" font="hud-16" />
      </Panel>
    </Column>
  </Canvas>
</OXML>
)OXML");

  WriteTextFile(root / "maps" / "prop_room.ldtk", R"JSON({
  "defaultGridSize": 16,
  "defs": { "tilesets": [] },
  "levels": [
    {
      "identifier": "Room_0_0",
      "worldX": 0,
      "worldY": 0,
      "pxWid": 64,
      "pxHei": 64,
      "layerInstances": [
        {
          "__identifier": "Entities",
          "__type": "Entities",
          "__cWid": 4,
          "__cHei": 4,
          "__gridSize": 16,
          "entityInstances": [
            { "__identifier": "Crate", "px": [16, 16], "width": 16, "height": 16, "fieldInstances": [] }
          ]
        }
      ]
    }
  ]
})JSON");

  WriteTextFile(root / "scenes" / "scene_a.lua", R"LUA(
local marker = { scene = "a", iter = _G.stress_iter }
_G.stress_weak_tracker[#_G.stress_weak_tracker + 1] = marker

local ents = {}
for i = 1, 8 do
  ents[#ents + 1] = {
    name = "a_ent_" .. tostring(i),
    components = {
      transform = { position = { x = i * 16, y = 32 } },
      sprite = { texture_asset_id = (i % 2 == 0) and "shared_tex" or "tex_a", width = 8, height = 8, layer = 1 },
    }
  }
end
ents[#ents + 1] = {
  components = {
    transform = { position = { x = 20, y = 20 } },
    text_label = { text = "Scene A " .. tostring(_G.stress_iter), font_id = "hud-16", layer = 2 },
  }
}

return {
  preload = { "shared_tex", "tex_a", "hud-16" },
  entities = ents,
  setup = function()
    input.on_key_down(function() return marker.scene end)
    -- Load the LDtk room WITHOUT setting a prop spawner: if Scene C's prop spawner leaked across
    -- the swap, stale_spawner_hits would increment here.
    tilemap.load("maps/prop_room.ldtk")
  end
}
)LUA");

  WriteTextFile(root / "scenes" / "scene_b.lua", R"LUA(
local marker = { scene = "b", iter = _G.stress_iter }
_G.stress_weak_tracker[#_G.stress_weak_tracker + 1] = marker

local ents = {}
for i = 1, 10 do
  ents[#ents + 1] = {
    name = "b_ent_" .. tostring(i),
    components = {
      transform = { position = { x = i * 12, y = 64 } },
      sprite = { texture_asset_id = (i <= 5) and "shared_tex" or "tex_b", width = 8, height = 8, layer = 1 },
    }
  }
end
ents[#ents + 1] = {
  components = {
    transform = { position = { x = 40, y = 40 } },
    text_label = { text = "Scene B " .. tostring(_G.stress_iter), font_id = "hud-16", layer = 2 },
  }
}

return {
  preload = { "shared_tex", "tex_b", "hud-16" },
  entities = ents,
}
)LUA");

  WriteTextFile(root / "scenes" / "scene_c.lua", R"LUA(
return function()
  local marker = { scene = "c", iter = _G.stress_iter }
  _G.stress_weak_tracker[#_G.stress_weak_tracker + 1] = marker

  acquire_scene_assets({ preload = { "tex_c" } })
  for i = 1, 6 do
    load_entity({
      components = {
        transform = { position = { x = i * 20, y = 96 } },
        sprite = { texture_asset_id = "tex_c", width = 8, height = 8, layer = 1 },
      }
    })
  end

  local vm = ui.observable({
    cycle = _G.stress_iter,
    items = { "Alpha", "Beta", "Gamma", "Delta" }
  })
  ui.mount("ui/hud.oxml", { context = vm })

  local active_scene_tag = "c"
  tilemap.set_prop_spawner(function(prop)
    if _G.current_expected_scene ~= active_scene_tag then
      _G.stale_spawner_hits = _G.stale_spawner_hits + 1
    end
    return marker.iter
  end)
  tilemap.load("maps/prop_room.ldtk")

  dev.watch("stress_watch", function() return tostring(marker.iter) end)
  dev.register_action("Game/StressAction", function() end)
end
)LUA");

  return true;
}

std::size_t CountMountedViews(const UIRuntime* uiRuntime) {
  if (uiRuntime == nullptr) {
    return 0;
  }
  std::size_t active = 0;
  for (const auto& info : uiRuntime->GetMountedViewsDebugInfo()) {
    if (info.isMounted) {
      ++active;
    }
  }
  return active;
}

std::size_t CountLuaOwnedDebugEntries(const octarine::debug::DebugMenuRegistry* debugReg) {
  if (debugReg == nullptr) {
    return 0;
  }
  std::size_t count = 0;
  for (const auto& w : debugReg->Watches()) {
    if (w.isLuaOwned) ++count;
  }
  for (const auto& a : debugReg->Actions()) {
    if (a.isLuaOwned) ++count;
  }
  for (const auto& t : debugReg->Toggles()) {
    if (t.isLuaOwned) ++count;
  }
  return count;
}

class HeadlessSceneBindingContext final : public LuaBindingContext {
 public:
  HeadlessSceneBindingContext(Registry* registry, SceneLoader* loader, SDL_Renderer* sdlRenderer,
                              std::string projectPath)
      : registry_(registry),
        loader_(loader),
        sdl_renderer_(sdlRenderer),
        project_path_(std::move(projectPath)),
        save_path_(project_path_ + "/save") {}

  [[nodiscard]] Registry* GetRegistry() const override { return registry_; }
  [[nodiscard]] SDL_Renderer* GetRenderer() const override { return sdl_renderer_; }
  [[nodiscard]] EngineContext& GetContext() override { return registry_->Get<EngineContext>(); }
  [[nodiscard]] bool IsBakeMode() const override { return false; }
  void RecordBakeValidationFailures(int /*failures*/) override {}
  void RequestQuit() override {}
  void ReturnFocusToGame() override {}
  void LoadScene(const std::string& scenePath) override { loader_->RequestLoadScene(scenePath); }
  void ReloadScene() override { loader_->ReloadScene(); }
  void StopScene() override { loader_->StopScene(); }
  void TrackSceneAssets(const std::vector<std::string>& assetIds) override { loader_->TrackSceneAssets(assetIds); }
  [[nodiscard]] std::string GetProjectPath() const override { return project_path_; }
  std::string GetSaveDataPath() override { return save_path_; }

 private:
  Registry* registry_;
  SceneLoader* loader_;
  SDL_Renderer* sdl_renderer_;
  std::string project_path_;
  std::string save_path_;
};

void SetupHeadlessHarness(Registry& reg, sol::state& lua, LuaBindingContext& bindingCtx,
                          const std::unique_ptr<EventBus>& eventBus, SDL_Renderer* sdlRenderer,
                          const std::filesystem::path& projDir) {
  auto& cfg = reg.Set<GameConfig>(GameConfig());
  Check(cfg.LoadConfigFromFile(projDir.string()), "GameConfig loaded stress fixture config.ini");
  cfg.windowWidth = 640;
  cfg.windowHeight = 360;

  EngineContext ctx{};
  ctx.sdlRenderer = sdlRenderer;
  ctx.eventBus = eventBus.get();
  ctx.config = &cfg;
  reg.Set<EngineContext>(ctx);

  engine_bootstrap::InstallCoreSingletons(reg, reg.Get<EngineContext>(), cfg.windowWidth, cfg.windowHeight,
                                          /*withFramePathCaches=*/true, &lua);
  auto scriptSys = reg.RegisterSystem<ScriptComponent>(ScriptSystem());
  auto& inputSys = engine_bootstrap::InstallInputSystem(reg, eventBus);
  engine_bootstrap::RegisterAllComponentBindings();
  LuaSystemRegistry::clear();
  LuaSystemRegistry::registerSystem(inputSys);
  engine_bootstrap::InstallLuaLibraries(lua);
  engine_bootstrap::InstallPoolAndSingletons(reg);
  scriptSys.Func().CreateLuaBindings(lua);
  RegisterAllLuaModules(lua, bindingCtx);
  LuaSystemRegistry::bindAll(lua);
  engine_bootstrap::SetCommonLuaGlobals(lua, cfg, /*startupMode=*/"", /*devMode=*/true);

  auto& am = reg.Get<AssetManager>();
  am.LoadGameConfig(cfg);
  Check(am.GetCatalog().Build(am.GetBasePath(), lua, cfg, /*allowManifest=*/false),
        "stress fixture AssetCatalog scanned cleanly");
  am.GetCatalog().InstallLuaIdTable(lua);

  const auto sim = engine_bootstrap::RegisterSimulationSystems(reg, eventBus);
  auto uiLayout = reg.RegisterBulkSystem(UILayoutSystem());
  reg.Order(uiLayout).After(sim.transform);
  reg.RegisterBulkSystem(TilemapRenderSystem());
  reg.RegisterParallelSystem<GlobalTransformComponent, SpriteComponent>(RenderSpriteSystem());
  auto renderText = reg.RegisterSystem<TextLabelComponent>(RenderTextSystem());
  reg.Order(renderText).After(uiLayout);

  lua["stress_iter"] = 0;
  lua["stale_spawner_hits"] = 0;
  lua["current_expected_scene"] = "";
  lua.script("global *; global stress_weak_tracker = setmetatable({}, { __mode = 'v' })");
}

void RunFrameTick(Registry& reg) {
  reg.Get<RenderQueue>().Clear();
  reg.Update(1.0F / 60.0F);
  reg.Get<RenderQueue>().Clear();
}

void RunSceneSwapStressCycles(Registry& reg, sol::state& lua, SceneLoader& loader) {
  auto& am = reg.Get<AssetManager>();
  auto& spriteCache = reg.Get<SpriteRenderCache>();
  const auto* uiRuntime = reg.TryGet<UIRuntime>();
  const auto* debugReg = reg.TryGet<octarine::debug::DebugMenuRegistry>();

  constexpr int kFullCycles = 40;  // 40 * 3 scenes = 120 scene swaps (+ periodic ReloadScene)
  int transitions = 0;
  bool sharedPointerStable = true;
  bool exclusiveReleasedCleanly = true;
  bool uiCleanedAcrossSwaps = true;
  bool debugCleanedAcrossSwaps = true;
  double warmupLuaKb = 0.0;

  const auto SwapToScene = [&](const std::string& path) {
    loader.RequestLoadScene(path);
    loader.FlushPendingSceneLoad();
    ++transitions;
  };

  for (int cycle = 0; cycle < kFullCycles; ++cycle) {
    // 1. Scene A
    lua["stress_iter"] = transitions + 1;
    lua["current_expected_scene"] = "a";
    SwapToScene("scenes/scene_a.lua");
    if (cycle > 0) {
      if (CountMountedViews(uiRuntime) != 0) uiCleanedAcrossSwaps = false;
      if (CountLuaOwnedDebugEntries(debugReg) != 0) debugCleanedAcrossSwaps = false;
      if (am.RefCount("tex_c") != 0 || am.GetTexture("tex_c") != nullptr) exclusiveReleasedCleanly = false;
    }
    SDL_Texture* sharedBeforeSwap = am.GetTexture("shared_tex");
    RunFrameTick(reg);

    // 2. Scene B (shares shared_tex + hud-16 with Scene A; drops tex_a, acquires tex_b)
    lua["stress_iter"] = transitions + 1;
    lua["current_expected_scene"] = "b";
    SwapToScene("scenes/scene_b.lua");
    if (am.RefCount("shared_tex") != 1 || am.GetTexture("shared_tex") != sharedBeforeSwap) {
      sharedPointerStable = false;
    }
    if (am.RefCount("tex_a") != 0 || am.GetTexture("tex_a") != nullptr || am.RefCount("tex_b") != 1) {
      exclusiveReleasedCleanly = false;
    }
    RunFrameTick(reg);

    // Every 10th cycle, also exercise ReloadScene + FlushPendingSceneLoad.
    if (cycle % 10 == 9) {
      loader.ReloadScene();
      loader.FlushPendingSceneLoad();
      ++transitions;
      RunFrameTick(reg);
    }

    // 3. Scene C (UI mount + prop_spawner + Lua debug watch/action; drops shared_tex & tex_b)
    lua["stress_iter"] = transitions + 1;
    lua["current_expected_scene"] = "c";
    SwapToScene("scenes/scene_c.lua");
    if (am.RefCount("shared_tex") != 0 || am.RefCount("tex_b") != 0 || am.RefCount("tex_c") != 1) {
      exclusiveReleasedCleanly = false;
    }
    RunFrameTick(reg);

    if (cycle == 3) {
      lua.script("collectgarbage('collect'); collectgarbage('collect')");
      warmupLuaKb = lua.script("return collectgarbage('count')").get<double>();
    }
  }

  Check(transitions >= 120, "executed 120+ scene load/reload transitions");
  Check(sharedPointerStable, "shared_tex stayed resident at refcount 1 without SDL_Texture* churn across A->B swaps");
  Check(exclusiveReleasedCleanly, "scene-exclusive textures released to refcount 0 immediately on every swap");
  Check(uiCleanedAcrossSwaps, "mounted OXML UI views unmounted cleanly on every scene swap");
  Check(debugCleanedAcrossSwaps, "Lua-owned debug watches and actions cleared on every scene swap");
  CheckEq(lua["stale_spawner_hits"].get<int>(), 0, "TilemapSystem prop_spawner never leaked into subsequent scenes");

  // Explicit StopScene teardown and post-stress leak audit.
  loader.StopScene();
  loader.FlushPendingSceneLoad();
  RunFrameTick(reg);
  lua.script("collectgarbage('collect'); collectgarbage('collect')");

  const double finalLuaKb = lua.script("return collectgarbage('count')").get<double>();
  const int liveWeakMarkers =
      lua.script("local n = 0; for _, v in pairs(stress_weak_tracker) do if v ~= nil then n = n + 1 end end; return n")
          .get<int>();

  Check(!loader.IsSceneRunning(), "StopScene marks scene not running");
  CheckEq(reg.GetUserEntityCount(), std::size_t{0}, "zero user entities remain after StopScene");
  CheckEq(am.RefCount("shared_tex"), 0, "shared_tex refcount is 0 after StopScene");
  CheckEq(am.RefCount("tex_a"), 0, "tex_a refcount is 0 after StopScene");
  CheckEq(am.RefCount("tex_b"), 0, "tex_b refcount is 0 after StopScene");
  CheckEq(am.RefCount("tex_c"), 0, "tex_c refcount is 0 after StopScene");
  CheckEq(am.RefCount("hud-16"), 0, "hud-16 font refcount is 0 after StopScene (no UI view ref leak)");
  Check(am.GetTextures().empty(), "TextureStore is empty after StopScene");
  Check(am.GetFonts().empty(), "FontStore is empty after StopScene");
  CheckEq(spriteCache.ActiveEntryCount(), std::size_t{0}, "SpriteRenderCache has 0 active entries after StopScene");
  Check(spriteCache.SlotCapacity() <= 64U, "SpriteRenderCache slot capacity stayed bounded across 120+ swaps");
  if (const auto* textCache = reg.TryGet<TextRenderCache>()) {
    CheckEq(textCache->RetiredTextureCount(), std::size_t{0}, "TextRenderCache flushed all retired textures");
  }
  CheckEq(CountMountedViews(uiRuntime), std::size_t{0}, "zero mounted UI views remain after StopScene");
  CheckEq(CountLuaOwnedDebugEntries(debugReg), std::size_t{0}, "zero Lua-owned debug entries remain after StopScene");
  CheckEq(liveWeakMarkers, 0, "Lua GC collected all per-scene closures and marker tables");
  Check(finalLuaKb <= warmupLuaKb + 256.0, "Lua VM memory stayed bounded across 120+ swaps");
  reg.ValidateInvariants();
}

}  // namespace

int main() {
  Logger::Init();
  SDL_Init(0);
  TTF_Init();

  const std::filesystem::path fixtureDir = std::filesystem::temp_directory_path() / "octarine_scene_swap_stress";
  std::error_code ec;
  std::filesystem::remove_all(fixtureDir, ec);

  SDL_Surface* surf = SDL_CreateSurface(640, 360, SDL_PIXELFORMAT_RGBA32);
  SDL_Renderer* sdlRenderer = surf != nullptr ? SDL_CreateSoftwareRenderer(surf) : nullptr;
  Check(sdlRenderer != nullptr, "headless software SDL_Renderer created");

  if (sdlRenderer != nullptr && CreateStressFixtureProject(fixtureDir)) {
    // Declare sol::state before Registry so ECS/singleton Lua references unwind while lua_State* is live.
    sol::state lua;
    auto eventBus = std::make_unique<EventBus>();
    Registry registry;
    SceneLoader loader(&registry, lua);
    HeadlessSceneBindingContext bindingCtx(&registry, &loader, sdlRenderer, fixtureDir.string());

    SetupHeadlessHarness(registry, lua, bindingCtx, eventBus, sdlRenderer, fixtureDir);
    RunSceneSwapStressCycles(registry, lua, loader);

    if (auto* uiRuntime = registry.TryGet<UIRuntime>()) {
      uiRuntime->UnmountAllViews();
    }
    if (auto* textCache = registry.TryGet<TextRenderCache>()) {
      textCache->DestroyImmediate();
    }
    registry.Get<AssetManager>().ClearAssets();
    lua_pushnil(lua.lua_state());
    lua_setfield(lua.lua_state(), LUA_REGISTRYINDEX, "octarine_assets");
  } else {
    Check(false, "failed to create stress test fixture project");
  }

  if (sdlRenderer != nullptr) {
    SDL_DestroyRenderer(sdlRenderer);
  }
  if (surf != nullptr) {
    SDL_DestroySurface(surf);
  }
  std::filesystem::remove_all(fixtureDir, ec);
  TTF_Quit();
  SDL_Quit();
  return octarine::test::ReportSummary("OctarineSceneSwapStressTest");
}
