// Integration tests for KDTreeCollisionSystem's enter/exit filtering (W2.2).
//
// KDTreeCollisionSystem is async: operator() on call N gathers the current entity positions and
// starts BVH detection; call N+1 consumes that result and emits CollisionBatchEvent. A 20 ms
// sleep between Update() calls gives the async work time to complete (it finishes in µs for
// 2-3 entities). Because the detection result is one frame behind the gather, each scenario
// requires an extra tick to flush the pipeline.
//
// gtest-free; exit code = failed-check count. Links the ECS core only.

#include <chrono>
#include <glm/glm.hpp>
#include <thread>
#include <utility>
#include <vector>

#include "Components/BoxColliderComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "ECS/Query.h"  // full ComponentQuery definition for RegisterBulkSystem dispatch
#include "ECS/Registry.h"
#include "Engine/EngineContext.h"
#include "EventBus/EventBus.h"
#include "Events/CollisionBatchEvent.h"
#include "Systems/KDTreeCollisionSystem.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

// Accumulates copies of every CollisionBatchEvent pair list received (pairs is a borrowed ref
// valid only during dispatch, so it must be copied here).
struct CollisionCapture {
  std::vector<std::vector<std::pair<Entity, Entity>>> batches;
  EventBus::SubscriptionHandle subscription;

  void OnBatch(const CollisionBatchEvent& evt) { batches.emplace_back(evt.pairs); }

  [[nodiscard]] int TotalPairs() const {
    int n = 0;
    for (const auto& b : batches) n += static_cast<int>(b.size());
    return n;
  }

  [[nodiscard]] bool Contains(const Entity a, const Entity b) const {
    for (const auto& batch : batches) {
      for (const auto& [x, y] : batch) {
        if ((x == a && y == b) || (x == b && y == a)) return true;
      }
    }
    return false;
  }
};

struct TestScene {
  Registry reg;
  std::unique_ptr<EventBus> bus;
  CollisionCapture capture;

  TestScene() : bus(std::make_unique<EventBus>()) {
    EngineContext ctx;
    ctx.eventBus = bus.get();
    reg.Set<EngineContext>(ctx);
    reg.RegisterBulkSystem(KDTreeCollisionSystem(true));
    capture.subscription =
        bus->SubscribeEvent<CollisionCapture, CollisionBatchEvent>(&capture, &CollisionCapture::OnBatch);
  }

  // Create a 32×32 AABB at top-left (x, y) with default masks (all entities interact).
  Entity MakeBox(const float x, const float y) {
    const Entity e = reg.CreateEntity();
    reg.AddComponent(e, GlobalTransformComponent{glm::vec2(x, y), glm::vec2(1.0f, 1.0f), 0.0});
    reg.AddComponent(e, BoxColliderComponent{32, 32});
    reg.AddComponent(e, EntityMaskComponent{});
    return e;
  }

  // Create a 32×32 AABB with explicit entity/collision masks for mask-pruning tests.
  Entity MakeBoxWithMasks(const float x, const float y, const EntityMask entityMask, const EntityMask collisionMask) {
    const Entity e = reg.CreateEntity();
    reg.AddComponent(e, GlobalTransformComponent{glm::vec2(x, y), glm::vec2(1.0f, 1.0f), 0.0});
    reg.AddComponent(e, BoxColliderComponent{32, 32, {}, false, collisionMask});
    reg.AddComponent(e, EntityMaskComponent{entityMask});
    return e;
  }

  Entity MakeCircle(const float x, const float y, const float radius) {
    const Entity e = reg.CreateEntity();
    reg.AddComponent(e, GlobalTransformComponent{glm::vec2(x, y), glm::vec2(1.0f, 1.0f), 0.0f});
    reg.AddComponent(e, CircleColliderComponent{radius});
    reg.AddComponent(e, EntityMaskComponent{});
    return e;
  }

  Entity MakeCapsule(const float x, const float y, const glm::vec2 start, const glm::vec2 end, const float radius) {
    const Entity e = reg.CreateEntity();
    reg.AddComponent(e, GlobalTransformComponent{glm::vec2(x, y), glm::vec2(1.0f, 1.0f), 0.0f});
    reg.AddComponent(e, CapsuleColliderComponent{start, end, radius});
    reg.AddComponent(e, EntityMaskComponent{});
    return e;
  }

  Entity MakeTriangle(const float x, const float y, const glm::vec2 p1, const glm::vec2 p2, const glm::vec2 p3) {
    const Entity e = reg.CreateEntity();
    reg.AddComponent(e, GlobalTransformComponent{glm::vec2(x, y), glm::vec2(1.0f, 1.0f), 0.0f});
    reg.AddComponent(e, TriangleColliderComponent{p1, p2, p3});
    reg.AddComponent(e, EntityMaskComponent{});
    return e;
  }

  Entity MakeLine(const float x, const float y, const glm::vec2 start, const glm::vec2 end) {
    const Entity e = reg.CreateEntity();
    reg.AddComponent(e, GlobalTransformComponent{glm::vec2(x, y), glm::vec2(1.0f, 1.0f), 0.0f});
    reg.AddComponent(e, LineColliderComponent{start, end});
    reg.AddComponent(e, EntityMaskComponent{});
    return e;
  }

  void MoveTo(const Entity e, const float x, const float y) const {
    reg.GetComponent<GlobalTransformComponent>(e).position = glm::vec2(x, y);
  }

  // Advance one engine tick synchronously.
  void Tick() { reg.Update(1.0f / 60.0f); }
};

void TestMultiShapeCollisions() {
  // --- Circle vs Circle: true Euclidean radius check (not just AABB corners).
  // Two radius-10 circles at (0,0) and (15,15): AABBs [-10,10]^2 and [5,25]^2 overlap in X and Y,
  // but center distance is sqrt(450) ≈ 21.21 > 20, so narrowphase rejects the pair.
  {
    TestScene scene;
    const Entity c1 = scene.MakeCircle(0.0f, 0.0f, 10.0f);
    const Entity c2 = scene.MakeCircle(15.0f, 15.0f, 10.0f);
    scene.Tick();
    CheckEq(scene.capture.TotalPairs(), 0, "circle-circle: AABB corner overlap rejected by Euclidean radius check");

    scene.MoveTo(c2, 12.0f, 12.0f);  // dist = sqrt(288) ≈ 16.97 <= 20
    scene.Tick();
    CheckEq(scene.capture.TotalPairs(), 1, "circle-circle: overlapping circles detected");
    Check(scene.capture.Contains(c1, c2), "circle-circle: (c1, c2) emitted");
  }

  // --- Circle vs Box: corner distance check vs edge overlap.
  // Box [0, 32]x[0, 32], Circle r=10 at (-8, -8): AABBs overlap ([-18,2] vs [0,32]),
  // but distance to corner (0,0) is sqrt(128) ≈ 11.31 > 10.
  {
    TestScene scene;
    const Entity box = scene.MakeBox(0.0f, 0.0f);
    const Entity circle = scene.MakeCircle(-8.0f, -8.0f, 10.0f);
    scene.Tick();
    CheckEq(scene.capture.TotalPairs(), 0, "circle-box: circle missing box corner rejected");

    scene.MoveTo(circle, -5.0f, 16.0f);  // 5px left of box edge, r=10 -> hits
    scene.Tick();
    CheckEq(scene.capture.TotalPairs(), 1, "circle-box: circle overlapping box edge detected");
    Check(scene.capture.Contains(box, circle), "circle-box: (box, circle) emitted");
  }

  // --- Capsule vs Circle and Capsule vs Box.
  {
    TestScene scene;
    const Entity cap = scene.MakeCapsule(0.0f, 0.0f, {-20.0f, 0.0f}, {20.0f, 0.0f}, 6.0f);
    const Entity circle = scene.MakeCircle(0.0f, 10.0f, 5.0f);  // 10px from spine <= 6+5=11
    const Entity farBox = scene.MakeBox(100.0f, 100.0f);
    scene.Tick();
    CheckEq(scene.capture.TotalPairs(), 1, "capsule-circle: overlapping capsule and circle detected");
    Check(scene.capture.Contains(cap, circle), "capsule-circle: (cap, circle) emitted");

    scene.MoveTo(farBox, 18.0f, -4.0f);  // overlaps right cap of capsule
    scene.Tick();
    Check(scene.capture.Contains(cap, farBox), "capsule-box: (cap, farBox) emitted");
  }

  // --- Triangle vs Circle, Triangle vs Box, and Triangle vs Triangle.
  {
    TestScene scene;
    const Entity tri1 = scene.MakeTriangle(0.0f, 0.0f, {0.0f, 0.0f}, {20.0f, 0.0f}, {0.0f, 20.0f});
    // Point (18, 18) is inside the triangle's AABB [0,20]x[0,20], but outside hypotenuse x+y=20
    // (distance from (18,18) to x+y=20 is 16/sqrt(2) ≈ 11.31 > r=4).
    const Entity circle = scene.MakeCircle(18.0f, 18.0f, 4.0f);
    scene.Tick();
    CheckEq(scene.capture.TotalPairs(), 0, "triangle-circle: circle in AABB corner outside hypotenuse rejected");

    scene.MoveTo(circle, 10.0f, 10.0f);  // right on hypotenuse x+y=20
    scene.Tick();
    CheckEq(scene.capture.TotalPairs(), 1, "triangle-circle: circle touching hypotenuse detected");
    Check(scene.capture.Contains(tri1, circle), "triangle-circle: (tri1, circle) emitted");

    const Entity tri2 = scene.MakeTriangle(5.0f, 5.0f, {0.0f, 0.0f}, {10.0f, 0.0f}, {0.0f, 10.0f});
    const Entity box = scene.MakeBox(15.0f, 0.0f);
    scene.Tick();
    Check(scene.capture.Contains(tri1, tri2), "triangle-triangle: overlapping triangles detected");
    Check(scene.capture.Contains(tri1, box), "triangle-box: overlapping triangle and box detected");
  }

  // --- Line vs Line, Line vs Circle, Line vs Box, Line vs Triangle.
  {
    TestScene scene;
    const Entity line1 = scene.MakeLine(0.0f, 0.0f, {-10.0f, 0.0f}, {10.0f, 0.0f});
    const Entity line2 = scene.MakeLine(0.0f, 0.0f, {0.0f, -10.0f}, {0.0f, 10.0f});
    const Entity lineParallel = scene.MakeLine(0.0f, 25.0f, {-10.0f, 0.0f}, {10.0f, 0.0f});
    scene.Tick();
    CheckEq(scene.capture.TotalPairs(), 1, "line-line: crossing segments detected, parallel disjoint ignored");
    Check(scene.capture.Contains(line1, line2), "line-line: (line1, line2) emitted");
    Check(!scene.capture.Contains(line1, lineParallel), "line-line: parallel disjoint line not emitted");

    const Entity circle = scene.MakeCircle(0.0f, 3.0f, 4.0f);  // crosses both line1 (dist 3<=4) and line2 (dist 0<=4)
    scene.Tick();
    Check(scene.capture.Contains(line1, circle), "line-circle: circle intersecting horizontal line detected");
    Check(scene.capture.Contains(line2, circle), "line-circle: circle intersecting vertical line detected");
  }
}

}  // namespace

int main() {
  // --- Entering pair is emitted on first contact.
  // Boxes at (0,0) and (16,0) with size 32 overlap by 16 px.
  {
    TestScene scene;
    const Entity a = scene.MakeBox(0.0f, 0.0f);
    const Entity b = scene.MakeBox(16.0f, 0.0f);

    scene.Tick();  // gathers overlapping boxes, starts detection
    scene.Tick();  // consumes result, emits (a, b) as entering

    CheckEq(scene.capture.TotalPairs(), 1, "entering: overlapping pair emitted on first contact");
    Check(scene.capture.Contains(a, b), "entering: correct pair (a, b) in the batch");
  }

  // --- Non-overlapping entities produce no pairs.
  // Boxes at (0,0) and (64,0): gap between maxX=32 and minX=64.
  {
    TestScene scene;
    scene.MakeBox(0.0f, 0.0f);
    scene.MakeBox(64.0f, 0.0f);

    scene.Tick();
    scene.Tick();

    CheckEq(scene.capture.TotalPairs(), 0, "no overlap: no pairs emitted");
  }

  // --- Sustained overlap is suppressed after the first contact.
  // Both boxes remain in the same position across three detection cycles; the pair
  // should appear in exactly one batch (the first).
  {
    TestScene scene;
    const Entity a = scene.MakeBox(0.0f, 0.0f);
    const Entity b = scene.MakeBox(16.0f, 0.0f);

    scene.Tick();  // gather + start detection_1 (overlap)
    scene.Tick();  // emit (a,b) entering; gather + start detection_2 (still overlap)
    scene.Tick();  // emit [] sustained; gather + start detection_3

    CheckEq(scene.capture.TotalPairs(), 1,
            "sustained: pair emitted exactly once despite two consecutive overlapping cycles");
  }

  // --- Re-entry fires the pair again after a gap.
  // Sequence: overlap → separate → overlap. The pair should appear in two separate batches.
  // Timing: detection_N consumes boxes gathered at call N, emitted at call N+1. Moving b
  // between calls N and N+1 affects detection_N+1 (gathered at call N+1).
  {
    TestScene scene;
    const Entity a = scene.MakeBox(0.0f, 0.0f);
    const Entity b = scene.MakeBox(16.0f, 0.0f);

    scene.Tick();                  // detection_1 gathers: a-b overlap
    scene.Tick();                  // emit (a,b) entering; detection_2 gathers: still overlap
    scene.MoveTo(b, 64.0f, 0.0f);  // move b apart before detection_3 gathers
    scene.Tick();                  // emit [] sustained (det_2 result); detection_3 gathers: b at 64 — no overlap
    scene.MoveTo(b, 16.0f, 0.0f);  // move b back before detection_4 gathers
    scene.Tick();                  // emit [] (det_3 no-overlap); detection_4 gathers: b at 16 — overlap
    scene.Tick();                  // emit (a,b) entering again (det_4 result; prevPairSet_ was cleared)

    CheckEq(scene.capture.TotalPairs(), 2, "re-entry: pair emitted on first contact and again after separation");
    Check(scene.capture.Contains(a, b), "re-entry: (a, b) is in the captured batches");
  }

  // --- New pair alongside a sustained pair: only the new pair fires.
  // a and b overlap from the start. c is far away, then moves to overlap a only (not b).
  // After c enters, only the (a, c) pair should appear — (a, b) must not re-fire.
  //
  // Layout: a at (0,0), b at (16,0) [overlaps a], c starts at (200,0), moves to (-20,0).
  // c at (-20,0): AABB [-20,12] overlaps a [0,32] but not b [16,48].
  {
    TestScene scene;
    const Entity a = scene.MakeBox(0.0f, 0.0f);
    const Entity b = scene.MakeBox(16.0f, 0.0f);
    const Entity c = scene.MakeBox(200.0f, 0.0f);

    scene.Tick();                   // detection_1: a-b overlap, a-c no
    scene.Tick();                   // emit (a,b) entering; detection_2: same positions
    scene.MoveTo(c, -20.0f, 0.0f);  // c now overlaps a but not b
    scene.Tick();                   // emit [] (det_2 a-b sustained); detection_3 gathers: a-b + a-c overlap
    scene.Tick();                   // emit entering pairs from det_3: only (a,c) — (a,b) already in prevPairSet_

    CheckEq(scene.capture.TotalPairs(), 2,
            "new pair: (a,b) on first entry, (a,c) when c enters — (a,b) not re-emitted");
    Check(scene.capture.Contains(a, b), "new pair: initial (a,b) entry was captured");
    Check(scene.capture.Contains(a, c), "new pair: (a,c) entry captured when c moved in");
  }

  // --- Incompatible masks suppress pairs (W2.1 mask-pruning gate).
  // Two overlapping boxes whose layers do not match either entity's collision mask must produce
  // no pairs. Box::intersects gates on canInteract before the geometric test.
  // Both entities carry entityMask=0b01 but collisionMask=0b10: neither can "see" the other's
  // layer, so (0b10 & 0b01) == 0 on both sides and canInteract is false.
  {
    TestScene scene;
    scene.MakeBoxWithMasks(0.0f, 0.0f, EntityMask(1), EntityMask(2));   // entityMask=1, hits layer 2
    scene.MakeBoxWithMasks(16.0f, 0.0f, EntityMask(1), EntityMask(2));  // entityMask=1, hits layer 2

    scene.Tick();
    scene.Tick();

    CheckEq(scene.capture.TotalPairs(), 0, "mask pruning: overlapping boxes with incompatible masks emit no pairs");
  }

  TestMultiShapeCollisions();

  return octarine::test::Result();
}
