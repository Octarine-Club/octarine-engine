#pragma once

#include <memory>

#include "ECS/System.h"

class EventBus;
class KDTreeCollisionSystem;
class Registry;
class TransformSystem;
class VelocityIntegrationSystem;

namespace engine_bootstrap {

// Handles to the simulation systems that other (presentation-side) systems order against.
struct SimulationSystemHandles {
  SystemHandle<VelocityIntegrationSystem> velocityIntegration;
  SystemHandle<TransformSystem> transform;
  SystemHandle<KDTreeCollisionSystem> collision;
};

// Registers the gameplay simulation tier — projectile emit/lifecycle, enemy steering, homing,
// cull, separation, magnet, combat (+ Lua combat callbacks, retaliation), velocity integration,
// off-screen despawn, transforms, collision, lifetime — plus the ordering edges between them.
//
// Shared by Game::Setup and the headless benchmark harness so the benchmarked frame cannot drift
// from the shipped one. Requires InstallPoolAndProjectile to have run (ProjectileEmitSystem and
// EntityPoolManager must already be Set) and an EngineContext with a live eventBus.
SimulationSystemHandles RegisterSimulationSystems(Registry& registry, const std::unique_ptr<EventBus>& eventBus);

}  // namespace engine_bootstrap
