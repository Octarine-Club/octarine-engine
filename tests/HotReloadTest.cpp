#include <filesystem>
#include <fstream>
#include <sol/sol.hpp>
#include <string>

#include "Components/ScriptComponent.h"
#include "ECS/Registry.h"
#include "General/Logger.h"
#include "Lua/HotReload/ScriptHotReload.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

void WriteFile(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::trunc);
  out << content;
  out.close();
}

void TestScriptHotReloadWorkflow() {
  std::cout << "[HotReload: ScriptHotReload workflow]\n";

  const std::string tempScriptPath =
      (std::filesystem::temp_directory_path() / "oct_test_script_hot_reload.lua").string();

  // 1. Initial working script version 1
  WriteFile(tempScriptPath, R"LUA(
    local M = {}
    M.version = 1
    function M.get_value()
      return 100
    end
    return M
  )LUA");

  Registry registry;
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table);

  ScriptHotReload hotReload;

  Entity entity = registry.CreateEntity();
  ScriptComponent sc;
  sc.sourcePath = tempScriptPath;
  registry.AddComponent(entity, sc);

  // Initial load via ForceReloadAll
  hotReload.ForceReloadAll(registry, lua);

  const auto& scLoaded1 = registry.GetComponent<ScriptComponent>(entity);
  Check(scLoaded1.script_table.valid(), "Script table populated on initial reload");
  CheckEq(scLoaded1.script_table["version"].get<int>(), 1, "Script table version is 1");

  sol::protected_function fn1 = scLoaded1.script_table["get_value"];
  CheckEq(fn1().get<int>(), 100, "Version 1 function returns 100");
  CheckEq(hotReload.LastReload().totalReloads, 1, "totalReloads is 1");
  CheckEq(hotReload.LastReload().totalFailures, 0, "totalFailures is 0");

  // 2. Modify script on disk to version 2
  WriteFile(tempScriptPath, R"LUA(
    local M = {}
    M.version = 2
    function M.get_value()
      return 200
    end
    return M
  )LUA");

  hotReload.ForceReloadAll(registry, lua);

  const auto& scLoaded2 = registry.GetComponent<ScriptComponent>(entity);
  CheckEq(scLoaded2.script_table["version"].get<int>(), 2, "Script table updated to version 2");

  sol::protected_function fn2 = scLoaded2.script_table["get_value"];
  CheckEq(fn2().get<int>(), 200, "Version 2 function returns 200");
  CheckEq(hotReload.LastReload().totalReloads, 2, "totalReloads incremented to 2");

  // 3. Write syntax error script to disk — hot reload must fail safely without wiping working state
  WriteFile(tempScriptPath, R"LUA(
    this is a deliberate syntax error !@#$
  )LUA");

  hotReload.ForceReloadAll(registry, lua);

  CheckEq(hotReload.LastReload().totalFailures, 1, "totalFailures incremented on syntax error");
  Check(!hotReload.LastReload().error.empty(), "Error message recorded in LastReload()");

  // Previous working state remains intact
  const auto& scAfterFail = registry.GetComponent<ScriptComponent>(entity);
  Check(scAfterFail.script_table.valid(), "Script table remains valid despite failed reload");
  CheckEq(scAfterFail.script_table["version"].get<int>(), 2, "Previous version 2 preserved");

  std::filesystem::remove(tempScriptPath);
}

}  // namespace

int main() {
  Logger::Init();
  TestScriptHotReloadWorkflow();
  return octarine::test::ReportSummary("OctarineHotReloadTest");
}
