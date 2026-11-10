#include <cmath>
#include <sol/sol.hpp>
#include <string>

#include "Components/AnimationComponent.h"
#include "Components/CameraComponents.h"
#include "Components/CameraFollowComponent.h"
#include "Components/LifetimeComponent.h"
#include "Components/PositionComponent.h"
#include "Components/ScriptComponent.h"
#include "Components/SpriteComponent.h"
#include "ECS/Context.h"
#include "ECS/EntityPool.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "Systems/AnimationSystem.h"
#include "Systems/CameraFollowSystem.h"
#include "Systems/LifetimeSystem.h"
#include "Systems/ScriptSystem.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

struct DummyPooledComponent {
  int value = 42;
};

void TestAnimationSystem() {
  AnimationSystem animSystem;
  SpriteComponent sprite;
  sprite.width = 32.0f;
  sprite.height = 32.0f;

  AnimationComponent anim;
  anim.numFrames = 4;
  anim.frameRateSpeed = 10;  // 0.1s per frame
  anim.currentFrame = 0;
  anim.shouldLoop = true;
  anim.frameTimer = 0.0f;
  anim.isFinished = false;

  // 1 tick of 0.05s -> no advance
  animSystem(0.05f, sprite, anim);
  CheckEq(anim.currentFrame, 0, "0.05s does not advance 10 FPS animation");
  CheckEq(sprite.srcRect.x, 0.0f, "srcRect.x stays at frame 0");

  // Another 0.06s (total 0.11s) -> advances 1 frame
  animSystem(0.06f, sprite, anim);
  CheckEq(anim.currentFrame, 1, "0.11s total advances to frame 1");
  CheckEq(sprite.srcRect.x, 32.0f, "srcRect.x updates to frame 1");

  // Advance by 0.35s -> jumps 3 frames (from 1 to 4 -> modulo 4 = 0)
  animSystem(0.35f, sprite, anim);
  CheckEq(anim.currentFrame, 0, "advancing 3 frames wraps around to frame 0 (looping)");

  // Test non-looping animation
  AnimationComponent nonLooping;
  nonLooping.numFrames = 3;
  nonLooping.frameRateSpeed = 10;
  nonLooping.currentFrame = 0;
  nonLooping.shouldLoop = false;

  // Jump 1.0s forward (10 frames on a 3-frame animation)
  animSystem(1.0f, sprite, nonLooping);
  CheckEq(nonLooping.currentFrame, 2, "non-looping holds at last frame (numFrames - 1)");
  Check(nonLooping.isFinished, "non-looping animation marked isFinished");

  // Further updates do nothing when isFinished
  animSystem(0.5f, sprite, nonLooping);
  CheckEq(nonLooping.currentFrame, 2, "finished animation remains on last frame");
}

void TestCameraFollowSystem() {
  Registry registry;
  GameConfig config;
  config.windowWidth = 800;
  config.windowHeight = 600;
  config.playableAreaWidth = 2000.0f;
  config.playableAreaHeight = 1500.0f;
  registry.Set<GameConfig>(config);

  CameraComponent camera;
  camera.zoom = 1.0f;
  camera.viewport = {0.0f, 0.0f, 800.0f, 600.0f};
  registry.Set<CameraComponent>(camera);

  ContextFacade ctx(&registry, 0.016f, nullptr, nullptr);
  CameraFollowSystem followSystem;

  // Target at center of world (1000, 750)
  PositionComponent pos1{glm::vec2(1000.0f, 750.0f)};
  CameraFollowComponent followComp;
  followSystem(ctx, pos1, followComp);

  auto& camAfter1 = registry.Get<CameraComponent>();
  // Centered viewport: x = 1000 - 400 = 600, y = 750 - 300 = 450
  CheckEq(camAfter1.viewport.x, 600.0f, "Camera viewport centered horizontally");
  CheckEq(camAfter1.viewport.y, 450.0f, "Camera viewport centered vertically");

  // Target near top-left (clamping to 0,0)
  PositionComponent pos2{glm::vec2(10.0f, 10.0f)};
  followSystem(ctx, pos2, followComp);
  auto& camAfter2 = registry.Get<CameraComponent>();
  CheckEq(camAfter2.viewport.x, 0.0f, "Camera clamped at left boundary");
  CheckEq(camAfter2.viewport.y, 0.0f, "Camera clamped at top boundary");

  // Target beyond bottom-right (clamping to maxX, maxY)
  PositionComponent pos3{glm::vec2(2500.0f, 2500.0f)};
  followSystem(ctx, pos3, followComp);
  auto& camAfter3 = registry.Get<CameraComponent>();
  CheckEq(camAfter3.viewport.x, 2000.0f - 800.0f, "Camera clamped at right boundary");
  CheckEq(camAfter3.viewport.y, 1500.0f - 600.0f, "Camera clamped at bottom boundary");

  // Camera with 2x zoom
  camAfter3.zoom = 2.0f;
  followSystem(ctx, pos1, followComp);
  auto& camZoom = registry.Get<CameraComponent>();
  CheckEq(camZoom.viewport.w, 400.0f, "Viewport width halves at 2x zoom");
  CheckEq(camZoom.viewport.h, 300.0f, "Viewport height halves at 2x zoom");
}

void TestLifetimeSystem() {
  Registry registry;
  LifetimeSystem lifetimeSystem;

  const Entity e1 = registry.CreateEntity();
  registry.AddComponent(e1, LifetimeComponent(1.0f));

  const Entity e2 = registry.CreateEntity();
  registry.AddComponent(e2, LifetimeComponent(0.2f));

  ContextFacade ctx(&registry, 0.5f, nullptr, nullptr);
  lifetimeSystem(ctx);

  CheckEq(registry.GetComponent<LifetimeComponent>(e1).remainingDuration, 0.5f, "e1 lifetime decreased by 0.5s");
  Check(registry.GetComponent<LifetimeComponent>(e1).IsAlive(), "e1 is still alive");

  // e2 had 0.2s, decreased by 0.5s -> expired and queued for despawn
  Check(!registry.GetComponent<LifetimeComponent>(e2).IsAlive(), "e2 expired");
  registry.Update(0.0f);  // Flush pending despawns
  Check(!registry.IsAlive(e2), "e2 was despawned by LifetimeSystem");
  Check(registry.IsAlive(e1), "e1 is still alive in registry");
}

void TestEntityPoolManager() {
  Registry registry;
  EntityPoolManager poolMgr;

  size_t factoryCalls = 0;
  ArchetypeID poolId = poolMgr.RegisterPool<DummyPooledComponent>(registry, [&](Registry& reg) {
    ++factoryCalls;
    Entity e = reg.CreateEntity();
    reg.AddComponent(e, DummyPooledComponent{100});
    return e;
  });

  CheckEq(factoryCalls, 1UL, "RegisterPool creates 1 sample entity");

  // Spawn should reuse the sample entity
  Entity e1 = poolMgr.Spawn(registry, poolId);
  CheckEq(factoryCalls, 1UL, "Spawn reuses parked entity without calling factory");
  Check(registry.IsAlive(e1), "Spawned entity is alive");
  // Component was reset by resetter
  CheckEq(registry.GetComponent<DummyPooledComponent>(e1).value, 42, "Component reset to default (42)");

  // Mutate component, then park
  registry.GetComponent<DummyPooledComponent>(e1).value = 999;
  poolMgr.Park(registry, e1);

  // Spawn again -> should reuse e1 and reset component
  Entity e2 = poolMgr.Spawn(registry, poolId);
  CheckEq(e1.GetId(), e2.GetId(), "Spawn reuses previously parked entity handle");
  CheckEq(registry.GetComponent<DummyPooledComponent>(e2).value, 42, "Parked entity component reset on re-spawn");

  // Spawn when pool is empty calls factory
  Entity e3 = poolMgr.Spawn(registry, poolId);
  CheckEq(factoryCalls, 2UL, "Empty pool calls factory to create new entity");

  // Spawning unknown pool throws
  bool threw = false;
  try {
    poolMgr.Spawn(registry, 99999);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  Check(threw, "Spawn with unregistered pool ID throws runtime_error");
  registry.ValidateInvariants();
}

void TestScriptSystem() {
  sol::state lua;
  lua.open_libraries(sol::lib::base);

  ScriptSystem scriptSystem;
  Registry registry;
  const Entity e = registry.CreateEntity();

  ScriptComponent script;
  lua.script(R"(
    update_count = 0
    received_dt = 0
    function on_update(tbl, entity, dt)
      update_count = update_count + 1
      received_dt = dt
    end
    function throwing_update(tbl, entity, dt)
      error("Intentional test lua error")
    end
  )");

  script.updateFunction = lua["on_update"];
  ContextFacade ctx(&registry, 0.016f, nullptr, nullptr);
  ctx.SetEntity(e);

  scriptSystem(ctx, script);
  CheckEq(lua["update_count"].get<int>(), 1, "Script update function was called");
  CheckEq(lua["received_dt"].get<float>(), 0.016f, "Script received accurate dt");

  // Error handling: ensure calling throwing_update does not crash C++
  script.updateFunction = lua["throwing_update"];
  bool crashed = false;
  try {
    scriptSystem(ctx, script);
  } catch (...) {
    crashed = true;
  }
  Check(!crashed, "ScriptSystem gracefully catches Lua errors without throwing");
}

}  // namespace

int main() {
  TestAnimationSystem();
  TestCameraFollowSystem();
  TestLifetimeSystem();
  TestEntityPoolManager();
  TestScriptSystem();
  return octarine::test::ReportSummary("OctarineGameplaySystemsTest");
}
