#include <cmath>
#include <filesystem>
#include <iostream>
#include <sol/sol.hpp>
#include <string>
#include <vector>

#include "ECS/Registry.h"
#include "Engine/EngineContext.h"
#include "Game/GameConfig.h"
#include "Game/UserSettings.h"
#include "General/Logger.h"
#include "Lua/LuaBindingContext.h"
#include "Lua/Modules/GameModuleLuaBinding.h"

namespace {

int g_failures = 0;

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

  [[nodiscard]] Registry* GetRegistry() const override { return &registry_; }
  [[nodiscard]] SDL_Renderer* GetRenderer() const override { return nullptr; }
  [[nodiscard]] EngineContext& GetContext() override { return registry_.Get<EngineContext>(); }

  void LoadScene(const std::string& /*scenePath*/) override {}
  void ReloadScene() override {}
  void StopScene() override {}
  void TrackSceneAssets(const std::vector<std::string>& /*assetIds*/) override {}

  [[nodiscard]] bool IsBakeMode() const override { return false; }
  void RecordBakeValidationFailures(int /*failures*/) override {}

  [[nodiscard]] bool IsEditorMode() const override { return false; }
  [[nodiscard]] std::string GetProjectPath() const override { return {}; }
  std::string GetSaveDataPath() override { return save_root_; }

  void RequestQuit() override {}

 private:
  mutable Registry registry_{};
  EngineContext context_{};
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
  Check(s.masterVolume == 1.0F, "default masterVolume is 1.0");
  Check(!s.audioMuted, "default audioMuted is false");
  Check(!s.fpsTarget.has_value(), "default fpsTarget is nullopt");
  Check(!s.showPerfOverlay.has_value(), "default showPerfOverlay is nullopt");
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
perf_overlay = true

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
  Check(s.showPerfOverlay.has_value() && *s.showPerfOverlay, "showPerfOverlay parsed");

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
  original.maximized = false;
  original.hasWindowPlacement = true;
  original.masterVolume = 0.6F;
  original.audioMuted = false;
  original.fpsTarget = 60;
  original.showPerfOverlay = false;
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
  Check(reloaded.maximized == original.maximized, "roundtrip maximized");
  Check(std::abs(reloaded.masterVolume - original.masterVolume) < 0.001F, "roundtrip masterVolume");
  Check(reloaded.audioMuted == original.audioMuted, "roundtrip audioMuted");
  Check(reloaded.fpsTarget == original.fpsTarget, "roundtrip fpsTarget");
  Check(reloaded.showPerfOverlay == original.showPerfOverlay, "roundtrip showPerfOverlay");
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
      assert(settings.set("perf_overlay", true) == true)

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

}  // namespace

int main() {
  Logger::Init();

  TestDefaults();
  TestParsing();
  TestSchemaAndTypeValidation();
  TestSerializationRoundTrip();
  TestLuaSettingsModule();

  if (g_failures == 0) {
    std::cout << "\nUserSettings test: PASS\n";
  } else {
    std::cerr << "\nUserSettings test: " << g_failures << " FAILED\n";
  }
  return g_failures;
}
