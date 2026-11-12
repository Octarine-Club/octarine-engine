#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sol/sol.hpp>
#include <string>
#include <vector>

#include "Components/CameraComponents.h"
#include "ECS/Registry.h"
#include "Game/EngineContext.h"
#include "Game/Game.h"
#include "Game/GameConfig.h"
#include "Game/UserSettings.h"
#include "General/Logger.h"
#include "Lua/LuaBindingContext.h"
#include "Lua/Modules/GameModuleLuaBinding.h"
#include "Renderer/Renderer.h"

namespace {

int g_failures = 0;

void SetProcessEnvVar(const char* name, const char* value) {
#ifdef _MSC_VER
  _putenv_s(name, value != nullptr ? value : "");
#else
  if (value != nullptr && value[0] != '\0') {
    setenv(name, value, 1);
  } else {
    unsetenv(name);
  }
#endif
}

void Check(const bool condition, const std::string& what) {
  if (condition) {
    std::cout << "  ok   " << what << "\n";
  } else {
    std::cerr << "  FAIL " << what << "\n";
    ++g_failures;
  }
}

class FakeSettingsHost : public LuaBindingContext {
 public:
  explicit FakeSettingsHost(std::string saveRoot) : save_root_(std::move(saveRoot)) {
    registry_.Set<GameConfig>(GameConfig{});
    registry_.Set<octarine::UserSettings>(octarine::UserSettings{});
    context_.config = &registry_.Get<GameConfig>();
    context_.userSettings = &registry_.Get<octarine::UserSettings>();
    registry_.Set<EngineContext>(context_);
  }

  void SetRenderer(SDL_Renderer* renderer) {
    sdl_renderer_ = renderer;
    registry_.Get<EngineContext>().sdlRenderer = renderer;
  }

  [[nodiscard]] Registry* GetRegistry() const override { return &registry_; }
  [[nodiscard]] SDL_Renderer* GetRenderer() const override { return sdl_renderer_; }
  [[nodiscard]] EngineContext& GetContext() override { return registry_.Get<EngineContext>(); }

  void LoadScene(const std::string& /*scenePath*/) override {}
  void ReloadScene() override {}
  void StopScene() override {}
  void TrackSceneAssets(const std::vector<std::string>& /*assetIds*/) override {}

  [[nodiscard]] bool IsBakeMode() const override { return false; }
  void RecordBakeValidationFailures(int /*failures*/) override {}

  [[nodiscard]] std::string GetProjectPath() const override { return {}; }
  std::string GetSaveDataPath() override { return save_root_; }

  void RequestQuit() override {}

 private:
  mutable Registry registry_{};
  EngineContext context_{};
  SDL_Renderer* sdl_renderer_ = nullptr;
  std::string save_root_;
};

void TestDefaults() {
  std::cout << "[defaults] UserSettings default construction\n";
  octarine::UserSettings s;
  Check(s.windowX == -1, "default windowX is -1");
  Check(s.windowY == -1, "default windowY is -1");
  Check(s.windowWidth == 0, "default windowWidth is 0");
  Check(s.windowHeight == 0, "default windowHeight is 0");
  Check(s.displayIndex == -1, "default displayIndex is -1");
  Check(s.displayName.empty(), "default displayName is empty");
  Check(!s.fullscreen, "default fullscreen is false");
  Check(!s.maximized, "default maximized is false");
  Check(!s.hasWindowPlacement, "default hasWindowPlacement is false");
  Check(!s.windowMode.has_value(), "default windowMode is nullopt");
  Check(s.GetEffectiveWindowMode() == WindowMode::Windowed, "default effective WindowMode is Windowed");
  Check(s.fullscreenWidth == 0, "default fullscreenWidth is 0");
  Check(s.fullscreenHeight == 0, "default fullscreenHeight is 0");
  Check(s.fullscreenRefreshRate == 0.0F, "default fullscreenRefreshRate is 0.0");
  Check(!s.presentationMode.has_value(), "default presentationMode is nullopt");
  Check(!s.vsync.has_value(), "default vsync is nullopt");
  Check(!s.windowResizable.has_value(), "default windowResizable is nullopt");
  Check(s.masterVolume == 1.0F, "default masterVolume is 1.0");
  Check(!s.audioMuted, "default audioMuted is false");
  Check(!s.fpsTarget.has_value(), "default fpsTarget is nullopt");
  Check(s.customSettings.empty(), "default customSettings is empty");
}

void TestParsing() {
  std::cout << "[parsing] UserSettings INI parse\n";
  constexpr std::string_view iniText = R"(
# User Settings Test
[window]
x = 250
y = 150
width = 1920
height = 1080
display_index = 1
display_name = External Monitor 4K
fullscreen = false
maximized = true

[audio]
master_volume = 0.75
muted = true

[engine]
fps_target = 144

[settings]
difficulty = "nightmare"
screen_shake = true
sfx_volume = 0.85
ui_scale = 125
numeric_string = "12345"
)";

  const auto s = octarine::UserSettings::Parse(iniText);
  Check(s.hasWindowPlacement, "hasWindowPlacement is true");
  Check(s.windowX == 250, "windowX is 250");
  Check(s.windowY == 150, "windowY is 150");
  Check(s.windowWidth == 1920, "windowWidth is 1920");
  Check(s.windowHeight == 1080, "windowHeight is 1080");
  Check(s.displayIndex == 1, "displayIndex is 1");
  Check(s.displayName == "External Monitor 4K", "displayName parsed");
  Check(!s.fullscreen, "fullscreen is false");
  Check(s.maximized, "maximized is true");

  Check(std::abs(s.masterVolume - 0.75F) < 0.001F, "masterVolume parsed");
  Check(s.audioMuted, "audioMuted is true");

  Check(s.fpsTarget.has_value() && *s.fpsTarget == 144, "fpsTarget parsed");

  const auto diff = s.GetSetting("difficulty");
  Check(diff.has_value() && std::holds_alternative<std::string>(*diff) && std::get<std::string>(*diff) == "nightmare",
        "typed string setting parsed");
  const auto shake = s.GetSetting("screen_shake");
  Check(shake.has_value() && std::holds_alternative<bool>(*shake) && std::get<bool>(*shake),
        "typed bool setting parsed");
  const auto sfx = s.GetSetting("sfx_volume");
  Check(sfx.has_value() && std::holds_alternative<double>(*sfx) && std::abs(std::get<double>(*sfx) - 0.85) < 0.001,
        "typed double setting parsed");
  const auto scale = s.GetSetting("ui_scale");
  Check(scale.has_value() && std::holds_alternative<int64_t>(*scale) && std::get<int64_t>(*scale) == 125,
        "typed int64 setting parsed");
  const auto numStr = s.GetSetting("numeric_string");
  Check(numStr.has_value() && std::holds_alternative<std::string>(*numStr) && std::get<std::string>(*numStr) == "12345",
        "quoted numeric string preserved as std::string");
  Check(s.GetCustomSetting("nonexistent", "fallback") == "fallback", "custom fallback works");
}

void TestSchemaAndTypeValidation() {
  std::cout << "[schema] UserSettings schema definition & type validation\n";
  octarine::UserSettings s;
  s.DefineSetting("screen_shake", true);
  s.DefineSetting("sfx_volume", 0.8);
  s.DefineSetting("difficulty", std::string("normal"));

  Check(s.HasSetting("screen_shake"), "defined setting exists");
  Check(std::get<bool>(*s.GetSetting("screen_shake")), "defined default returned before set");

  Check(s.SetSetting("screen_shake", false), "valid bool set succeeds");
  Check(!std::get<bool>(*s.GetSetting("screen_shake")), "updated bool returned");
  Check(std::get<bool>(*s.GetDefaultSetting("screen_shake")), "default remains true");

  Check(!s.SetSetting("screen_shake", std::string("invalid")), "setting string into bool key rejected");
  Check(!std::get<bool>(*s.GetSetting("screen_shake")), "rejected set leaves value intact");

  Check(s.SetSetting("sfx_volume", int64_t{1}), "setting integer into double key coerces to double");
  Check(std::holds_alternative<double>(*s.GetSetting("sfx_volume")) &&
            std::abs(std::get<double>(*s.GetSetting("sfx_volume")) - 1.0) < 0.001,
        "coerced value is double 1.0");

  Check(s.ResetSetting("screen_shake"), "ResetSetting succeeds");
  Check(std::get<bool>(*s.GetSetting("screen_shake")), "ResetSetting restores default");
}

void TestSerializationRoundTrip() {
  std::cout << "[roundtrip] UserSettings serialization round-trip\n";
  octarine::UserSettings original;
  original.windowX = 500;
  original.windowY = 300;
  original.windowWidth = 1600;
  original.windowHeight = 900;
  original.displayIndex = 2;
  original.displayName = "Studio Display";
  original.fullscreen = true;
  original.windowMode = WindowMode::ExclusiveFullscreen;
  original.fullscreenWidth = 2560;
  original.fullscreenHeight = 1440;
  original.fullscreenRefreshRate = 144.0F;
  original.presentationMode = PresentationMode::IntegerScale;
  original.textureScaleMode = "pixelart";
  original.vsync = true;
  original.windowResizable = false;
  original.maximized = false;
  original.hasWindowPlacement = true;
  original.masterVolume = 0.6F;
  original.audioMuted = false;
  original.fpsTarget = 60;
  original.DefineSetting("screen_shake", true);
  original.SetSetting("screen_shake", false);
  original.DefineSetting("sfx_volume", 1.0);
  original.SetCustomSetting("gamepad_layout", "southpaw");
  original.SetCustomSetting("special_chars", "a = \"b\"\nc");

  const std::string serialized = original.Serialize();
  const auto reloaded = octarine::UserSettings::Parse(serialized);

  Check(reloaded.hasWindowPlacement == original.hasWindowPlacement, "roundtrip hasWindowPlacement");
  Check(reloaded.windowX == original.windowX, "roundtrip windowX");
  Check(reloaded.windowY == original.windowY, "roundtrip windowY");
  Check(reloaded.windowWidth == original.windowWidth, "roundtrip windowWidth");
  Check(reloaded.windowHeight == original.windowHeight, "roundtrip windowHeight");
  Check(reloaded.displayIndex == original.displayIndex, "roundtrip displayIndex");
  Check(reloaded.displayName == original.displayName, "roundtrip displayName");
  Check(reloaded.fullscreen == original.fullscreen, "roundtrip fullscreen");
  Check(reloaded.windowMode == original.windowMode, "roundtrip windowMode (exclusive)");
  Check(reloaded.fullscreenWidth == 2560 && reloaded.fullscreenHeight == 1440, "roundtrip fullscreen resolution");
  Check(std::abs(reloaded.fullscreenRefreshRate - 144.0F) < 0.01F, "roundtrip fullscreenRefreshRate");
  Check(reloaded.presentationMode == PresentationMode::IntegerScale, "roundtrip presentationMode");
  Check(reloaded.textureScaleMode == "pixelart", "roundtrip textureScaleMode");
  Check(reloaded.vsync == original.vsync && *reloaded.vsync, "roundtrip vsync");
  Check(reloaded.windowResizable == original.windowResizable && !*reloaded.windowResizable,
        "roundtrip windowResizable");
  Check(reloaded.maximized == original.maximized, "roundtrip maximized");
  Check(std::abs(reloaded.masterVolume - original.masterVolume) < 0.001F, "roundtrip masterVolume");
  Check(reloaded.audioMuted == original.audioMuted, "roundtrip audioMuted");
  Check(reloaded.fpsTarget == original.fpsTarget, "roundtrip fpsTarget");
  Check(!std::get<bool>(*reloaded.GetSetting("screen_shake")), "roundtrip bool setting");
  Check(std::holds_alternative<double>(*reloaded.GetSetting("sfx_volume")) &&
            std::abs(std::get<double>(*reloaded.GetSetting("sfx_volume")) - 1.0) < 0.001,
        "roundtrip integral double 1.0 stays double");
  Check(reloaded.GetCustomSetting("gamepad_layout") == "southpaw", "roundtrip custom string 1");
  Check(reloaded.GetCustomSetting("special_chars") == "a = \"b\"\nc", "roundtrip escaped string");
}

void TestLuaSettingsModule() {
  std::cout << "[lua] Lua settings.* module define, get, set, reset, and persistence round-trip\n";
  const auto tempDir = std::filesystem::temp_directory_path() / "octarine_lua_settings_test";
  std::error_code ec;
  std::filesystem::remove_all(tempDir, ec);
  std::filesystem::create_directories(tempDir, ec);

  // Session 1: Define settings in Lua, modify some, modify engine settings, and save to disk.
  {
    FakeSettingsHost host(tempDir.string());
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    LuaModuleBinding<GameModule>::install(lua, host);

    const bool ok = lua.script(R"(
      assert(type(settings) == "table", "settings table must exist")
      assert(settings.define("screen_shake", true) == true)
      assert(settings.define({
        sfx_volume = 0.8,
        music_volume = 0.7,
        difficulty = "normal",
        ui_scale = 100,
        controls = {
          jump = "Space",
          dash = "LeftShift",
        },
      }) == true)

      -- Check defaults before any overrides
      assert(settings.get("screen_shake") == true)
      assert(math.abs(settings.get("sfx_volume") - 0.8) < 0.001)
      assert(settings.get("difficulty") == "normal")
      assert(settings.get("ui_scale") == 100)
      assert(settings.get("controls.jump") == "Space")
      assert(settings.get("unknown_key", "my_fallback") == "my_fallback")

      -- Modify game settings & engine settings through the same unified API
      assert(settings.set("screen_shake", false) == true)
      assert(settings.set("sfx_volume", 0.45) == true)
      assert(settings.set("difficulty", "hard") == true)
      assert(settings.set("controls.jump", "ButtonSouth") == true)
      assert(settings.set("master_volume", 0.65) == true)
      assert(settings.set("audio_muted", true) == true)
      assert(settings.set("fps_target", 120) == true)

      -- Reject invalid type for defined setting
      assert(settings.set("screen_shake", "not_a_bool") == false)
      assert(settings.get("screen_shake") == false)

      return settings.save()
    )")
                        .get<bool>();
    Check(ok, "Session 1: Lua settings.define / set / save succeeded");
  }

  // Session 2: Simulate engine boot where user_settings.ini is loaded BEFORE Lua runs settings.define!
  {
    FakeSettingsHost host(tempDir.string());
    auto& userSettings = host.GetRegistry()->Get<octarine::UserSettings>();
    Check(userSettings.Load(tempDir.string()), "Session 2: C++ loads user_settings.ini at startup");

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    LuaModuleBinding<GameModule>::install(lua, host);

    const bool ok = lua.script(R"(
      -- Game script defines its schema defaults on startup (after C++ already loaded user_settings.ini)
      settings.define("screen_shake", true)
      settings.define({
        sfx_volume = 0.8,
        music_volume = 0.7,
        difficulty = "normal",
        ui_scale = 100,
        controls = {
          jump = "Space",
          dash = "LeftShift",
        },
      })

      -- Saved overrides from Session 1 must win over defaults and keep their native Lua types!
      assert(type(settings.get("screen_shake")) == "boolean" and settings.get("screen_shake") == false)
      assert(type(settings.get("sfx_volume")) == "number" and math.abs(settings.get("sfx_volume") - 0.45) < 0.001)
      assert(type(settings.get("music_volume")) == "number" and math.abs(settings.get("music_volume") - 0.7) < 0.001)
      assert(type(settings.get("difficulty")) == "string" and settings.get("difficulty") == "hard")
      assert(type(settings.get("ui_scale")) == "number" and settings.get("ui_scale") == 100)
      assert(settings.get("controls.jump") == "ButtonSouth")
      assert(settings.get("controls.dash") == "LeftShift")

      -- Default inspection and reset work as expected
      assert(settings.get_default("screen_shake") == true)
      assert(settings.reset("screen_shake") == true)
      assert(settings.get("screen_shake") == true)

      settings.reset_all()
      assert(settings.get("difficulty") == "normal")
      assert(settings.get("controls.jump") == "Space")

      local all = settings.get_all()
      assert(type(all) == "table" and all.difficulty == "normal" and all.screen_shake == true)
      return true
    )")
                        .get<bool>();
    Check(ok, "Session 2: Persisted settings survive reload, preserve Lua types, and support reset");
  }

  std::filesystem::remove_all(tempDir, ec);
}

void TestGameConfigAndCompositeRect() {
  std::cout << "[display] GameConfig display defaults & Renderer::ComputeCompositeRect\n";
  GameConfig cfg;
  const std::unordered_map<std::string, std::string> raw = {
      {"Title", "Display Test Game"},
      {"StartupScript", "game.lua"},
      {"BaseResolutionWidth", "640"},
      {"BaseResolutionHeight", "360"},
      {"DefaultWindowWidth", "640"},
      {"DefaultWindowHeight", "360"},
      {"DefaultWindowMode", "borderless"},
      {"DefaultPresentationMode", "integer_scale"},
      {"DefaultVSync", "true"},
      {"WindowResizable", "false"},
  };
  Check(cfg.LoadConfig(raw), "GameConfig::LoadConfig with display keys succeeds");
  Check(cfg.GetBaseWidth() == 640 && cfg.GetBaseHeight() == 360, "BaseResolutionWidth/Height parsed");
  Check(cfg.GetDefaultWidth() == 640 && cfg.GetDefaultHeight() == 360, "DefaultWindowWidth/Height parsed");
  Check(cfg.GetEngineOptions().windowMode == WindowMode::BorderlessFullscreen, "DefaultWindowMode borderless parsed");
  Check(cfg.GetEngineOptions().presentationMode == PresentationMode::IntegerScale,
        "DefaultPresentationMode integer_scale parsed");
  Check(cfg.GetEngineOptions().vsync, "DefaultVSync true parsed");
  Check(!cfg.GetEngineOptions().windowResizable, "WindowResizable false parsed");

  GameConfig fallbackCfg;
  const std::unordered_map<std::string, std::string> missingBase = {
      {"Title", "No Base"},
      {"StartupScript", "game.lua"},
      {"DefaultWindowWidth", "800"},
      {"DefaultWindowHeight", "600"},
  };
  Check(fallbackCfg.LoadConfig(missingBase),
        "GameConfig::LoadConfig succeeds when BaseResolutionWidth/Height are omitted");
  Check(fallbackCfg.GetBaseWidth() == 800 && fallbackCfg.GetBaseHeight() == 600,
        "BaseResolution falls back to DefaultWindowWidth/Height");

  GameConfig badCfg;
  const std::unordered_map<std::string, std::string> invalidAspect = {
      {"Title", "Bad Aspect"},
      {"StartupScript", "game.lua"},
      {"MinAspectRatio", "2.0"},
      {"MaxAspectRatio", "1.5"},
  };
  Check(!badCfg.LoadConfig(invalidAspect), "GameConfig::LoadConfig fails when MinAspectRatio > MaxAspectRatio");

  // Composite rect math: 640x360 (16:9) inside a 1920x1200 (16:10) window
  const SDL_FRect letterbox = Renderer::ComputeCompositeRect(640, 360, 1920, 1200, PresentationMode::Letterbox);
  Check(std::abs(letterbox.x - 0.0F) < 0.01F && std::abs(letterbox.y - 60.0F) < 0.01F &&
            std::abs(letterbox.w - 1920.0F) < 0.01F && std::abs(letterbox.h - 1080.0F) < 0.01F,
        "ComputeCompositeRect Letterbox centers 16:9 inside 1920x1200 with 60px top/bottom bars");

  // IntegerScale: 640x360 inside 1600x900 -> 2x scale (1280x720) centered at (160, 90)
  const SDL_FRect intScale = Renderer::ComputeCompositeRect(640, 360, 1600, 900, PresentationMode::IntegerScale);
  Check(std::abs(intScale.x - 160.0F) < 0.01F && std::abs(intScale.y - 90.0F) < 0.01F &&
            std::abs(intScale.w - 1280.0F) < 0.01F && std::abs(intScale.h - 720.0F) < 0.01F,
        "ComputeCompositeRect IntegerScale picks 2x (1280x720) inside 1600x900");

  // IntegerScale fallback when window is smaller than 1x scene: 640x360 inside 320x200
  const SDL_FRect smallInt = Renderer::ComputeCompositeRect(640, 360, 320, 200, PresentationMode::IntegerScale);
  Check(std::abs(smallInt.w - 320.0F) < 0.01F && std::abs(smallInt.h - 180.0F) < 0.01F &&
            std::abs(smallInt.y - 10.0F) < 0.01F,
        "ComputeCompositeRect IntegerScale falls back to Letterbox when window < 1x scene");

  // Stretch: fills full window
  const SDL_FRect stretch = Renderer::ComputeCompositeRect(640, 360, 1920, 1200, PresentationMode::Stretch);
  Check(std::abs(stretch.x) < 0.01F && std::abs(stretch.y) < 0.01F && std::abs(stretch.w - 1920.0F) < 0.01F &&
            std::abs(stretch.h - 1200.0F) < 0.01F,
        "ComputeCompositeRect Stretch fills entire window");

  // Expand mode logical sizing across aspect ratios
  const auto s169 = Renderer::ComputeLogicalSceneSize(1280, 720, 1920, 1080, PresentationMode::Expand);
  Check(s169.first == 1280 && s169.second == 720, "ComputeLogicalSceneSize Expand 16:9 keeps 1280x720");

  const auto s1610 = Renderer::ComputeLogicalSceneSize(1280, 720, 1280, 800, PresentationMode::Expand);
  Check(s1610.first == 1280 && s1610.second == 800, "ComputeLogicalSceneSize Expand 16:10 expands height to 800");

  const auto s43 = Renderer::ComputeLogicalSceneSize(1280, 720, 1280, 960, PresentationMode::Expand);
  Check(s43.first == 1280 && s43.second == 960, "ComputeLogicalSceneSize Expand 4:3 expands height to 960");

  const auto s219 = Renderer::ComputeLogicalSceneSize(1280, 720, 2560, 1080, PresentationMode::Expand);
  Check(s219.first == 1708 && s219.second == 720, "ComputeLogicalSceneSize Expand 21:9 expands width to 1708");

  const auto s329 = Renderer::ComputeLogicalSceneSize(1280, 720, 5120, 1440, PresentationMode::Expand, 1.3333F, 2.40F);
  Check(s329.first == 1728 && s329.second == 720,
        "ComputeLogicalSceneSize Expand 32:9 clamped to maxAspect=2.40 (1728x720)");

  // Clamped 32:9 (1728x720) inside 5120x1440 window pillarboxes to 3456x1440 centered at x=832
  const SDL_FRect expClamped = Renderer::ComputeCompositeRect(1728, 720, 5120, 1440, PresentationMode::Expand);
  Check(std::abs(expClamped.x - 832.0F) < 0.01F && std::abs(expClamped.y - 0.0F) < 0.01F &&
            std::abs(expClamped.w - 3456.0F) < 0.01F && std::abs(expClamped.h - 1440.0F) < 0.01F,
        "ComputeCompositeRect Expand pillarboxes clamped 32:9 aspect inside window");
}

void TestLuaDisplayApisAndReset() {
  std::cout << "[lua-display] Lua display modes, window modes, presentation modes, vsync, and default reset\n";
  const auto tempDir = std::filesystem::temp_directory_path() / "octarine_lua_display_test";
  std::error_code ec;
  std::filesystem::remove_all(tempDir, ec);
  std::filesystem::create_directories(tempDir, ec);

  FakeSettingsHost host(tempDir.string());
  auto& cfg = host.GetRegistry()->Get<GameConfig>();
  auto& us = host.GetRegistry()->Get<octarine::UserSettings>();
  cfg.SetBaseWidth(1280);
  cfg.SetBaseHeight(720);
  cfg.windowWidth = 1280;
  cfg.windowHeight = 720;
  cfg.GetEngineOptions().windowMode = WindowMode::BorderlessFullscreen;
  cfg.GetEngineOptions().presentationMode = PresentationMode::IntegerScale;
  cfg.GetEngineOptions().vsync = true;
  us.builtInDefaults.windowWidth = 1280;
  us.builtInDefaults.windowHeight = 720;
  us.builtInDefaults.windowMode = WindowMode::BorderlessFullscreen;
  us.builtInDefaults.presentationMode = PresentationMode::IntegerScale;
  us.builtInDefaults.textureScaleMode = "pixelart";
  us.builtInDefaults.vsync = true;
  us.textureScaleMode = "pixelart";
  us.SetWindowMode(WindowMode::BorderlessFullscreen);

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
  LuaModuleBinding<GameModule>::install(lua, host);

  const bool ok = lua.script(R"(
    -- Inspect shipped defaults
    assert(settings.get_default("window_mode") == "borderless")
    assert(settings.get_default("presentation_mode") == "integer_scale")
    assert(settings.get_default("texture_scale_mode") == "pixelart")
    assert(settings.get_default("vsync") == true)
    assert(get_window_mode() == "borderless")
    assert(is_fullscreen() == true)
    assert(get_presentation_mode() == "integer_scale")
    assert(get_texture_scale_mode() == "pixelart")
    assert(get_vsync() == true)

    -- Enumerate displays and display modes (works even headlessly via fallback)
    local displays = get_displays()
    assert(type(displays) == "table" and #displays >= 1)
    assert(type(displays[1].name) == "string" and displays[1].width > 0 and displays[1].height > 0)

    local modes = get_display_modes(0)
    assert(type(modes) == "table" and #modes >= 1)
    assert(modes[1].width > 0 and modes[1].height > 0 and modes[1].refresh_rate > 0)

    -- Switch to exclusive fullscreen with custom resolution & refresh rate
    local initFw, initFh = get_fullscreen_resolution()
    assert(initFw > 0 and initFh > 0)
    assert(set_fullscreen_resolution(2560, 1440, 120.0) == true)
    assert(set_window_mode("exclusive") == true)
    assert(get_window_mode() == "exclusive")
    assert(settings.get("window_mode") == "exclusive")
    local fw, fh, fhz = get_fullscreen_resolution()
    assert(fw == 2560 and fh == 1440 and math.abs(fhz - 120.0) < 0.01)

    -- Switch presentation mode and vsync via both direct functions and settings.set
    assert(set_presentation_mode("expand") == true)
    assert(settings.get("presentation_mode") == "expand")
    assert(get_presentation_mode() == "expand")
    assert(set_presentation_mode("stretch") == true)
    assert(settings.get("presentation_mode") == "stretch")
    assert(settings.set("presentation_mode", "letterbox") == true)
    assert(get_presentation_mode() == "letterbox")

    local bw, bh = get_base_resolution()
    assert(bw == 1280 and bh == 720)
    local lw, lh = get_logical_size()
    assert(lw == 1280 and lh == 720)
    local dw, dh = get_display_size()
    assert(dw > 0 and dh > 0)

    assert(set_texture_scale_mode("nearest") == true)
    assert(settings.get("texture_scale_mode") == "nearest")
    assert(settings.set("texture_scale_mode", "linear") == true)
    assert(get_texture_scale_mode() == "linear")
    assert(set_texture_scale_mode("pixel_art") == true)
    assert(get_texture_scale_mode() == "pixelart")

    assert(set_vsync(false) == true)
    assert(settings.get("vsync") == false)
    assert(settings.set("vsync", true) == true)
    assert(get_vsync() == true)

    -- Reject invalid mode strings
    assert(set_window_mode("invalid_mode") == false)
    assert(set_presentation_mode("invalid_scaling") == false)
    assert(set_texture_scale_mode("invalid_filter") == false)

    -- Reset built-in settings back to shipped defaults from config.ini
    assert(settings.set("window_mode", "windowed") == true)
    assert(get_window_mode() == "windowed")
    assert(settings.reset("window_mode") == "borderless")
    assert(get_window_mode() == "borderless")

    settings.set("presentation_mode", "stretch")
    settings.set("texture_scale_mode", "linear")
    settings.set("vsync", false)
    settings.reset_all()
    assert(get_presentation_mode() == "integer_scale")
    assert(get_texture_scale_mode() == "pixelart")
    assert(get_vsync() == true)
    return true
  )")
                      .get<bool>();

  Check(ok, "Lua display APIs, mode switching, resolution queries, and built-in reset work");
  std::filesystem::remove_all(tempDir, ec);
}

void TestGameConfigAspectAndLogicalBounds() {
  std::cout << "[aspect-bounds] GameConfig negative resolution guard, aspect getters, and logical size bounds\n";
  GameConfig negCfg;
  const std::unordered_map<std::string, std::string> negativeBase = {
      {"Title", "Negative Base"},
      {"StartupScript", "game.lua"},
      {"BaseResolutionWidth", "-640"},
      {"BaseResolutionHeight", "360"},
  };
  Check(!negCfg.LoadConfig(negativeBase), "GameConfig::LoadConfig rejects negative BaseResolutionWidth");

  GameConfig aspectCfg;
  aspectCfg.SetMinAspectRatio(1.33F);
  aspectCfg.SetMaxAspectRatio(2.40F);
  Check(std::abs(aspectCfg.GetMinAspectRatio() - 1.33F) < 0.001F, "GetMinAspectRatio returns configured value");
  Check(std::abs(aspectCfg.GetMaxAspectRatio() - 2.40F) < 0.001F, "GetMaxAspectRatio returns configured value");

  const auto zeroBase = Renderer::ComputeLogicalSceneSize(0, -10, 1920, 1080, PresentationMode::Expand);
  Check(zeroBase.first == 1, "ComputeLogicalSceneSize clamps non-positive baseW to 1");
  Check(zeroBase.second == 1, "ComputeLogicalSceneSize clamps non-positive baseH to 1");

  const auto nonExpand = Renderer::ComputeLogicalSceneSize(640, 360, 1920, 1200, PresentationMode::Letterbox);
  Check(nonExpand.first == 640, "ComputeLogicalSceneSize preserves baseW in Letterbox mode");
  Check(nonExpand.second == 360, "ComputeLogicalSceneSize preserves baseH in Letterbox mode");

  const auto minClamped =
      Renderer::ComputeLogicalSceneSize(1280, 720, 720, 1280, PresentationMode::Expand, 1.3333334F, 2.40F);
  Check(minClamped.first == 1280, "ComputeLogicalSceneSize portrait clamped to minAspect 4:3 width");
  Check(minClamped.second == 960, "ComputeLogicalSceneSize portrait clamped to minAspect 4:3 height");
}

void TestSoftwareRendererSceneTarget() {
  std::cout << "[renderer-target] Renderer logical size, scene target resize, and render scale\n";
  SDL_Surface* surface = SDL_CreateSurface(1600, 1000, SDL_PIXELFORMAT_RGBA32);
  SDL_Renderer* sdlRenderer = surface != nullptr ? SDL_CreateSoftwareRenderer(surface) : nullptr;
  Check(sdlRenderer != nullptr, "software SDL_Renderer created for Renderer target test");
  if (sdlRenderer == nullptr) {
    SDL_DestroySurface(surface);
    return;
  }

  Renderer renderer;
  Check(renderer.CreateScene(sdlRenderer, 1280, 720), "Renderer::CreateScene succeeds");
  Check(renderer.GetLogicalWidth() == 1280, "GetLogicalWidth matches initial 1280");
  Check(renderer.GetLogicalHeight() == 720, "GetLogicalHeight matches initial 720");
  Check(std::abs(renderer.GetRenderScale() - 1.0F) < 0.001F, "initial render scale is 1.0");

  renderer.SetLogicalSize(1280, 800);
  Check(renderer.GetLogicalWidth() == 1280, "SetLogicalSize updates width to 1280");
  Check(renderer.GetLogicalHeight() == 800, "SetLogicalSize updates height to 800");
  Check(renderer.EnsureSceneTargetSize(sdlRenderer, 1600, 1000), "EnsureSceneTargetSize resizes target to 1600x1000");
  Check(std::abs(renderer.GetRenderScale() - 1.25F) < 0.001F, "render scale updates to 1.25x");

  Renderer::ApplyVSync(sdlRenderer, false);
  renderer.DestroyScene();
  SDL_DestroyRenderer(sdlRenderer);
  SDL_DestroySurface(surface);
}

void TestLuaCameraAndExpandResize() {
  std::cout << "[lua-camera-expand] Camera zoom clamping and expand presentation resize via Lua\n";
  const auto tempDir = std::filesystem::temp_directory_path() / "octarine_lua_cam_expand_test";
  std::error_code ec;
  std::filesystem::remove_all(tempDir, ec);
  std::filesystem::create_directories(tempDir, ec);

  SDL_Surface* surface = SDL_CreateSurface(1600, 1000, SDL_PIXELFORMAT_RGBA32);
  SDL_Renderer* sdlRenderer = surface != nullptr ? SDL_CreateSoftwareRenderer(surface) : nullptr;

  FakeSettingsHost host(tempDir.string());
  host.SetRenderer(sdlRenderer);
  auto& cfg = host.GetRegistry()->Get<GameConfig>();
  cfg.SetBaseWidth(1280);
  cfg.SetBaseHeight(720);
  cfg.windowWidth = 1280;
  cfg.windowHeight = 720;
  cfg.playableAreaWidth = 2000.0F;
  cfg.playableAreaHeight = 1200.0F;

  CameraComponent cam{};
  cam.viewport = {200.0F, 150.0F, 1280.0F, 720.0F};
  cam.zoom = 1.0F;
  host.GetRegistry()->Set<CameraComponent>(cam);

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math);
  LuaModuleBinding<GameModule>::install(lua, host);

  const bool ok = lua.script(R"(
    set_camera_zoom(2.0)
    assert(math.abs(get_camera_zoom() - 2.0) < 0.001)
    set_camera_zoom(0.5)
    assert(math.abs(get_camera_zoom() - 0.5) < 0.001)
    set_camera_zoom(1.0)

    assert(set_presentation_mode("expand") == true)
    local lw, lh = get_logical_size()
    assert(lw == 1280 and lh == 800)

    assert(set_presentation_mode("letterbox") == true)
    local lw2, lh2 = get_logical_size()
    assert(lw2 == 1280 and lh2 == 720)
    return true
  )")
                      .get<bool>();
  Check(ok, "set_camera_zoom and set_presentation_mode('expand') update logical size and camera viewport");

  const auto& updatedCam = host.GetRegistry()->Get<CameraComponent>();
  Check(std::abs(updatedCam.viewport.w - 1280.0F) < 0.1F, "camera viewport width restored to 1280");
  Check(std::abs(updatedCam.viewport.h - 720.0F) < 0.1F, "camera viewport height restored to 720");

  SDL_DestroyRenderer(sdlRenderer);
  SDL_DestroySurface(surface);
  std::filesystem::remove_all(tempDir, ec);
}

void TestRuntimeWindowAndFrameLoopExpand() {
  std::cout << "[runtime-frameloop] Game::Initialize + FrameLoop expand/letterbox resize under dummy driver\n";
  const auto tempRoot = std::filesystem::temp_directory_path() / "octarine_frameloop_expand_test";
  const auto projDir = tempRoot / "proj";
  const auto saveDir = tempRoot / "save";
  std::error_code ec;
  std::filesystem::remove_all(tempRoot, ec);
  std::filesystem::create_directories(projDir, ec);
  std::filesystem::create_directories(saveDir, ec);

  {
    std::ofstream cfgOut(projDir / "config.ini");
    cfgOut << "Title = Expand FrameLoop Test\n"
           << "StartupScript = main.lua\n"
           << "BaseResolutionWidth = 640\n"
           << "BaseResolutionHeight = 360\n"
           << "DefaultWindowWidth = 1280\n"
           << "DefaultWindowHeight = 800\n"
           << "DefaultPresentationMode = expand\n"
           << "FpsTarget = 0\n";
    std::ofstream luaOut(projDir / "main.lua");
    luaOut << "-- minimal startup script for FrameLoop test\n";
  }

  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
  SetProcessEnvVar("OCTARINE_SAVE_PATH", saveDir.string().c_str());

  {
    Game game;
    game.SetStartupMode("bench");
    const bool initOk = game.Initialize(projDir.string());
    Check(initOk, "Game::Initialize succeeds under dummy video/audio driver");
    if (initOk) {
      game.Step(1.0F / 60.0F);
      const auto& cfg = game.GetRegistry()->Get<GameConfig>();
      Check(cfg.windowWidth == 640, "FrameLoop Expand keeps 640 base width in 1280x800 window");
      Check(cfg.windowHeight == 400, "FrameLoop Expand expands height from 360 to 400 in 1280x800 window");
      Check(game.GetLua()["game_window_height"].get<int>() == 400, "Lua game_window_height updated to 400");

      const auto [dispW, dispH] = game.GetLua().script("return get_display_size(0)").get<std::tuple<int, int>>();
      Check(dispW > 0 && dispH > 0, "get_display_size(0) returns positive display bounds under initialized video");
      const auto [fbDispW, fbDispH] = game.GetLua().script("return get_display_size(99)").get<std::tuple<int, int>>();
      Check(fbDispW > 0 && fbDispH > 0, "get_display_size(99) falls back to window size when index out of range");

      game.GetLua().script("set_presentation_mode('letterbox')");
      SDL_Event moveEv{};
      moveEv.type = SDL_EVENT_WINDOW_MOVED;
      SDL_PushEvent(&moveEv);
      SDL_Event resizeEv{};
      resizeEv.type = SDL_EVENT_WINDOW_RESIZED;
      SDL_PushEvent(&resizeEv);
      SDL_Event displayEv{};
      displayEv.type = SDL_EVENT_WINDOW_DISPLAY_CHANGED;
      SDL_PushEvent(&displayEv);

      game.Step(1.0F / 60.0F);
      Check(cfg.windowHeight == 360, "FrameLoop Letterbox restores logical height to 360 after window events");
      Check(game.GetLua()["game_window_height"].get<int>() == 360, "Lua game_window_height restored to 360");
    }
    game.Destroy();
  }

  SetProcessEnvVar("OCTARINE_SAVE_PATH", nullptr);
  std::filesystem::remove_all(tempRoot, ec);
}

}  // namespace

int main() {
  Logger::Init();

  TestDefaults();
  TestParsing();
  TestSchemaAndTypeValidation();
  TestSerializationRoundTrip();
  TestLuaSettingsModule();
  TestGameConfigAndCompositeRect();
  TestLuaDisplayApisAndReset();
  TestGameConfigAspectAndLogicalBounds();
  TestSoftwareRendererSceneTarget();
  TestLuaCameraAndExpandResize();
  TestRuntimeWindowAndFrameLoopExpand();

  if (g_failures == 0) {
    std::cout << "\nUserSettings test: PASS\n";
  } else {
    std::cerr << "\nUserSettings test: " << g_failures << " FAILED\n";
  }
  return g_failures;
}
