#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sol/sol.hpp>
#include <string>

#include "AssetManager/AssetManager.h"
#include "Audio/AudioGainRegistry.h"
#include "Components/CameraComponents.h"
#include "Components/SpriteComponent.h"
#include "Components/TextLabelComponent.h"
#include "Components/UINineSliceComponent.h"
#include "ECS/Registry.h"
#include "Engine/EngineBootstrap.h"
#include "EventBus/EventBus.h"
#include "Events/AudioPlayEvent.h"
#include "Game/EngineContext.h"
#include "Game/Game.h"
#include "Game/GameConfig.h"
#include "General/FileSandbox.h"
#include "General/Logger.h"
#include "Lua/Bindings/RegisterAllBindings.h"
#include "Lua/Modules/RegisterAllModules.h"
#include "Renderer/SpriteRenderCache.h"
#include "Systems/ScriptSystem.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

bool RunLua(sol::state& lua, const std::string& script) {
  const auto r = lua.safe_script(script, sol::script_pass_on_error);
  if (!r.valid()) {
    const sol::error err = r;
    std::cerr << "    lua error: " << err.what() << "\n";
    return false;
  }
  return true;
}

void TestLogModule(sol::state& lua) {
  std::cout << "[Lua: log module]\n";
  Check(RunLua(lua, R"LUA(
    log("Test log info message")
    log_w("Test log warn message")
    log_e("Test log error message")
    print("Test print message")
  )LUA"),
        "log/log_w/log_e/print execute cleanly");

  const auto history = Logger::GetHistory();
  bool foundInfo = false;
  bool foundWarn = false;
  bool foundError = false;
  bool foundPrint = false;
  for (const auto& entry : history) {
    if (entry.find("Test log info message") != std::string::npos) foundInfo = true;
    if (entry.find("Test log warn message") != std::string::npos) foundWarn = true;
    if (entry.find("Test log error message") != std::string::npos) foundError = true;
    if (entry.find("Test print message") != std::string::npos) foundPrint = true;
  }
  Check(foundInfo, "Logger captured info message from Lua");
  Check(foundWarn, "Logger captured warn message from Lua");
  Check(foundError, "Logger captured error message from Lua");
  Check(foundPrint, "Logger captured print message from Lua");
}

void TestStorageModule(sol::state& lua) {
  std::cout << "[Lua: storage module]\n";
  // Raw string write and read
  Check(RunLua(lua, R"LUA(
    w_ok = storage.write("test_file.txt", "hello octarine storage")
    r_content = storage.read("test_file.txt")
    ex_before = storage.exists("test_file.txt")
  )LUA"),
        "storage.write / read / exists execute");

  CheckEq(lua["w_ok"].get<bool>(), true, "storage.write returned true");
  CheckEq(lua["r_content"].get<std::string>(), "hello octarine storage", "storage.read matched written content");
  CheckEq(lua["ex_before"].get<bool>(), true, "storage.exists true for written file");

  // Table write and read
  Check(RunLua(lua, R"LUA(
    t_save = {
      score = 42000,
      hero = "Knight",
      unlocked = true,
      stats = { hp = 100, mp = 50 }
    }
    wt_ok = storage.write_table("save_data.lua", t_save)
    rt_table = storage.read_table("save_data.lua")
  )LUA"),
        "storage.write_table / read_table execute");

  CheckEq(lua["wt_ok"].get<bool>(), true, "storage.write_table succeeded");
  Check(RunLua(lua, R"LUA(
    rt_score = rt_table.score
    rt_hero = rt_table.hero
    rt_unlocked = rt_table.unlocked
    rt_hp = rt_table.stats.hp
  )LUA"),
        "read table keys");
  CheckEq(lua["rt_score"].get<int>(), 42000, "table score preserved");
  CheckEq(lua["rt_hero"].get<std::string>(), "Knight", "table hero preserved");
  CheckEq(lua["rt_unlocked"].get<bool>(), true, "table boolean preserved");
  CheckEq(lua["rt_hp"].get<int>(), 100, "nested table field preserved");

  // Listing files
  Check(RunLua(lua, R"LUA(
    list_files = storage.list()
    found_save = false
    for i = 1, #list_files do
      if list_files[i] == "save_data.lua" then found_save = true end
    end
  )LUA"),
        "storage.list contains created files");
  CheckEq(lua["found_save"].get<bool>(), true, "save_data.lua found in storage.list()");

  // File removal
  Check(RunLua(lua, R"LUA(
    rem_ok = storage.remove("test_file.txt")
    ex_after = storage.exists("test_file.txt")
  )LUA"),
        "storage.remove executes");
  CheckEq(lua["rem_ok"].get<bool>(), true, "storage.remove succeeded");
  CheckEq(lua["ex_after"].get<bool>(), false, "storage.exists false after removal");

  // Clean up save_data.lua
  RunLua(lua, "storage.remove('save_data.lua')");
}

void TestStorageSandboxing(sol::state& lua) {
  std::cout << "[Lua: storage sandboxing]\n";
  // Attempting directory traversal via ../.. should be rejected
  Check(RunLua(lua, R"LUA(
    traverse_write = storage.write("../../../../tmp/escape.txt", "pwn")
    traverse_read = storage.read("../../../../etc/passwd")
  )LUA"),
        "directory traversal commands execute without crashing");

  CheckEq(lua["traverse_write"].get<bool>(), false, "traversal write rejected");
  Check(lua["traverse_read"] == sol::lua_nil, "traversal read returns nil");
}

void TestGameModule(sol::state& lua, Game& game) {
  std::cout << "[Lua: game & entity module]\n";
  Check(RunLua(lua, R"LUA(
    init_p = registry.is_paused()
    registry.set_paused(true)
    during_p = registry.is_paused()
    registry.set_paused(false)
    after_p = registry.is_paused()

    set_fixed_dt(0.01)
    read_dt = get_fixed_dt()

    set_master_volume(0.75)
    read_vol = get_master_volume()

    set_audio_muted(true)
    muted_during = is_audio_muted()
    set_audio_muted(false)
    muted_after = is_audio_muted()

    set_deterministic(true)
    is_det = is_deterministic()
    set_deterministic(false)

    fps_val = get_fps()
    ent_cnt = get_entity_count()
  )LUA"),
        "game / registry module functions execute");

  CheckEq(lua["init_p"].get<bool>(), false, "game initially not paused");
  CheckEq(lua["during_p"].get<bool>(), true, "game paused after registry.set_paused(true)");
  CheckEq(lua["after_p"].get<bool>(), false, "game unpaused after registry.set_paused(false)");
  CheckEq(game.GetRegistry()->IsPaused(), false, "C++ registry syncs pause state");
  Check(std::abs(lua["read_dt"].get<double>() - 0.01) < 1e-4, "fixed dt set/get round-trip");
  Check(std::abs(lua["read_vol"].get<float>() - 0.75f) < 1e-4f, "master volume set/get round-trip");
  CheckEq(lua["muted_during"].get<bool>(), true, "audio muted set/get");
  CheckEq(lua["muted_after"].get<bool>(), false, "audio unmuted set/get");
  CheckEq(lua["is_det"].get<bool>(), true, "deterministic mode toggle");
}

void TestIoModule(sol::state& lua) {
  std::cout << "[Lua: io module]\n";
  // Test reading non-existent file returns empty vector/collection without crash
  Check(RunLua(lua, R"LUA(
    missing_lines = read_file_lines("nonexistent_test_file.txt")
    len_zero = (#missing_lines == 0)
  )LUA"),
        "read_file_lines on missing file returns empty container");

  CheckEq(lua["len_zero"].get<bool>(), true, "empty container for missing file");
}

void TestEntityAndUiHelpers(sol::state& lua, Game& game) {
  std::cout << "[Lua: entity/sprite/ui helpers]\n";
  auto* reg = game.GetRegistry();
  auto& spriteCache = reg->Set<SpriteRenderCache>(SpriteRenderCache());

  const Entity parent = reg->CreateEntity();
  const Entity childA = reg->CreateEntity();
  const Entity childB = reg->CreateEntity();
  reg->SetParent(childA, parent);
  reg->SetParent(childB, parent);

  // Seed a text label and a sprite with a cached texture handle.
  TextLabelComponent label;
  label.fontId = "font_default";
  reg->AddComponent(parent, label);
  reg->AddComponent(parent, SpriteComponent("tex_old", 32.0F, 32.0F, 0, true, 1.0F, 2.0F));
  auto* fakeTex = reinterpret_cast<SDL_Texture*>(static_cast<std::uintptr_t>(0x1234));
  spriteCache.Store(parent, fakeTex, SDL_FRect{0.0F, 0.0F, 32.0F, 32.0F}, 1);

  lua["e_parent"] = parent;
  lua["e_child_a"] = childA;
  lua["e_child_b"] = childB;

  Check(RunLua(lua, R"LUA(
    f_before = get_text_label_font(e_parent)
    set_text_label_font(e_parent, "font_bold")
    f_after = get_text_label_font(e_parent)

    set_sprite_world_size(e_parent, 64, 48)
    set_sprite_region(e_parent, "tex_new", 10, 20, 30, 40)
    rx, ry, rw, rh = get_sprite_src_rect(e_parent)

    -- Attaching SpriteComponent when missing
    set_sprite_region(e_child_a, "tex_attached", 4, 8, 16, 24)
    ax, ay, aw, ah = get_sprite_src_rect(e_child_a)

    -- UINineSliceComponent attach + update
    set_ui_nine_slice(e_parent, 4, 5, 6, 7)
    set_ui_nine_slice(e_parent, 8, 9, 10, 11)

    kids = get_entity_children(e_parent)
    kids_count = #kids
  )LUA"),
        "entity/sprite/ui helper bindings execute");

  CheckEq(lua["f_before"].get<std::string>(), "font_default", "get_text_label_font reads initial font");
  CheckEq(lua["f_after"].get<std::string>(), "font_bold", "set_text_label_font updates font");
  Check(spriteCache.Lookup(parent, 1) == nullptr, "set_sprite_region invalidates SpriteRenderCache on assetId change");
  CheckEq(reg->GetComponent<SpriteComponent>(parent).assetId, "tex_new", "set_sprite_region updates assetId");
  Check(std::abs(reg->GetComponent<SpriteComponent>(parent).width - 64.0F) < 1e-4F, "set_sprite_world_size width");
  Check(std::abs(reg->GetComponent<SpriteComponent>(parent).height - 48.0F) < 1e-4F, "set_sprite_world_size height");
  Check(std::abs(lua["rx"].get<float>() - 10.0F) < 1e-4F && std::abs(lua["ry"].get<float>() - 20.0F) < 1e-4F &&
            std::abs(lua["rw"].get<float>() - 30.0F) < 1e-4F && std::abs(lua["rh"].get<float>() - 40.0F) < 1e-4F,
        "get_sprite_src_rect returns updated region");
  Check(reg->HasComponent<SpriteComponent>(childA), "set_sprite_region attaches SpriteComponent when missing");
  Check(std::abs(lua["ax"].get<float>() - 4.0F) < 1e-4F && std::abs(lua["aw"].get<float>() - 16.0F) < 1e-4F,
        "set_sprite_region attached srcRect matches");
  Check(reg->HasComponent<UINineSliceComponent>(parent), "set_ui_nine_slice attaches component");
  const auto& ns = reg->GetComponent<UINineSliceComponent>(parent);
  Check(std::abs(ns.left - 8.0F) < 1e-4F && std::abs(ns.top - 9.0F) < 1e-4F && std::abs(ns.right - 10.0F) < 1e-4F &&
            std::abs(ns.bottom - 11.0F) < 1e-4F,
        "set_ui_nine_slice updates margins");
  CheckEq(lua["kids_count"].get<int>(), 2, "get_entity_children returns all children");
}

void TestPlatformAndUrlValidation(sol::state& lua, Game& game) {
  std::cout << "[Lua: platform, time, catalog & url validation]\n";
  auto* reg = game.GetRegistry();
  reg->Set<AssetManager>(AssetManager());
  reg->Get<GameConfig>().GetEngineOptions().headless = true;

  Check(RunLua(lua, R"LUA(
    sys_t = get_system_time()
    ts_str = get_timestamp_string()
    has_missing = has_catalog_asset("does_not_exist")
    has_empty = has_catalog_asset("")

    -- URL validation: reject http, short, control chars, whitespace, and headless valid https
    u_http = open_external_url("http://example.com")
    u_short = open_external_url("https://a")
    u_space = open_external_url("https://example.com/bad path")
    u_ctrl = open_external_url("https://example.com/\nfoo")
    u_headless_https = open_external_url("https://example.com/ok")

    -- Clipboard validation: reject empty, >1024 bytes, and headless valid text
    c_empty = set_clipboard_text("")
    c_huge = set_clipboard_text(string.rep("x", 1025))
    c_headless = set_clipboard_text("hello")

    shipped_flag = oct_shipped
    shipped_fn = is_shipped_build()
    verify_failed = has_verify_failed()
  )LUA"),
        "platform/time/url/clipboard/shipped bindings execute");

  Check(lua["sys_t"].get<double>() > 1e9, "get_system_time returns epoch seconds");
  CheckEq(lua["ts_str"].get<std::string>().size(), static_cast<std::size_t>(15),
          "get_timestamp_string returns YYYYMMDD_HHMMSS");
  CheckEq(lua["has_missing"].get<bool>(), false, "has_catalog_asset false for missing id");
  CheckEq(lua["has_empty"].get<bool>(), false, "has_catalog_asset false for empty id");
  CheckEq(lua["u_http"].get<bool>(), false, "open_external_url rejects http://");
  CheckEq(lua["u_short"].get<bool>(), false, "open_external_url rejects <10 chars");
  CheckEq(lua["u_space"].get<bool>(), false, "open_external_url rejects spaces");
  CheckEq(lua["u_ctrl"].get<bool>(), false, "open_external_url rejects control chars");
  CheckEq(lua["u_headless_https"].get<bool>(), false, "open_external_url blocked in headless mode");
  CheckEq(lua["c_empty"].get<bool>(), false, "set_clipboard_text rejects empty string");
  CheckEq(lua["c_huge"].get<bool>(), false, "set_clipboard_text rejects >1024 bytes");
  CheckEq(lua["c_headless"].get<bool>(), false, "set_clipboard_text blocked in headless mode");
  CheckEq(lua["shipped_flag"].get<bool>(), false, "oct_shipped is false in non-shipped test");
  CheckEq(lua["shipped_fn"].get<bool>(), false, "is_shipped_build() is false in non-shipped test");
  CheckEq(lua["verify_failed"].get<bool>(), false, "has_verify_failed() starts false");
}

void TestHotReloadTrackerBindings(sol::state& lua, Game& game) {
  std::cout << "[Lua: hot_reload.* bindings]\n";
  const auto tempDir = std::filesystem::temp_directory_path() / "octarine_hot_reload_mod_test";
  std::error_code ec;
  std::filesystem::create_directories(tempDir, ec);
  const auto scriptPath = tempDir / "tracked.lua";
  {
    std::ofstream f(scriptPath, std::ios::trunc);
    f << "return 1\n";
  }

  game.GetRegistry()->Get<GameConfig>().GetEngineOptions().hotReloadPollSeconds = 0.25F;
  lua["tracked_path"] = scriptPath.string();

  Check(RunLua(lua, R"LUA(
    hr_on = set_hot_reload_enabled(true)
    hr_check = is_hot_reload_enabled()
    hot_reload.track_script(tracked_path)
    cnt_1 = hot_reload.get_tracked_count()
    poll_none = hot_reload.poll_scripts(0.0)
    poll_none_len = #poll_none
  )LUA"),
        "hot_reload track and initial poll execute");

  CheckEq(lua["hr_on"].get<bool>(), true, "set_hot_reload_enabled(true) returns true");
  CheckEq(lua["hr_check"].get<bool>(), true, "is_hot_reload_enabled() reflects enabled");
  CheckEq(lua["cnt_1"].get<std::size_t>(), static_cast<std::size_t>(1), "hot_reload.get_tracked_count() == 1");
  CheckEq(lua["poll_none_len"].get<int>(), 0, "no changes before mtime bump");

  // Advance file mtime so FileWatcher detects modification deterministically.
  const auto oldTime = std::filesystem::last_write_time(scriptPath, ec);
  std::filesystem::last_write_time(scriptPath, oldTime + std::chrono::seconds(2), ec);

  Check(RunLua(lua, R"LUA(
    poll_early = hot_reload.poll_scripts(0.1)
    poll_early_len = #poll_early
    poll_ready = hot_reload.poll_scripts(0.2)
    poll_ready_len = #poll_ready
    poll_ready_path = poll_ready[1] or ""
    hot_reload.untrack_script(tracked_path)
    cnt_0 = hot_reload.get_tracked_count()
    set_hot_reload_enabled(false)
  )LUA"),
        "hot_reload poll interval gating + change detection + untrack execute");

  CheckEq(lua["poll_early_len"].get<int>(), 0, "poll_scripts respects pollInterval accumulator");
  CheckEq(lua["poll_ready_len"].get<int>(), 1, "poll_scripts detects modified file once interval elapses");
  const auto expectedCanonPath = std::filesystem::weakly_canonical(scriptPath, ec).string();
  CheckEq(lua["poll_ready_path"].get<std::string>(), expectedCanonPath, "poll_scripts returns modified path");
  CheckEq(lua["cnt_0"].get<std::size_t>(), static_cast<std::size_t>(0), "untrack_script drops count to 0");

  std::filesystem::remove_all(tempDir, ec);
}

void TestAudioGainRegistryBindings(sol::state& lua, Game& game) {
  std::cout << "[Lua: AudioGainRegistry bindings]\n";
  EventBus bus;
  auto* reg = game.GetRegistry();
  auto& gainReg = reg->Set<octarine::AudioGainRegistry>(octarine::AudioGainRegistry{});
  gainReg.Subscribe(bus);

  struct VolumeObserver {
    float lastVolume = -1.0F;
    void OnPlay(const AudioPlayEvent& ev) { lastVolume = ev.volume; }
  } observer;
  auto obsSub = bus.SubscribeEvent<VolumeObserver, AudioPlayEvent>(&observer, &VolumeObserver::OnPlay);

  Check(RunLua(lua, R"LUA(
    set_audio_cue_gain("ui_click", 0.5)
    set_native_sfx_volume(0.8, false)
  )LUA"),
        "set_audio_cue_gain and set_native_sfx_volume execute");

  // Unscaled event (volume == 1.0f) should be scaled to cue_gain * sfx_volume = 0.4f.
  bus.EmitEvent<AudioPlayEvent>("ui_click", 1.0F);
  Check(std::abs(observer.lastVolume - 0.4F) < 1e-4F, "AudioGainRegistry scales unscaled cue by gain * sfx_volume");

  // Pre-scaled event (volume != 1.0f) should pass through untouched.
  bus.EmitEvent<AudioPlayEvent>("ui_click", 0.25F);
  Check(std::abs(observer.lastVolume - 0.25F) < 1e-4F, "AudioGainRegistry preserves pre-scaled event volume");

  // Muted state zeroes all AudioPlayEvents.
  Check(RunLua(lua, "set_native_sfx_volume(0.8, true)"), "set_native_sfx_volume(muted=true) executes");
  bus.EmitEvent<AudioPlayEvent>("ui_click", 1.0F);
  Check(std::abs(observer.lastVolume - 0.0F) < 1e-4F, "AudioGainRegistry zeroes volume when muted");

  // ENG-LUA-11: Unregistered cues and explicit-volume Lua play_sound honor sfx_volume when unmuted.
  Check(RunLua(lua, "set_native_sfx_volume(0.5, false)"), "set_native_sfx_volume(0.5, false) executes");
  bus.EmitEvent<AudioPlayEvent>("unregistered_cue", 1.0F, true);
  Check(std::abs(observer.lastVolume - 0.5F) < 1e-4F, "Unregistered cue honors master sfx_volume");

  bus.EmitEvent<AudioPlayEvent>("ui_click", 0.6F, true);
  Check(std::abs(observer.lastVolume - 0.15F) < 1e-4F, "Explicit Lua volume scales by cue_gain * sfx_volume");

  bus.EmitEvent<AudioPlayEvent>("unregistered_cue", 0.6F, true);
  Check(std::abs(observer.lastVolume - 0.30F) < 1e-4F, "Explicit Lua volume on unregistered cue scales by sfx_volume");

  // Also verify Lua play_sound() directly through EngineContext.eventBus + AudioGainRegistry.
  {
    EngineContext ectx{};
    ectx.eventBus = &bus;
    reg->Set<EngineContext>(ectx);
    Check(RunLua(lua, R"LUA(
      set_native_sfx_volume(0.5, false)
      play_sound("unregistered_lua_sfx", 0.4)
      set_audio_clip_limits("sfx_rapid", 3, 0.08)
    )LUA"),
          "play_sound and set_audio_clip_limits execute");
    Check(std::abs(observer.lastVolume - 0.20F) < 1e-4F, "Lua play_sound(id, 0.4) scales by sfx_volume (0.5) -> 0.20");
    CheckEq(gainReg.clip_limits["sfx_rapid"].maxVoices, 3, "set_audio_clip_limits sets maxVoices");
    Check(std::abs(gainReg.clip_limits["sfx_rapid"].minIntervalSec - 0.08) < 1e-6,
          "set_audio_clip_limits sets minIntervalSec");
  }
}

void TestSandboxedIoDofileImportTagsAndXy(sol::state& lua, Game& game) {
  std::cout << "[Lua: sandboxed read_file_lines, dofile text-mode, import cache, tags & _xy getters]\n";
  const auto tempProj = std::filesystem::temp_directory_path() / "octarine_lua_batch_d_proj";
  const auto outsideFile = std::filesystem::temp_directory_path() / "octarine_outside_secret.lua";
  std::error_code ec;
  std::filesystem::remove_all(tempProj, ec);
  std::filesystem::create_directories(tempProj / "data", ec);
  std::filesystem::create_directories(tempProj / "scripts", ec);

  {
    std::ofstream f(tempProj / "config.ini", std::ios::trunc);
    f << "[Game]\nTitle = TestProj\nStartupScript = scripts/mod_counter.lua\n";
  }
  {
    std::ofstream f(tempProj / "data" / "lines.txt", std::ios::binary | std::ios::trunc);
    f << "alpha\nbeta\r\ngamma\n";
  }
  {
    std::ofstream f(tempProj / "scripts" / "mod_counter.lua", std::ios::trunc);
    f << "_G.mod_load_count = (_G.mod_load_count or 0) + 1\nreturn { version = _G.mod_load_count }\n";
  }
  {
    std::ofstream f(tempProj / "scripts" / "cyc_a.lua", std::ios::trunc);
    f << "return import('scripts/cyc_b')\n";
  }
  {
    std::ofstream f(tempProj / "scripts" / "cyc_b.lua", std::ios::trunc);
    f << "return import('scripts/cyc_a')\n";
  }
  {
    std::ofstream f(tempProj / "scripts" / "evil_bytecode.lua", std::ios::binary | std::ios::trunc);
    f.write("\x1bLua\x55\x00\x01\x02", 8);
  }
  {
    std::ofstream f(outsideFile, std::ios::trunc);
    f << "return 'escaped'\n";
  }

  auto* reg = game.GetRegistry();
  if (reg->TryGet<EngineContext>() == nullptr) {
    reg->Set<EngineContext>(EngineContext{});
  }
  engine_bootstrap::InstallLuaLibraries(lua);
  engine_bootstrap::InstallCoreSingletons(*reg, reg->Get<EngineContext>(), 800, 600, false, &lua);
  GameConfig projCfg;
  projCfg.LoadConfigFromFile(tempProj.string());
  reg->Set<GameConfig>(projCfg);
  reg->Get<AssetManager>().LoadGameConfig(projCfg);
  reg->Get<CameraComponent>().viewport.x = 320.0F;
  reg->Get<CameraComponent>().viewport.y = 180.0F;

  Check(RunLua(lua, R"LUA(
    ok_lines = read_file_lines("data/lines.txt")
    prefixed_lines = read_file_lines(get_asset_path("data/lines.txt"))
    escaped_rel = read_file_lines("../octarine_outside_secret.lua")
    escaped_abs = read_file_lines("/etc/hosts")

    df_res = dofile("scripts/mod_counter.lua")
    df_escape_ok = pcall(dofile, "../octarine_outside_secret.lua")
    df_bin_ok = pcall(dofile, "scripts/evil_bytecode.lua")

    _G.mod_load_count = 0
    m1 = import("scripts/mod_counter")
    m2 = import("scripts/mod_counter.lua")
    imp_same = rawequal(m1, m2)
    imp_count_after_cached = _G.mod_load_count
    imp_cyc_ok, imp_cyc_err = pcall(import, "scripts/cyc_a")
    imp_bin_ok = pcall(import, "scripts/evil_bytecode.lua")
    clear_import_cache("scripts/mod_counter")
    m3 = import("scripts/mod_counter")
    imp_count_after_clear = _G.mod_load_count

    t_ent = load_entity({ name = "UniqueLookupTarget" })
    found_ent = find_entity_by_name("UniqueLookupTarget")
    found_matches = (found_ent ~= nil and found_ent:get_id() == t_ent:get_id())

    tag_before = has_tag(t_ent, "elite_unit")
    add_tag(t_ent, "elite_unit")
    tag_during = has_tag(t_ent, "elite_unit") and registry.has_tag(t_ent, "elite_unit")
    remove_tag(t_ent, "elite_unit")
    tag_after = has_tag(t_ent, "elite_unit")

    cam_x, cam_y = get_camera_position_xy()
    map_w, map_h = get_game_map_dimensions_xy()

    print(setmetatable({}, { __tostring = function() return "safe_custom_tostring" end }))
    bad_print_ok = pcall(function()
      print(setmetatable({}, { __tostring = function() error("tostring_boom") end }))
    end)
  )LUA"),
        "sandboxed IO, dofile, import, tags, _xy getters, and safe print execute");

  sol::table okLines = lua["ok_lines"];
  sol::table prefLines = lua["prefixed_lines"];
  CheckEq(okLines.size(), static_cast<std::size_t>(3), "read_file_lines reads 3 lines from project-relative file");
  CheckEq(okLines[2].get<std::string>(), std::string("beta"), "read_file_lines strips CRLF cleanly");
  CheckEq(prefLines.size(), static_cast<std::size_t>(3), "read_file_lines accepts get_asset_path(...) path");
  CheckEq(lua["escaped_rel"].get<sol::table>().size(), static_cast<std::size_t>(0),
          "read_file_lines rejects ../ path traversal");
  CheckEq(lua["escaped_abs"].get<sol::table>().size(), static_cast<std::size_t>(0),
          "read_file_lines rejects host absolute paths outside project root");

  CheckEq(lua["df_escape_ok"].get<bool>(), false, "dofile rejects ../ path traversal");
  CheckEq(lua["df_bin_ok"].get<bool>(), false, "dofile rejects unscrambled binary bytecode in text mode");

  CheckEq(lua["imp_same"].get<bool>(), true, "import caches module across extension-less and .lua paths");
  CheckEq(lua["imp_count_after_cached"].get<int>(), 1, "cached import executes chunk once");
  CheckEq(lua["imp_cyc_ok"].get<bool>(), false, "circular import fails cleanly without stack overflow");
  Check(lua["imp_cyc_err"].get<std::string>().find("circular") != std::string::npos,
        "circular import reports circular dependency");
  CheckEq(lua["imp_bin_ok"].get<bool>(), false, "import rejects binary bytecode");
  CheckEq(lua["imp_count_after_clear"].get<int>(), 2, "clear_import_cache evicts cached module");

  // Verify hot_reload.poll_scripts evicts modified scripts from import cache automatically.
  const auto modScript = tempProj / "scripts" / "mod_counter.lua";
  reg->Get<GameConfig>().GetEngineOptions().hotReloadPollSeconds = 0.05F;
  lua["mod_disk_path"] = modScript.string();
  Check(RunLua(lua, "set_hot_reload_enabled(true); hot_reload.track_script(mod_disk_path)"), "track mod_counter.lua");
  const auto prevMtime = std::filesystem::last_write_time(modScript, ec);
  std::filesystem::last_write_time(modScript, prevMtime + std::chrono::seconds(3), ec);
  Check(RunLua(lua, R"LUA(
    hot_reload.poll_scripts(0.2)
    m4 = import("scripts/mod_counter")
    imp_count_after_hot_reload = _G.mod_load_count
    set_hot_reload_enabled(false)
  )LUA"),
        "poll_scripts evicts import cache entry on mtime change");
  CheckEq(lua["imp_count_after_hot_reload"].get<int>(), 3, "import reloads module after hot_reload.poll_scripts");

  CheckEq(lua["found_matches"].get<bool>(), true, "find_entity_by_name locates entity without GetUserEntities copy");
  CheckEq(lua["tag_before"].get<bool>(), false, "has_tag initially false");
  CheckEq(lua["tag_during"].get<bool>(), true, "add_tag attaches dynamic tag visible to global and registry.has_tag");
  CheckEq(lua["tag_after"].get<bool>(), false, "remove_tag removes dynamic tag");
  Check(std::abs(lua["cam_x"].get<float>() - 320.0F) < 1e-3F && std::abs(lua["cam_y"].get<float>() - 180.0F) < 1e-3F,
        "get_camera_position_xy returns camera x, y without vec2 allocation");
  Check(lua["map_w"].is<float>() && lua["map_h"].is<float>(), "get_game_map_dimensions_xy returns numeric pair");
  CheckEq(lua["bad_print_ok"].get<bool>(), true, "print survives throwing __tostring metamethod");

  std::filesystem::remove(outsideFile, ec);
  std::filesystem::remove_all(tempProj, ec);
}

}  // namespace

int main() {
  Logger::Init();

  // Set isolated temporary save directory for tests
  const std::string testSaveDir = (std::filesystem::temp_directory_path() / "octarine_test_save").string();
  std::filesystem::create_directories(testSaveDir);
#if defined(_WIN32)
  _putenv_s("OCTARINE_SAVE_PATH", testSaveDir.c_str());
#else
  setenv("OCTARINE_SAVE_PATH", testSaveDir.c_str(), 1);
#endif

  Game game;
  game.GetRegistry()->Set<GameConfig>(GameConfig{});
  sol::state& lua = game.GetLua();
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::io, sol::lib::string, sol::lib::table);

  RegisterAllLuaBindings();
  ScriptSystem scriptSystem;
  scriptSystem.CreateLuaBindings(lua);
  RegisterAllLuaModules(lua, game);

  TestLogModule(lua);
  TestStorageModule(lua);
  TestStorageSandboxing(lua);
  TestGameModule(lua, game);
  TestIoModule(lua);
  TestEntityAndUiHelpers(lua, game);
  TestPlatformAndUrlValidation(lua, game);
  TestHotReloadTrackerBindings(lua, game);
  TestAudioGainRegistryBindings(lua, game);
  TestSandboxedIoDofileImportTagsAndXy(lua, game);

  std::filesystem::remove_all(testSaveDir);
  return octarine::test::ReportSummary("OctarineLuaModulesTest");
}
