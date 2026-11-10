#include <filesystem>
#include <sol/sol.hpp>
#include <string>

#include "ECS/Registry.h"
#include "Game/Game.h"
#include "Game/GameConfig.h"
#include "General/FileSandbox.h"
#include "General/Logger.h"
#include "Lua/Bindings/RegisterAllBindings.h"
#include "Lua/Modules/RegisterAllModules.h"
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

  std::filesystem::remove_all(testSaveDir);
  return octarine::test::ReportSummary("OctarineLuaModulesTest");
}
