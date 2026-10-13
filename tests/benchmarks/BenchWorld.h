#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <random>
#include <sol/sol.hpp>
#include <string>

#include "ECS/Entity.h"
#include "ECS/Registry.h"
#include "Engine/EngineContext.h"
#include "EventBus/EventBus.h"

namespace bench {

// Collision / entity layers, mirroring dash-vs `world.LAYER`.
namespace layer {
constexpr unsigned long kPlayer = 1;
constexpr unsigned long kEnemy = 2;
constexpr unsigned long kGem = 4;
constexpr unsigned long kWeapon = 8;
}  // namespace layer

enum class Layout : std::uint8_t {
  // Disk around the player whose radius grows with N at constant density until it hits the
  // CullDistanceSystem radius; beyond that density rises — exactly how a survivors-like horde packs.
  Horde,
  // Jittered grid at constant density centred on the player; area grows with N. Pure throughput.
  Uniform,
  // 16 dense knots (4x Horde density) on a ring around the player — worst case for spatial grids.
  Cluster,
};

struct WorldSpec {
  int enemies = 1000;
  Layout layout = Layout::Horde;
  // Target centre-to-centre spacing; dash-vs enemies are ~28 px with a +2 px separation radius.
  float spacing = 40.0f;
  // Cycle through every EnemyBehaviorType (Chase-heavy, like dash-vs waves) instead of all-Chase.
  bool mixedBehaviors = true;
  // Fraction of enemies carrying an auto-firing ProjectileEmitterComponent aimed at the player.
  float spitterFraction = 0.0f;
  // Fraction of enemies dropping hazard pools (ProjectileEmitter is_hazard).
  float trailFraction = 0.0f;
  int gems = 0;
  // Weapon zones parented to the player (exercises the hierarchical TransformSystem path).
  int orbitWeapons = 0;
  // Free-flying homing weapons with retarget_on_hit (exercises CombatSystem's retarget scan).
  int homingWeapons = 0;
  // Weapons kill (30 hp enemies) and a director respawns at the perimeter — steady-state
  // spawn/despawn churn. Otherwise enemies are effectively immortal so N stays fixed.
  bool lethal = false;
  // Player orbits the arena centre like dash-vs scenes/benchmark_10k.lua.
  bool movePlayer = true;
  std::uint32_t seed = 1337;
};

// Headless, deterministic, dash-vs-shaped world: Registry + EventBus + every singleton the
// simulation and render-prep systems read, with no window, renderer, or audio device. Enemy
// archetypes mirror scripts/lib/enemy.lua (minus the Lua ScriptComponent unless AttachLuaScripts).
class BenchWorld {
 public:
  explicit BenchWorld(const WorldSpec& spec);
  ~BenchWorld();
  BenchWorld(const BenchWorld&) = delete;
  BenchWorld& operator=(const BenchWorld&) = delete;

  [[nodiscard]] Registry& GetRegistry() { return registry_; }
  [[nodiscard]] EventBus& GetBus() { return *bus_; }
  [[nodiscard]] const std::unique_ptr<EventBus>& GetBusPtr() const { return bus_; }
  [[nodiscard]] Entity GetPlayer() const { return player_; }
  [[nodiscard]] const WorldSpec& GetSpec() const { return spec_; }
  [[nodiscard]] glm::vec2 GetCenter() const { return center_; }

  // Spawns enemies, gems, and weapons per spec.
  void SpawnPopulation();
  Entity SpawnEnemy(glm::vec2 position, int index);
  Entity SpawnGem(glm::vec2 position);
  [[nodiscard]] glm::vec2 SamplePosition(int index, int total);
  [[nodiscard]] glm::vec2 SamplePerimeter();

  // Player orbit + camera follow + (lethal) horde director. Register before the simulation so
  // registration-order tie-breaking runs the driver first each frame.
  void RegisterDriver();
  // engine_bootstrap::RegisterSimulationSystems plus the collision-event-driven systems.
  void RegisterSimulation(bool synchronousCollision = false);
  // Render-queue producers dash-vs uses (squares + sprites); call BeginFrame/EndFrame around Update.
  void RegisterRenderPrep();
  // Attaches a ScriptComponent running `luaSource` (a chunk returning a script table with
  // on_update) to every `stride`-th enemy and registers ScriptSystem.
  void AttachLuaScripts(const std::string& luaSource, int stride = 1);

  // Clears the render queue (what the renderer does after drawing).
  void BeginFrame();
  // Radix-sorts the render queue (what the renderer does before drawing).
  void EndFrame();
  // One complete headless frame: BeginFrame, Registry::Update, EndFrame.
  void Step();

  [[nodiscard]] float RandomFloat(float lo, float hi);

 private:
  void InstallSingletons();
  void SpawnPlayer();
  void SpawnWeapons();

  WorldSpec spec_;
  std::mt19937 rng_;
  glm::vec2 center_{};
  float map_size_ = 0.0f;
  Entity player_{};
  Entity enemy_tag_{};
  // Destruction order matters: Registry-held components and systems reference the Lua state and
  // EventBus, so the registry must be destroyed first (declared last).
  std::unique_ptr<sol::state> lua_;
  std::unique_ptr<EventBus> bus_;
  EngineContext context_{};
  Registry registry_;
};

}  // namespace bench
