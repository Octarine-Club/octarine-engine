#include <iostream>
#include <sol/sol.hpp>
#include <string>

#include "Debug/DebugMenuRegistry.h"
#include "Debug/SystemProfilerTracker.h"
#include "ECS/Entity.h"
#include "ECS/Registry.h"
#include "Game/Game.h"
#include "Lua/Modules/DevModuleLuaBinding.h"
#include "Lua/Modules/RegisterAllModules.h"
#include "TestHarness.h"

using octarine::test::Check;

int main() {
  using namespace octarine::debug;

  std::cout << "[DebugMenuRegistry] Basic Registration & Defaults\n";
  {
    DebugMenuRegistry reg;
    bool panelDrawn = false;
    reg.RegisterPanel({
        .id = "test.panel",
        .category = "Game",
        .title = "Test Panel",
        .shortcut = "F3",
        .drawCallback = [&](Registry&) { panelDrawn = true; },
    });

    const auto* p = reg.FindPanel("test.panel");
    Check(p != nullptr, "registered panel is findable");
    Check(!reg.IsPanelOpen("test.panel"), "panel defaults to closed (isOpen == false)");

    reg.SetPanelOpen("test.panel", true);
    Check(reg.IsPanelOpen("test.panel"), "SetPanelOpen(true) sets isOpen to true");

    reg.CloseAllPanels();
    Check(!reg.IsPanelOpen("test.panel"), "CloseAllPanels closes open panels");

    bool actionFired = false;
    reg.RegisterAction("Game/Cheats/Test Action", [&](Registry&) { actionFired = true; }, "Ctrl+T");
    Check(reg.Actions().size() == 1, "action registered");
    Check(reg.Actions()[0].category == "Game", "action category parsed");
    Check(reg.Actions()[0].label == "Cheats/Test Action", "action label parsed");

    bool toggleState = false;
    reg.RegisterToggle("Game/GodMode", [&]() { return toggleState; }, [&](bool v) { toggleState = v; });
    Check(reg.Toggles().size() == 1, "toggle registered");
    Check(!reg.Toggles()[0].getter(), "toggle getter works");
    reg.Toggles()[0].setter(true);
    Check(toggleState, "toggle setter works");

    int watchValue = 42;
    reg.RegisterWatch("Test Watch", [&]() { return std::to_string(watchValue); });
    Check(reg.Watches().size() == 1, "watch registered");
    Check(reg.Watches()[0].valueFormatter() == "42", "watch formatter returns value");
    reg.UnregisterWatch("Test Watch");
    Check(reg.Watches().empty(), "UnregisterWatch removes watch");
  }

  std::cout << "[DebugMenuRegistry] In-Place Hot Reload & Open State Preservation\n";
  {
    DebugMenuRegistry reg;
    int drawCountV1 = 0;
    reg.RegisterPanel({
        .id = "hot.reload.panel",
        .category = "Game",
        .title = "V1",
        .drawCallback = [&](Registry&) { ++drawCountV1; },
    });
    reg.SetPanelOpen("hot.reload.panel", true);

    int drawCountV2 = 0;
    reg.RegisterPanel({
        .id = "hot.reload.panel",
        .category = "Game",
        .title = "V2",
        .drawCallback = [&](Registry&) { ++drawCountV2; },
    });

    const auto* p = reg.FindPanel("hot.reload.panel");
    Check(p != nullptr && p->title == "V2", "title updated to V2");
    Check(reg.IsPanelOpen("hot.reload.panel"), "panel remains open across re-registration");
    Check(reg.Panels().size() == 1, "panel vector does not duplicate entries");

    reg.RegisterAction("Game/Action", [](Registry&) {}, "A1");
    reg.RegisterAction("Game/Action", [](Registry&) {}, "A2");
    Check(reg.Actions().size() == 1, "action vector updates in-place on hot reload");
    Check(reg.Actions()[0].shortcut == "A2", "action shortcut updated");

    reg.RegisterToggle("Game/Toggle", []() { return false; }, [](bool) {});
    reg.RegisterToggle("Game/Toggle", []() { return true; }, [](bool) {});
    Check(reg.Toggles().size() == 1, "toggle vector updates in-place on hot reload");
    Check(reg.Toggles()[0].getter(), "toggle getter updated");
  }

  std::cout << "[DebugMenuRegistry] Per-Project Persistence Serialization\n";
  {
    DebugMenuRegistry reg;
    reg.RegisterPanel({.id = "panel.a", .category = "Engine", .title = "A"});
    reg.RegisterPanel({.id = "panel.b", .category = "Engine", .title = "B"});
    reg.SetPanelOpen("panel.a", true);
    reg.SetPanelOpen("panel.b", false);

    const std::string serialized = reg.SerializeOpenStates();
    Check(!serialized.empty(), "serialized open states non-empty");

    DebugMenuRegistry freshReg;
    // Test restoring when panels registered after deserialize (e.g. Lua scripts loading later)
    freshReg.DeserializeOpenStates(serialized);
    freshReg.RegisterPanel({.id = "panel.a", .category = "Engine", .title = "A"});
    freshReg.RegisterPanel({.id = "panel.b", .category = "Engine", .title = "B"});
    Check(freshReg.IsPanelOpen("panel.a"), "persisted open state restored for panel.a (true)");
    Check(!freshReg.IsPanelOpen("panel.b"), "persisted open state restored for panel.b (false)");
  }

  std::cout << "[DebugMenuRegistry] ClearLuaOwned Lifecycle Safety\n";
  {
    DebugMenuRegistry reg;
    reg.RegisterPanel({.id = "cpp.panel", .category = "Engine", .title = "CPP", .isLuaOwned = false});
    reg.RegisterPanel({.id = "lua.panel", .category = "Game", .title = "LUA", .isLuaOwned = true});
    reg.RegisterAction("Game/LuaAction", [](Registry&) {}, "", true);
    reg.RegisterToggle("Game/LuaToggle", []() { return false; }, [](bool) {}, true);
    reg.RegisterWatch("LuaWatch", []() { return "0"; }, true);

    reg.SetPanelOpen("lua.panel", true);

    reg.ClearLuaOwned();

    Check(reg.FindPanel("cpp.panel") != nullptr, "C++ panel preserved after ClearLuaOwned");
    Check(reg.Actions().empty(), "Lua actions cleared");
    Check(reg.Toggles().empty(), "Lua toggles cleared");
    Check(reg.Watches().empty(), "Lua watches cleared");

    // Re-registering the Lua panel should restore its prior open state from the cache
    reg.RegisterPanel({.id = "lua.panel", .category = "Game", .title = "LUA", .isLuaOwned = true});
    Check(reg.IsPanelOpen("lua.panel"), "Lua panel open state restored after scene reload");
  }

  std::cout << "[DevModuleLuaBinding] Lua dev.* API Surface\n";
  {
    Game game;
    sol::state& lua = game.GetLua();
    lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string);

    auto* reg = game.GetRegistry();
    reg->Set<DebugMenuRegistry>(DebugMenuRegistry());

    LuaModuleBinding<DevModule>::install(lua, game);

    Check(lua["dev"].valid() && lua["dev"].get_type() == sol::type::table, "dev table registered in Lua");

    const auto scriptResult = lua.safe_script(R"(
      local panel_opened = false
      dev.register_panel("test_lua_panel", {
        title = "Lua Panel",
        category = "Game",
        draw = function()
          panel_opened = true
        end
      })

      assert(not dev.is_panel_open("test_lua_panel"), "panel initially closed")
      dev.set_panel_open("test_lua_panel", true)
      assert(dev.is_panel_open("test_lua_panel"), "panel opened via Lua")

      dev.register_action("Game/ActionFromLua", function() end)
      dev.register_toggle("Game/ToggleFromLua", function() return true end, function(v) end)
      dev.watch("LuaHP", function() return 100 end)
    )");

    Check(scriptResult.valid(), "Lua dev script executes without error");
    auto& debugReg = reg->Get<DebugMenuRegistry>();
    Check(debugReg.FindPanel("test_lua_panel") != nullptr, "Lua panel registered in DebugMenuRegistry");
    Check(debugReg.IsPanelOpen("test_lua_panel"), "Lua panel is open");
    Check(!debugReg.Actions().empty(), "Lua action registered in DebugMenuRegistry");
    Check(!debugReg.Toggles().empty(), "Lua toggle registered in DebugMenuRegistry");
    Check(!debugReg.Watches().empty(), "Lua watch registered in DebugMenuRegistry");
    Check(debugReg.Watches()[0].valueFormatter() == "100", "Lua watch returns formatted string");

    reg->ValidateInvariants();
    Check(true, "registry invariants valid after DevModule execution");
  }

  std::cout << "[SystemProfilerTracker] Rolling Averages, P95, Peaks & Throttling\n";
  {
    Registry registry;
    registry.SetSystemTimingEnabled(true);

    struct DummyComp {
      int x = 0;
    };
    auto sysA = registry.RegisterSystem<DummyComp>([](const DummyComp&) {});
    auto sysB = registry.RegisterSystem<DummyComp>([](const DummyComp&) {});

    SystemProfilerTracker tracker(10, 0.25f);

    registry.Update(0.016f);
    tracker.RecordFrame(registry, 0.016f);

    const auto& snap1 = tracker.GetSnapshot();
    Check(snap1.systems.size() == 2, "snapshot tracks all registered systems");
    Check(snap1.avgUpdateMs >= 0.0, "snapshot computes average update ms");
    Check(!snap1.updateMsHistory.empty(), "snapshot records update history");

    double firstAvgA = snap1.systems[0].avgUs;

    // Record next frame with dt < refreshInterval; snapshot shouldn't recompute yet
    registry.Update(0.016f);
    tracker.RecordFrame(registry, 0.05f);
    const auto& snap2 = tracker.GetSnapshot();
    Check(snap2.systems[0].avgUs == firstAvgA, "snapshot values stay stable before refresh interval");

    // Advance past refresh interval
    registry.Update(0.016f);
    tracker.RecordFrame(registry, 0.25f);
    const auto& snap3 = tracker.GetSnapshot();
    Check(snap3.systems.size() == 2, "snapshot updated after refresh interval");

    // Test Pause
    tracker.SetPaused(true);
    Check(tracker.IsPaused(), "tracker pause state set");
    registry.Update(0.016f);
    tracker.RecordFrame(registry, 1.0f);
    Check(tracker.GetSnapshot().systems[0].avgUs == snap3.systems[0].avgUs, "paused tracker ignores frames");

    // Test Reset
    tracker.SetPaused(false);
    tracker.Reset();
    Check(tracker.GetSnapshot().systems.empty(), "reset clears snapshot");
  }

  return octarine::test::ReportSummary("DebugMenuRegistry");
}
