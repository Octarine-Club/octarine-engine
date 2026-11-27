#include <sol/sol.hpp>

#include "Components/HealthComponent.h"
#include "ECS/Registry.h"
#include "Engine/EngineBootstrap.h"
#include "Engine/GameModuleHooks.h"
#include "Game/Game.h"
#include "General/Logger.h"
#include "Lua/Bindings/HealthComponentLuaBinding.h"
#include "Systems/HealthRegenSystem.h"
#include "Systems/ScriptSystem.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

void TestNativeSystemAndPauseGuard() {
  std::cout << "[MinimalNativeModule: C++ HealthComponent + HealthRegenSystem]\n";
  Registry registry;
  registry.RegisterBulkSystem(HealthRegenSystem{});

  const Entity ent = registry.CreateEntity();
  HealthComponent hc(100, 15);
  hc.Damage(40);
  registry.AddComponent(ent, hc);

  CheckEq(registry.GetComponent<HealthComponent>(ent).current_health, 60, "initial damage leaves 60 HP");

  registry.SetPaused(true);
  registry.Update(1.0F);
  CheckEq(registry.GetComponent<HealthComponent>(ent).current_health, 60, "paused system does not tick regen");

  registry.SetPaused(false);
  registry.Update(2.0F);
  CheckEq(registry.GetComponent<HealthComponent>(ent).current_health, 90, "2s tick regenerates 30 HP");

  registry.Update(2.0F);
  CheckEq(registry.GetComponent<HealthComponent>(ent).current_health, 100, "regen clamps at max_health");
  registry.ValidateInvariants();
}

void TestLuaBindingsAndModuleSurface() {
  std::cout << "[MinimalNativeModule: Lua component + module bindings]\n";
  Game game;
  sol::state& lua = game.GetLua();
  engine_bootstrap::InstallLuaLibraries(lua);
  engine_bootstrap::RegisterAllComponentBindings();

  ScriptSystem scriptSystem;
  scriptSystem.CreateLuaBindings(lua);
  engine_bootstrap::InstallLuaModules(lua, game);

  const auto res = lua.safe_script(R"LUA(
    local a = load_entity({
      components = {
        health = { max_health = 80, current_health = 50, regen_per_second = 5 }
      }
    })
    local b = load_entity({
      components = {
        health = { max_health = 40 }
      }
    })
    registry.get_health(a):heal(20)
    registry.get_health(b):damage(999)
    return registry.get_health(a).current_health,
           registry.get_health(b).is_dead,
           minimal_module.alive_health_count()
  )LUA",
                                   sol::script_pass_on_error);

  Check(res.valid(), "Lua script driving native health component and minimal_module succeeds");
  if (res.valid()) {
    const int hpA = res.get<int>(0);
    const bool deadB = res.get<bool>(1);
    const int aliveCount = res.get<int>(2);
    CheckEq(hpA, 70, "Lua heal(20) updated native HealthComponent to 70");
    Check(deadB, "Lua damage(999) marked entity B dead");
    CheckEq(aliveCount, 1, "minimal_module.alive_health_count() reports 1 living entity");
  }
  game.GetRegistry()->ValidateInvariants();
}

}  // namespace

int main() {
  Logger::Init();
  TestNativeSystemAndPauseGuard();
  TestLuaBindingsAndModuleSurface();
  return octarine::test::ReportSummary("MinimalNativeModuleTest");
}
