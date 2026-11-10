#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "Components/GlobalTransformComponent.h"
#include "Components/PivotComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/RotationComponent.h"
#include "Components/ScaleComponent.h"
#include "ECS/CommandBuffer.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Game/EngineContext.h"
#include "General/ThreadPool.h"
#include "Systems/TransformSystem.h"
#include "Systems/VelocityIntegrationSystem.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

// ============================================================================
// Allocation Tracking Hooks
// ============================================================================

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define OCTARINE_HAS_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#define OCTARINE_HAS_SANITIZER 1
#endif
#endif

#if !defined(OCTARINE_HAS_SANITIZER)
static std::atomic<bool> g_trackAllocations{false};
static std::atomic<uint64_t> g_allocationCount{0};
static std::atomic<uint64_t> g_allocatedBytes{0};

void* operator new(std::size_t size) {
  if (g_trackAllocations.load(std::memory_order_relaxed)) {
    g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    g_allocatedBytes.fetch_add(size, std::memory_order_relaxed);
  }
  void* ptr = std::malloc(size);
  if (!ptr) throw std::bad_alloc();
  return ptr;
}

void* operator new[](std::size_t size) {
  if (g_trackAllocations.load(std::memory_order_relaxed)) {
    g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    g_allocatedBytes.fetch_add(size, std::memory_order_relaxed);
  }
  void* ptr = std::malloc(size);
  if (!ptr) throw std::bad_alloc();
  return ptr;
}

void* operator new(std::size_t size, std::align_val_t al) {
  if (g_trackAllocations.load(std::memory_order_relaxed)) {
    g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    g_allocatedBytes.fetch_add(size, std::memory_order_relaxed);
  }
  void* ptr = nullptr;
#if defined(_WIN32)
  ptr = _aligned_malloc(size, static_cast<size_t>(al));
#else
  if (posix_memalign(&ptr, static_cast<size_t>(al), size) != 0) {
    ptr = nullptr;
  }
#endif
  if (!ptr) throw std::bad_alloc();
  return ptr;
}

void* operator new[](std::size_t size, std::align_val_t al) { return ::operator new(size, al); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (g_trackAllocations.load(std::memory_order_relaxed)) {
    g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    g_allocatedBytes.fetch_add(size, std::memory_order_relaxed);
  }
  return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (g_trackAllocations.load(std::memory_order_relaxed)) {
    g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    g_allocatedBytes.fetch_add(size, std::memory_order_relaxed);
  }
  return std::malloc(size);
}

void operator delete(void* ptr) noexcept { std::free(ptr); }

void operator delete[](void* ptr) noexcept { std::free(ptr); }

void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

void operator delete(void* ptr, std::align_val_t) noexcept {
#if defined(_WIN32)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

void operator delete[](void* ptr, std::align_val_t al) noexcept { ::operator delete(ptr, al); }

void operator delete(void* ptr, std::size_t, std::align_val_t al) noexcept { ::operator delete(ptr, al); }

void operator delete[](void* ptr, std::size_t, std::align_val_t al) noexcept { ::operator delete(ptr, al); }
#endif

// ============================================================================
// Main Steady-State Simulation Test
// ============================================================================

int main() {
  ThreadPool::SetForceSerial(true);

  Registry registry;
  auto bus = std::make_unique<EventBus>();
  EngineContext ctx;
  ctx.eventBus = bus.get();
  registry.Set<EngineContext>(ctx);

  auto vel = registry.RegisterParallelSystem<PositionComponent, RigidBodyComponent>(VelocityIntegrationSystem{});
  auto trans = registry.RegisterBulkSystem<GlobalTransformComponent>(TransformSystem{});
  registry.Order(trans).After(vel);

  // Spawn 1000 steady-state simulation entities across multiple chunks
  constexpr int kNumEntities = 1000;
  for (int i = 0; i < kNumEntities; ++i) {
    const Entity e = registry.CreateEntity();
    registry.AddComponent(e, PositionComponent{glm::vec2(static_cast<float>(i), 10.0f)});
    registry.AddComponent(e, RigidBodyComponent{glm::vec2(1.5f, -0.5f)});
    registry.AddComponent(e, GlobalTransformComponent{});
    registry.AddComponent(e, ScaleComponent{glm::vec2(1.0f, 1.0f)});
    registry.AddComponent(e, RotationComponent{0.0f});
    registry.AddComponent(e, PivotComponent{glm::vec2(0.5f, 0.5f)});
  }

  // Preallocate CommandBuffer
  CommandBuffer cmd(2048);

  // Standalone query handle for chunk iteration test
  auto query = registry.CreateQuery<PositionComponent, RigidBodyComponent>();

  // Warmup frame (Tick 0): initializes queries, builds system execution DAG, populates scratch work vectors
  const float dt = 1.0f / 60.0f;
  registry.Update(dt);
  query->Update();

  // Warmup CommandBuffer
  for (int i = 0; i < 50; ++i) {
    cmd.EmplaceBlam(Entity{static_cast<EcsId>(i)});
  }
  cmd.Clear();

#if !defined(OCTARINE_HAS_SANITIZER)
  // Reset tracking counters and activate strict zero-allocation gate
  g_allocationCount.store(0);
  g_allocatedBytes.store(0);
  g_trackAllocations.store(true);
#endif

  // Run 100 steady-state frames
  constexpr int kSteadyStateFrames = 100;
  for (int frame = 0; frame < kSteadyStateFrames; ++frame) {
    // 1. Full registry update (Velocity integration + Transform resolution across all chunks)
    registry.Update(dt);

    // 2. Direct archetype chunk query iteration
    query->ForEach([](Entity, PositionComponent& pos, const RigidBodyComponent&) { pos.value.x += 0.001f; });

    // 3. CommandBuffer record and clear within capacity
    for (int i = 0; i < 50; ++i) {
      cmd.EmplaceBlam(Entity{static_cast<EcsId>(i + 5000)});
    }
    cmd.Clear();
  }

#if !defined(OCTARINE_HAS_SANITIZER)
  // Disable tracking before reporting results
  g_trackAllocations.store(false);

  CheckEq(g_allocationCount.load(), uint64_t{0}, "Steady-state frames must perform 0 heap allocations");
  CheckEq(g_allocatedBytes.load(), uint64_t{0}, "Steady-state frames must allocate 0 heap bytes");
#else
  std::cout << "[ZeroAllocFrameTest] Skipping allocation assertions under sanitizer instrumentation\n";
  Check(true, "Steady-state frames completed without crash under sanitizer");
#endif

  return octarine::test::ReportSummary("ZeroAllocFrameTest");
}
