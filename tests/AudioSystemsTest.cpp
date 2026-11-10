#include <cmath>
#include <glm/glm.hpp>
#include <string>

#include "Audio/AudioTrackCache.h"
#include "Components/AudioActiveTag.h"
#include "Components/AudioListenerComponent.h"
#include "Components/AudioSinkComponent.h"
#include "Components/AudioSourceComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/RigidBodyComponent.h"
#include "ECS/Context.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "Systems/AudioCullingSystem.h"
#include "Systems/DopplerSystem.h"
#include "Systems/SpatialAudioSystem.h"
#include "Systems/UpdateListenerTransformSystem.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

void TestUpdateListenerTransformSystem() {
  Registry registry;
  registry.Set<AudioListenerCache>(AudioListenerCache{});

  UpdateListenerTransformSystem listenerSystem;
  ContextFacade ctx(&registry, 0.016f, nullptr, nullptr);

  // No listener entity yet
  listenerSystem(ctx);
  auto& cache1 = registry.Get<AudioListenerCache>();
  Check(!cache1.valid, "cache is invalid when no listener entity exists");

  // Create listener entity
  Entity listenerEntity = registry.CreateEntity();
  registry.AddComponent(listenerEntity, GlobalTransformComponent{glm::vec2(150.0f, 300.0f), glm::vec2(1.0f), 0.0f});
  AudioListenerComponent listenerComp;
  listenerComp.maxDistance = 500.0f;
  listenerComp.rolloff = 1.5f;
  listenerComp.dopplerFactor = 1.0f;
  listenerComp.speedOfSound = 340.0f;
  registry.AddComponent(listenerEntity, listenerComp);
  registry.AddComponent(listenerEntity, RigidBodyComponent{glm::vec2(10.0f, -5.0f)});

  listenerSystem(ctx);
  auto& cache2 = registry.Get<AudioListenerCache>();
  Check(cache2.valid, "cache is valid after listener registered");
  CheckEq(cache2.position.x, 150.0f, "cache position.x matches listener transform");
  CheckEq(cache2.position.y, 300.0f, "cache position.y matches listener transform");
  CheckEq(cache2.velocity.x, 10.0f, "cache velocity.x matches listener rigidbody");
  CheckEq(cache2.velocity.y, -5.0f, "cache velocity.y matches listener rigidbody");
  CheckEq(cache2.right.x, 1.0f, "cache right vector is unit-x");
  CheckEq(cache2.forward.y, 1.0f, "cache forward vector is unit-y");
}

void TestSpatialAudioMath() {
  // Attenuation:
  // minDist = 50, maxDist = 200, rolloff = 1.0
  const float attenClose = SpatialAudioSystem::ComputeAttenuation(20.0f, 50.0f, 200.0f, 1.0f);
  CheckEq(attenClose, 1.0f, "distance <= minDist has full attenuation (1.0)");

  const float attenFar = SpatialAudioSystem::ComputeAttenuation(250.0f, 50.0f, 200.0f, 1.0f);
  CheckEq(attenFar, 0.0f, "distance >= maxDist is completely attenuated (0.0)");

  // Mid distance: distance = 125 (t = 0.5), rolloff = 1.0 -> 1 / (1 + 0.5) = 1 / 1.5 = 0.6667
  const float attenMid = SpatialAudioSystem::ComputeAttenuation(125.0f, 50.0f, 200.0f, 1.0f);
  Check(std::abs(attenMid - (1.0f / 1.5f)) < 1e-4f, "midpoint attenuation matches inverse curve");

  // Pan:
  // Listener right = (1, 0)
  const glm::vec2 listenerRight(1.0f, 0.0f);

  // Directly to the right
  const float panRight = SpatialAudioSystem::ComputePan(glm::vec2(100.0f, 0.0f), listenerRight);
  CheckEq(panRight, 1.0f, "emitter to the right produces pan = 1.0");

  // Directly to the left
  const float panLeft = SpatialAudioSystem::ComputePan(glm::vec2(-100.0f, 0.0f), listenerRight);
  CheckEq(panLeft, -1.0f, "emitter to the left produces pan = -1.0");

  // Directly ahead (rel = 0, 100)
  const float panCenter = SpatialAudioSystem::ComputePan(glm::vec2(0.0f, 100.0f), listenerRight);
  CheckEq(panCenter, 0.0f, "emitter directly in front produces center pan = 0.0");

  // Coincident (distance = 0)
  const float panCoincident = SpatialAudioSystem::ComputePan(glm::vec2(0.0f, 0.0f), listenerRight);
  CheckEq(panCoincident, 0.0f, "coincident emitter produces center pan = 0.0");
}

void TestAudioCullingSystem() {
  Registry registry;
  GameConfig config;
  config.GetEngineOptions().audioListenerRadius = 200.0f;
  registry.Set<GameConfig>(config);

  AudioListenerCache listenerCache;
  listenerCache.valid = true;
  listenerCache.position = glm::vec2(0.0f, 0.0f);
  registry.Set<AudioListenerCache>(listenerCache);
  registry.Set<AudioTrackCache>(AudioTrackCache{});

  AudioCullingSystem cullingSystem;
  ContextFacade ctx(&registry, 0.016f, nullptr, nullptr);

  // Emitter 1 inside radius (distance 100 <= 200)
  Entity e1 = registry.CreateEntity();
  GlobalTransformComponent t1{glm::vec2(100.0f, 0.0f), glm::vec2(1.0f), 0.0f};
  AudioSourceComponent s1;
  s1.spatial = true;
  registry.AddComponent(e1, t1);
  registry.AddComponent(e1, s1);

  // Emitter 2 outside radius (distance 300 > 200)
  Entity e2 = registry.CreateEntity();
  GlobalTransformComponent t2{glm::vec2(300.0f, 0.0f), glm::vec2(1.0f), 0.0f};
  AudioSourceComponent s2;
  s2.spatial = true;
  registry.AddComponent(e2, t2);
  registry.AddComponent(e2, s2);

  cullingSystem(ctx, e1, t1, s1);
  cullingSystem(ctx, e2, t2, s2);

  cullingSystem.GetCommandBuffer().Playback(&registry);

  Check(registry.HasTag<AudioActiveTag>(e1), "e1 inside radius gained AudioActiveTag");
  Check(!registry.HasTag<AudioActiveTag>(e2), "e2 outside radius did NOT gain AudioActiveTag");

  // Now move e1 outside radius
  t1.position = glm::vec2(500.0f, 0.0f);
  cullingSystem(ctx, e1, t1, s1);
  cullingSystem.GetCommandBuffer().Playback(&registry);

  Check(!registry.HasTag<AudioActiveTag>(e1), "e1 moved outside radius lost AudioActiveTag");
  registry.ValidateInvariants();
}

void TestDopplerPhysics() {
  AudioListenerCache cache;
  cache.valid = true;
  cache.position = glm::vec2(0.0f, 0.0f);
  cache.velocity = glm::vec2(0.0f, 0.0f);
  cache.speedOfSound = 340.0f;
  cache.dopplerFactor = 1.0f;

  AudioSourceComponent source;
  source.doppler = true;

  GlobalTransformComponent transform{glm::vec2(100.0f, 0.0f), glm::vec2(1.0f), 0.0f};

  // 1. Stationary emitter, stationary listener -> ratio = 1.0
  RigidBodyComponent rbStationary{glm::vec2(0.0f, 0.0f)};
  float ratioStationary = DopplerSystem::ComputeRatio(transform, rbStationary, source, cache);
  CheckEq(ratioStationary, 1.0f, "Stationary source and listener has ratio 1.0");

  // 2. Approaching emitter: emitter at x=100 moves left towards listener at x=0 (vx = -34)
  // Observer hears HIGHER frequency -> ratio > 1.0
  RigidBodyComponent rbApproaching{glm::vec2(-34.0f, 0.0f)};
  float ratioApproaching = DopplerSystem::ComputeRatio(transform, rbApproaching, source, cache);
  Check(ratioApproaching > 1.0f,
        "Approaching emitter produces ratio > 1.0 (got " + std::to_string(ratioApproaching) + ")");

  // 3. Receding emitter: emitter at x=100 moves right away from listener at x=0 (vx = +34)
  // Observer hears LOWER frequency -> ratio < 1.0
  RigidBodyComponent rbReceding{glm::vec2(34.0f, 0.0f)};
  float ratioReceding = DopplerSystem::ComputeRatio(transform, rbReceding, source, cache);
  Check(ratioReceding < 1.0f, "Receding emitter produces ratio < 1.0 (got " + std::to_string(ratioReceding) + ")");

  // 4. Approaching listener: listener moves right towards emitter (vl = +34)
  cache.velocity = glm::vec2(34.0f, 0.0f);
  float ratioListenerApproaching = DopplerSystem::ComputeRatio(transform, rbStationary, source, cache);
  Check(ratioListenerApproaching > 1.0f,
        "Approaching listener produces ratio > 1.0 (got " + std::to_string(ratioListenerApproaching) + ")");

  // 5. Doppler disabled -> ratio = 1.0 regardless of motion
  source.doppler = false;
  float ratioDisabled = DopplerSystem::ComputeRatio(transform, rbApproaching, source, cache);
  CheckEq(ratioDisabled, 1.0f, "Disabled doppler always produces ratio 1.0");
}

}  // namespace

int main() {
  TestUpdateListenerTransformSystem();
  TestSpatialAudioMath();
  TestAudioCullingSystem();
  TestDopplerPhysics();
  return octarine::test::ReportSummary("OctarineAudioSystemsTest");
}
