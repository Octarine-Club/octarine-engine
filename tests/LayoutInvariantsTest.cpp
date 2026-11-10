#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "Components/AudioActiveTag.h"
#include "Components/BoxColliderComponent.h"
#include "Components/ChunkResidentComponent.h"
#include "Components/CircleColliderComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/LifetimeComponent.h"
#include "Components/PivotComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/RotationComponent.h"
#include "Components/ScaleComponent.h"
#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "Renderer/RenderKey.h"
#include "TestHarness.h"

using octarine::test::Check;

// ============================================================================
// Compile-time Standard Layout & Trivially Copyable Invariants
// ============================================================================
static_assert(std::is_standard_layout_v<PositionComponent>, "PositionComponent must be standard layout");
static_assert(std::is_trivially_copyable_v<PositionComponent>, "PositionComponent must be trivially copyable");

static_assert(std::is_standard_layout_v<RigidBodyComponent>, "RigidBodyComponent must be standard layout");
static_assert(std::is_trivially_copyable_v<RigidBodyComponent>, "RigidBodyComponent must be trivially copyable");

static_assert(std::is_standard_layout_v<RotationComponent>, "RotationComponent must be standard layout");
static_assert(std::is_trivially_copyable_v<RotationComponent>, "RotationComponent must be trivially copyable");

static_assert(std::is_standard_layout_v<ScaleComponent>, "ScaleComponent must be standard layout");
static_assert(std::is_trivially_copyable_v<ScaleComponent>, "ScaleComponent must be trivially copyable");

static_assert(std::is_standard_layout_v<PivotComponent>, "PivotComponent must be standard layout");
static_assert(std::is_trivially_copyable_v<PivotComponent>, "PivotComponent must be trivially copyable");

static_assert(std::is_standard_layout_v<GlobalTransformComponent>, "GlobalTransformComponent must be standard layout");
static_assert(std::is_trivially_copyable_v<GlobalTransformComponent>,
              "GlobalTransformComponent must be trivially copyable");

static_assert(std::is_standard_layout_v<LifetimeComponent>, "LifetimeComponent must be standard layout");
static_assert(std::is_trivially_copyable_v<LifetimeComponent>, "LifetimeComponent must be trivially copyable");

static_assert(std::is_standard_layout_v<EntityMaskComponent>, "EntityMaskComponent must be standard layout");
static_assert(std::is_trivially_copyable_v<EntityMaskComponent>, "EntityMaskComponent must be trivially copyable");

static_assert(std::is_standard_layout_v<ChunkResidentComponent>, "ChunkResidentComponent must be standard layout");
static_assert(std::is_trivially_copyable_v<ChunkResidentComponent>,
              "ChunkResidentComponent must be trivially copyable");

static_assert(std::is_standard_layout_v<BoxColliderComponent>, "BoxColliderComponent must be standard layout");
static_assert(std::is_trivially_copyable_v<BoxColliderComponent>, "BoxColliderComponent must be trivially copyable");

static_assert(std::is_standard_layout_v<CircleColliderComponent>, "CircleColliderComponent must be standard layout");
static_assert(std::is_trivially_copyable_v<CircleColliderComponent>,
              "CircleColliderComponent must be trivially copyable");

static_assert(std::is_standard_layout_v<AudioActiveTag>, "AudioActiveTag must be standard layout");
static_assert(std::is_trivially_copyable_v<AudioActiveTag>, "AudioActiveTag must be trivially copyable");

static_assert(std::is_standard_layout_v<Entity>, "Entity must be standard layout");
static_assert(std::is_trivially_copyable_v<Entity>, "Entity must be trivially copyable");

// ============================================================================
// Compile-time Size Invariants
// ============================================================================
static_assert(sizeof(PositionComponent) == 8, "PositionComponent size lock: 2x float (8 bytes)");
static_assert(sizeof(RigidBodyComponent) == 8, "RigidBodyComponent size lock: 2x float (8 bytes)");
static_assert(sizeof(RotationComponent) == 4, "RotationComponent size lock: 1x float (4 bytes)");
static_assert(sizeof(ScaleComponent) == 8, "ScaleComponent size lock: 2x float (8 bytes)");
static_assert(sizeof(PivotComponent) == 8, "PivotComponent size lock: 2x float (8 bytes)");
static_assert(sizeof(GlobalTransformComponent) == 28,
              "GlobalTransformComponent size lock: 7x float (28 bytes: pos 8, scale 8, rot 4, pivot 8)");
static_assert(sizeof(LifetimeComponent) == 8, "LifetimeComponent size lock: 2x float (8 bytes)");
#if defined(_WIN32)
static_assert(sizeof(EntityMaskComponent) == 4, "EntityMaskComponent size lock (LLP64: 4 bytes)");
static_assert(sizeof(ChunkResidentComponent) == 12, "ChunkResidentComponent size lock: 2x int32 + uint32 (12 bytes)");
static_assert(sizeof(BoxColliderComponent) == 24, "BoxColliderComponent size lock (LLP64: 24 bytes)");
static_assert(sizeof(CircleColliderComponent) == 20, "CircleColliderComponent size lock (LLP64: 20 bytes)");
#else
static_assert(sizeof(EntityMaskComponent) == 8, "EntityMaskComponent size lock: bitset<32> on LP64 (8 bytes)");
static_assert(sizeof(ChunkResidentComponent) == 12, "ChunkResidentComponent size lock: 2x int32 + uint32 (12 bytes)");
static_assert(sizeof(BoxColliderComponent) == 32, "BoxColliderComponent size lock: 32 bytes (aligned to 8 bytes)");
static_assert(sizeof(CircleColliderComponent) == 24,
              "CircleColliderComponent size lock: 24 bytes (aligned to 8 bytes)");
#endif
static_assert(sizeof(Entity) == 8, "Entity size lock: uint64_t handle (8 bytes)");

// ============================================================================
// Chunk & Cache Line Alignment Invariants
// ============================================================================
static_assert(kChunkSize == 16384, "kChunkSize must be 16KB");
static_assert(kChunkAlignment == 64, "kChunkAlignment must be 64 bytes (AVX2/cache line aligned)");
static_assert(kUsableSpace == kChunkSize, "kUsableSpace must equal kChunkSize");

// ============================================================================
// RenderKey 64-bit Layout & Size Invariants
// ============================================================================
static_assert(sizeof(decltype(RenderKey::sortKey)) == 8, "RenderKey::sortKey must be exactly 64 bits");
static_assert(sizeof(RenderKey) == 80, "RenderKey must be exactly 80 bytes for contiguous radix sorting");

int main() {
  // 1. Field Offsets in GlobalTransformComponent
  Check(offsetof(GlobalTransformComponent, position) == 0, "GlobalTransformComponent::position offset == 0");
  Check(offsetof(GlobalTransformComponent, scale) == 8, "GlobalTransformComponent::scale offset == 8");
  Check(offsetof(GlobalTransformComponent, rotation) == 16, "GlobalTransformComponent::rotation offset == 16");
  Check(offsetof(GlobalTransformComponent, pivot) == 20, "GlobalTransformComponent::pivot offset == 20");

  // 2. Field Offsets in BoxColliderComponent
  Check(offsetof(BoxColliderComponent, width) == 0, "BoxColliderComponent::width offset == 0");
  Check(offsetof(BoxColliderComponent, height) == 4, "BoxColliderComponent::height offset == 4");
  Check(offsetof(BoxColliderComponent, offset) == 8, "BoxColliderComponent::offset offset == 8");
  Check(offsetof(BoxColliderComponent, isFixed) == 16, "BoxColliderComponent::isFixed offset == 16");

  // 3. Chunk 64-byte Buffer Alignment
  Chunk chunk;
  const auto bufferAddress = reinterpret_cast<std::uintptr_t>(chunk.GetComponentArray(0));
  Check((bufferAddress % kChunkAlignment) == 0, "Chunk internal buffer must be 64-byte aligned");
  Check(chunk.GetEntityCount() == 0, "Initial Chunk entity count must be 0");
  Check(chunk.GetActiveCount() == 0, "Initial Chunk active count must be 0");

  // 4. RenderKey Bit Shift & Sorting Invariants
  const auto keyLayer10 = RenderKey::ComputeSortKey(10, 0.0f, RenderableType::SPRITE, nullptr, 0);
  const auto keyLayer20 = RenderKey::ComputeSortKey(20, 0.0f, RenderableType::SPRITE, nullptr, 0);
  Check(keyLayer10 < keyLayer20, "Layer 10 must sort before Layer 20");

  // Layer field sits at bits [48, 63]
  Check((keyLayer10 >> 48) == 10, "Layer 10 must occupy bits 48-63");
  Check((keyLayer20 >> 48) == 20, "Layer 20 must occupy bits 48-63");

  // Within same layer, lower depth sorts before higher depth
  const auto keyDepthNear = RenderKey::ComputeSortKey(5, -100.0f, RenderableType::SPRITE, nullptr, 0);
  const auto keyDepthFar = RenderKey::ComputeSortKey(5, 500.0f, RenderableType::SPRITE, nullptr, 0);
  Check(keyDepthNear < keyDepthFar, "Depth -100 must sort before depth +500 within same layer");

  // Primitives with same blend mode cluster together
  const auto keySquare1 = RenderKey::ComputeSortKey(1, 0.0f, RenderableType::SQUARE_PRIMITIVE, nullptr, 1);
  const auto keySquare2 = RenderKey::ComputeSortKey(1, 0.0f, RenderableType::SQUARE_PRIMITIVE, nullptr, 1);
  Check(keySquare1 == keySquare2, "Identical layer, depth, type, blend and null batchKey produce identical sortKey");

  return octarine::test::ReportSummary("LayoutInvariantsTest");
}
