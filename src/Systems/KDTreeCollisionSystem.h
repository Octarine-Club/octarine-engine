#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Components/BoxColliderComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "ECS/Entity.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Engine/EngineContext.h"
#include "EventBus/EventBus.h"
#include "Events/CollisionBatchEvent.h"
#include "Events/CollisionExitBatchEvent.h"
#include "General/PerfUtils.h"
#include "General/Rotation2D.h"
#include "General/ThreadPool.h"

constexpr int kMaxDimensions = 2;
// These values can be tuned for better performance.
constexpr int kMaxRecursionDepth = 64;
constexpr int kBruteforceCutoff = 32;

struct Box {
  Entity entity{};
  EntityMask entityMask{};
  EntityMask collisionMask{};
  // AABB enclosing the (possibly rotated) OBB — used by the median-cut broadphase.
  float minX{0.0f};
  float minY{0.0f};
  float maxX{0.0f};
  float maxY{0.0f};
  // OBB narrowphase state.
  float cx{0.0f};
  float cy{0.0f};
  float hx{0.0f};
  float hy{0.0f};
  octarine::Rotation2D rot{};

  [[nodiscard]] bool intersectsInDimension(const Box& other, const int dim) const {
    if (dim == 0) return !(maxX < other.minX || minX > other.maxX);
    if (dim == 1) return !(maxY < other.minY || minY > other.maxY);
    return false;
  }

  // SAT on the 4 face normals of two OBBs. Only invoked when at least one box is rotated;
  // axis-aligned pairs short-circuit on the AABB check above.
  [[nodiscard]] bool obbIntersects(const Box& other) const {
    const float ax0 = rot.cos, ay0 = rot.sin;
    const float ax1 = -rot.sin, ay1 = rot.cos;
    const float bx0 = other.rot.cos, by0 = other.rot.sin;
    const float bx1 = -other.rot.sin, by1 = other.rot.cos;
    const float dx = other.cx - cx;
    const float dy = other.cy - cy;

    const float axes[4][2] = {{ax0, ay0}, {ax1, ay1}, {bx0, by0}, {bx1, by1}};
    return std::ranges::all_of(axes, [&](const auto& axis) {
      const float ux = axis[0];
      const float uy = axis[1];
      const float aProj = hx * std::abs(ax0 * ux + ay0 * uy) + hy * std::abs(ax1 * ux + ay1 * uy);
      const float bProj = other.hx * std::abs(bx0 * ux + by0 * uy) + other.hy * std::abs(bx1 * ux + by1 * uy);
      const float distProj = std::abs(dx * ux + dy * uy);
      return distProj <= aProj + bProj;
    });
  }

  [[nodiscard]] bool intersects(const Box& other) const {
    const bool canInteract = !(collisionMask & other.entityMask).none() || !(other.collisionMask & entityMask).none();
    if (!canInteract) {
      return false;
    }
    if (!intersectsInDimension(other, 0) || !intersectsInDimension(other, 1)) return false;
    if (rot.IsIdentity() && other.rot.IsIdentity()) return true;
    return obbIntersects(other);
  }
};

struct CollisionResult {
  std::vector<std::pair<Entity, Entity>> intersectingPairs;
  std::vector<Box> boxes;
};

struct Partitions {
  int leftEnd;
  int rightStart;
};

class KDTreeCollisionSystem {
 public:
  void operator()(const ContextFacade& ctx) {
    // No scope timer here: Registry::Update already times this span as "KDTreeCollisionSystem";
    // a second name for the same span double-counts on the benchmark dashboard.
    auto* registry = ctx.GetRegistry();
    auto* eventBus = registry->Get<EngineContext>().eventBus;

    if (collisionResult_.valid() &&
        collisionResult_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      return;
    }

    if (collisionResult_.valid()) {
      PROFILE_NAMED_SCOPE("Emit Events");
      CollisionResult result = collisionResult_.get();
      EmitCollisionEvents(eventBus, result.intersectingPairs);
      cachedBoxes_ = std::move(result.boxes);
      cachedBoxes_.clear();
    }

    std::vector<Box> boxes = std::move(cachedBoxes_);
    {
      PROFILE_NAMED_SCOPE("Gather Boxes");

      if (!query_) {
        query_ = ctx.GetRegistry()->CreateQuery<GlobalTransformComponent, BoxColliderComponent, EntityMaskComponent>();
      }
      query_->Update();

      const size_t count = query_->GetCount();
      boxes.resize(count);
      std::atomic<size_t> nextIndex{0};

      query_->ParallelForEach([&](Entity entity, const GlobalTransformComponent& transform,
                                  const BoxColliderComponent& collider, const EntityMaskComponent& entityMask) {
        const size_t idx = nextIndex.fetch_add(1, std::memory_order_relaxed);
        const float w = static_cast<float>(collider.width) * transform.scale.x;
        const float h = static_cast<float>(collider.height) * transform.scale.y;
        const float hx = w * 0.5f;
        const float hy = h * 0.5f;

        // transform.position is top-left. apply collider offset (scaled).
        const float boxCx = transform.position.x + collider.offset.x * transform.scale.x + hx;
        const float boxCy = transform.position.y + collider.offset.y * transform.scale.y + hy;

        // FromRadians short-circuits the unrotated majority, and RotateAround collapses to a
        // copy for identity — so a world of axis-aligned colliders pays no trig at all here.
        const octarine::Rotation2D rot = octarine::Rotation2D::FromRadians(transform.rotation);

        // The entity turns about `position + pivot`, not about the collider's own centre, so
        // orbit the box centre around that point. Otherwise a pivoted sprite and its collider
        // separate as soon as the entity rotates. The OBB axes are unaffected — only where the
        // centre lands changes.
        const glm::vec2 centre = octarine::RotateAround({boxCx, boxCy}, transform.position + transform.pivot, rot);
        const glm::vec2 aabbHalf = octarine::RotatedHalfExtents({hx, hy}, rot);

        boxes[idx] = {.entity = entity,
                      .entityMask = entityMask.mask,
                      .collisionMask = collider.collisionMask,
                      .minX = centre.x - aabbHalf.x,
                      .minY = centre.y - aabbHalf.y,
                      .maxX = centre.x + aabbHalf.x,
                      .maxY = centre.y + aabbHalf.y,
                      .cx = centre.x,
                      .cy = centre.y,
                      .hx = hx,
                      .hy = hy,
                      .rot = rot};
      });
    }

    PROFILE_COUNTER_SET("Collision: Box count", static_cast<long long>(boxes.size()));

    if (boxes.empty()) {
      return;
    }

    collisionResult_ = StartAsyncCollisionDetection(std::move(boxes));
  }

  // Returns true if entity a and entity b are currently overlapping (sustained OR just-entered).
  // Reflects the result of the most recently completed async detection pass (one frame of lag
  // on the very first frame, stable thereafter). Safe to call from on_update or on_collision.
  [[nodiscard]] bool IsOverlapping(const Entity a, const Entity b) const {
    return prevPairSet_.contains({std::min(a.id, b.id), std::max(a.id, b.id)});
  }

 private:
  struct PairHash {
    size_t operator()(const std::pair<EntityID, EntityID>& p) const noexcept {
      // Boost-style hash_combine: golden-ratio constant spreads entropy, shifts mix bits.
      static constexpr size_t kGoldenRatio = 0x9e3779b9ULL;
      static constexpr int kLeftShift = 6;
      size_t h = std::hash<EntityID>{}(p.first);
      h ^= std::hash<EntityID>{}(p.second) + kGoldenRatio + (h << kLeftShift) + (h >> 2);
      return h;
    }
  };
  using PairSet = std::unordered_set<std::pair<EntityID, EntityID>, PairHash>;

  PairSet prevPairSet_;
  std::vector<Box> cachedBoxes_;
  std::future<CollisionResult> collisionResult_;
  std::unique_ptr<ComponentQuery<GlobalTransformComponent, BoxColliderComponent, EntityMaskComponent>> query_;

  // Diff this frame's overlaps against the previous frame and emit the enter/exit batches.
  //   - enter: pairs overlapping now but not last frame — first-contact only, so persistent
  //     overlaps (enemy pinned against a wall, stacked entities) don't re-fire every frame.
  //   - exit: pairs that overlapped last frame but no longer do.
  void EmitCollisionEvents(EventBus* eventBus, const std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    PROFILE_COUNTER_SET("Collision: Intersecting pairs", static_cast<long long>(intersectingPairs.size()));

    PairSet currentSet;
    currentSet.reserve(intersectingPairs.size());
    for (const auto& [a, b] : intersectingPairs) {
      currentSet.emplace(std::min(a.id, b.id), std::max(a.id, b.id));
    }

    std::vector<std::pair<Entity, Entity>> enteringPairs;
    enteringPairs.reserve(intersectingPairs.size());
    for (const auto& [a, b] : intersectingPairs) {
      if (!prevPairSet_.contains({std::min(a.id, b.id), std::max(a.id, b.id)})) {
        enteringPairs.emplace_back(a, b);
      }
    }

    std::vector<std::pair<Entity, Entity>> exitingPairs;
    for (const auto& [minId, maxId] : prevPairSet_) {
      if (!currentSet.contains({minId, maxId})) {
        exitingPairs.emplace_back(Entity{minId}, Entity{maxId});
      }
    }

    prevPairSet_ = std::move(currentSet);

    PROFILE_COUNTER_SET("Collision: Entering pairs", static_cast<long long>(enteringPairs.size()));
    eventBus->EmitEvent<CollisionBatchEvent>(enteringPairs);
    eventBus->EmitEvent<CollisionExitBatchEvent>(exitingPairs);
  }

  static void BruteForceHugeBoxes(const std::vector<Box>& boxes, const std::vector<Box>::iterator& hugeBoxesBegin,
                                  std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    ACCUMULATE_PROFILE_SCOPE("Huge Box Brute Force");
    for (auto hugeIt = hugeBoxesBegin; hugeIt != boxes.end(); ++hugeIt) {
      for (auto normalIt = boxes.begin(); normalIt != hugeBoxesBegin; ++normalIt) {
        if (hugeIt->intersects(*normalIt)) {
          intersectingPairs.emplace_back(hugeIt->entity, normalIt->entity);
        }
      }
      for (auto otherHugeIt = hugeIt + 1; otherHugeIt != boxes.end(); ++otherHugeIt) {
        if (hugeIt->intersects(*otherHugeIt)) {
          intersectingPairs.emplace_back(hugeIt->entity, otherHugeIt->entity);
        }
      }
    }
  }

  static void ComputeAsyncCollisions(std::vector<Box>& boxes,
                                     std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    AGGREGATE_PROFILE_SESSION("Async Box Creation");
    if (boxes.empty()) return;

    ACCUMULATE_PROFILE_SCOPE("Huge Box Extraction");

    // Dynamically calculate the huge box threshold to be scale-invariant.
    // Defines a "huge outlier" as a box >= 4x the average size.
    double sum = 0.0;
    for (const auto& b : boxes) {
      sum += std::max(b.maxX - b.minX, b.maxY - b.minY);
    }
    const double mean = sum / static_cast<double>(boxes.size());
    const auto hugeThreshold = static_cast<float>(mean * 4.0);
    // Extract any box larger than the dynamic threshold. These are typically background bounds or map
    // triggers that cross the entire level. Putting them in the KD-Tree destroys the median
    // splits and breaks the Sweep Bipartite early-outs.
    const auto hugeBoxesBegin = std::ranges::partition(boxes, [hugeThreshold](const Box& b) {
                                  return (b.maxX - b.minX) <= hugeThreshold && (b.maxY - b.minY) <= hugeThreshold;
                                }).begin();

    const int normalCount = static_cast<int>(std::distance(boxes.begin(), hugeBoxesBegin));
    if (normalCount > 0) {
      FindIntersectionsRecursive(boxes, 0, normalCount, 0, 0, intersectingPairs);
    }

    if (hugeBoxesBegin != boxes.end()) {
      BruteForceHugeBoxes(boxes, hugeBoxesBegin, intersectingPairs);
    }
  }

  [[nodiscard]] static std::future<CollisionResult> StartAsyncCollisionDetection(std::vector<Box> boxes) {
    // Run on the persistent worker pool rather than std::async(launch::async), which spawns and
    // tears down an OS thread per cycle. The promise is shared because ThreadPool::Submit takes a
    // copyable std::function; the future hands back to the same valid()/wait_for/get polling below.
    auto promise = std::make_shared<std::promise<CollisionResult>>();
    std::future<CollisionResult> result = promise->get_future();
    ThreadPool::Instance().Submit([boxes = std::move(boxes), promise]() mutable {
      std::vector<std::pair<Entity, Entity>> intersectingPairs;
      ComputeAsyncCollisions(boxes, intersectingPairs);
      promise->set_value(CollisionResult{std::move(intersectingPairs), std::move(boxes)});
    });
    return result;
  }

  static void FindIntersectionsBruteForce(const std::vector<Box>& boxes, const int begin, const int end,
                                          std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    ACCUMULATE_PROFILE_SCOPE("Brute Force Intersection");

    EntityMask entityUnion = 0;
    EntityMask collUnion = 0;
    for (int i = begin; i < end; ++i) {
      entityUnion |= boxes[static_cast<size_t>(i)].entityMask;
      collUnion |= boxes[static_cast<size_t>(i)].collisionMask;
    }
    if ((entityUnion & collUnion).none()) {
      return;
    }

    int checks = 0;
    for (int i = begin; i < end; ++i) {
      for (int j = i + 1; j < end; ++j) {
        checks++;
        const auto& bi = boxes[static_cast<size_t>(i)];
        const auto& bj = boxes[static_cast<size_t>(j)];
        if (bi.intersects(bj)) {
          intersectingPairs.emplace_back(bi.entity, bj.entity);
        }
      }
    }
#ifdef OCTARINE_PROFILING
    PROFILE_COUNTER_ADD("Collision: Brute Force Checks", checks);
#else
    (void)checks;
#endif
  }

  [[nodiscard]] static Partitions PartitionBoxes(std::vector<Box>& boxes, const int begin, const int end,
                                                 const int dimension, const float medianValue) {
    ACCUMULATE_PROFILE_SCOPE("Partition Boxes");
    const auto first = boxes.begin() + begin;
    const auto last = boxes.begin() + end;

    const auto leftEndIt = std::partition(first, last, [&](const Box& box) {
      if (dimension == 0) {
        return box.maxX < medianValue;
      }

      return box.maxY < medianValue;
    });

    const auto rightBeginIt = std::partition(leftEndIt, last, [&](const Box& box) {
      if (dimension == 0) {
        return box.minX <= medianValue;
      }

      return box.minY <= medianValue;
    });

    const int leftEnd = static_cast<int>(std::distance(boxes.begin(), leftEndIt));

    const int rightStart = static_cast<int>(std::distance(boxes.begin(), rightBeginIt));

    return Partitions{leftEnd, rightStart};
  }

  static void FindIntersectionsBruteForceBipartite(const std::vector<Box>& boxes, const int begin1, const int end1,
                                                   const int begin2, const int end2,
                                                   std::vector<std::pair<Entity, Entity>>& pairs) {
    ACCUMULATE_PROFILE_SCOPE("Brute Force Bipartite");
    int checks = 0;
    for (int i = begin1; i < end1; ++i) {
      for (int j = begin2; j < end2; ++j) {
        checks++;
        const auto& bi = boxes[static_cast<size_t>(i)];
        const auto& bj = boxes[static_cast<size_t>(j)];
        if (bi.intersects(bj)) {
          pairs.emplace_back(bi.entity, bj.entity);
        }
      }
    }
#ifdef OCTARINE_PROFILING
    PROFILE_COUNTER_ADD("Collision: Brute Force Bipartite Checks", checks);
#else
    (void)checks;
#endif
  }

  // KD-Tree inherently requires recursion and dense Bipartite bounds-checking logic.
  // NOLINTNEXTLINE(misc-no-recursion,readability-function-cognitive-complexity)
  static void FindIntersectionsSweepBipartite(std::vector<Box>& boxes, const int begin1, const int end1,
                                              const int begin2, const int end2, const int dimension,
                                              std::vector<std::pair<Entity, Entity>>& pairs) {
    ACCUMULATE_PROFILE_SCOPE("Sweep Bipartite");

    if (end1 - begin1 == 0 || end2 - begin2 == 0) return;

    // Aggregate Mask Pruning: Skips bipartite sweep entirely if the union of entity and collision masks proves no
    // interaction is possible.
    EntityMask entity1 = 0;
    EntityMask coll1 = 0;
    for (int i = begin1; i < end1; ++i) {
      entity1 |= boxes[static_cast<size_t>(i)].entityMask;
      coll1 |= boxes[static_cast<size_t>(i)].collisionMask;
    }

    EntityMask entity2 = 0;
    EntityMask coll2 = 0;
    for (int i = begin2; i < end2; ++i) {
      entity2 |= boxes[static_cast<size_t>(i)].entityMask;
      coll2 |= boxes[static_cast<size_t>(i)].collisionMask;
    }

    const bool groupCanInteract = !(coll1 & entity2).none() || !(coll2 & entity1).none();
    if (!groupCanInteract) {
      return;
    }

    if ((end1 - begin1) * (end2 - begin2) <= kBruteforceCutoff * kBruteforceCutoff) {
      FindIntersectionsBruteForceBipartite(boxes, begin1, end1, begin2, end2, pairs);
      return;
    }

    {
      ACCUMULATE_PROFILE_SCOPE("Bipartite Sort Phase");
      if (dimension == 0) {
        std::sort(boxes.begin() + begin1, boxes.begin() + end1,
                  [](const Box& a, const Box& b) { return a.minX < b.minX; });
        std::sort(boxes.begin() + begin2, boxes.begin() + end2,
                  [](const Box& a, const Box& b) { return a.minX < b.minX; });
      } else {
        std::sort(boxes.begin() + begin1, boxes.begin() + end1,
                  [](const Box& a, const Box& b) { return a.minY < b.minY; });
        std::sort(boxes.begin() + begin2, boxes.begin() + end2,
                  [](const Box& a, const Box& b) { return a.minY < b.minY; });
      }
    }

    {
      ACCUMULATE_PROFILE_SCOPE("Bipartite Sweep Phase");
      int sweepChecks = 0;
      if (dimension == 0) {
        int startJ = begin2;
        for (int i = begin1; i < end1; ++i) {
          const Box& a = boxes[static_cast<size_t>(i)];
          while (startJ < end2 && boxes[static_cast<size_t>(startJ)].maxX < a.minX) ++startJ;
          for (int j = startJ; j < end2; ++j) {
            sweepChecks++;
            const Box& b = boxes[static_cast<size_t>(j)];
            if (b.minX > a.maxX) break;
            if (b.maxY < a.minY || b.minY > a.maxY) continue;
            if (a.intersects(b)) pairs.emplace_back(a.entity, b.entity);
          }
        }
      } else {
        int startJ = begin2;
        for (int i = begin1; i < end1; ++i) {
          const Box& a = boxes[static_cast<size_t>(i)];
          while (startJ < end2 && boxes[static_cast<size_t>(startJ)].maxY < a.minY) ++startJ;
          for (int j = startJ; j < end2; ++j) {
            sweepChecks++;
            const Box& b = boxes[static_cast<size_t>(j)];
            if (b.minY > a.maxY) break;
            if (b.maxX < a.minX || b.minX > a.maxX) continue;
            if (a.intersects(b)) pairs.emplace_back(a.entity, b.entity);
          }
        }
      }
#ifdef OCTARINE_PROFILING
      PROFILE_COUNTER_ADD("Collision: Bipartite Sweep Inner Loop Checks", sweepChecks);
#else
      (void)sweepChecks;
#endif
    }
  }

  static constexpr float kHalf = 0.5f;

  // NOLINTNEXTLINE(misc-no-recursion) - KD-Tree cross-checking inherently requires recursion.
  static void ProcessSpanningBipartite(std::vector<Box>& boxes, const int begin, const int end, const int leftEnd,
                                       const int rightStart, const int dimension, const int nextDimension,
                                       std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    if (leftEnd >= rightStart) return;

    // Find the min/max bounds of the Spanning set along the current dimension
    float spanMin = std::numeric_limits<float>::max();
    float spanMax = std::numeric_limits<float>::lowest();
    for (int i = leftEnd; i < rightStart; ++i) {
      const float minV = dimension == 0 ? boxes[static_cast<size_t>(i)].minX : boxes[static_cast<size_t>(i)].minY;
      const float maxV = dimension == 0 ? boxes[static_cast<size_t>(i)].maxX : boxes[static_cast<size_t>(i)].maxY;
      if (minV < spanMin) spanMin = minV;
      if (maxV > spanMax) spanMax = maxV;
    }

    const auto activeLeftBegin = std::partition(boxes.begin() + begin, boxes.begin() + leftEnd, [=](const Box& a) {
      const float maxV = dimension == 0 ? a.maxX : a.maxY;
      return maxV < spanMin;
    });
    const int leftOverlapStart = static_cast<int>(std::distance(boxes.begin(), activeLeftBegin));

    const auto activeRightEnd = std::partition(boxes.begin() + rightStart, boxes.begin() + end, [=](const Box& a) {
      const float minV = dimension == 0 ? a.minX : a.minY;
      return minV <= spanMax;
    });
    const int rightOverlapEnd = static_cast<int>(std::distance(boxes.begin(), activeRightEnd));

    FindIntersectionsSweepBipartite(boxes, leftOverlapStart, leftEnd, leftEnd, rightStart, nextDimension,
                                    intersectingPairs);
    FindIntersectionsSweepBipartite(boxes, leftEnd, rightStart, rightStart, rightOverlapEnd, nextDimension,
                                    intersectingPairs);
  }
  // KD-Tree inherently requires recursion.
  // NOLINTNEXTLINE(misc-no-recursion)
  static void FindIntersectionsRecursive(std::vector<Box>& boxes, const int begin, const int end, int dimension,
                                         const int depth, std::vector<std::pair<Entity, Entity>>& intersectingPairs) {
    const int count = end - begin;
    if (count <= 1) {
      return;
    }

    if (count < kBruteforceCutoff || depth >= kMaxRecursionDepth) {
      FindIntersectionsBruteForce(boxes, begin, end, intersectingPairs);
      return;
    }

    const int mid = (begin + end) / 2;
    float medianValue = 0.0f;
    {
      ACCUMULATE_PROFILE_SCOPE("Find Median");
      std::nth_element(boxes.begin() + begin, boxes.begin() + mid, boxes.begin() + end,
                       [dimension](const Box& a, const Box& b) {
                         if (dimension == 0) return a.maxX + a.minX < b.maxX + b.minX;
                         return a.maxY + a.minY < b.maxY + b.minY;
                       });
      medianValue = (dimension == 0)
                        ? (boxes[static_cast<size_t>(mid)].minX + boxes[static_cast<size_t>(mid)].maxX) * kHalf
                        : (boxes[static_cast<size_t>(mid)].minY + boxes[static_cast<size_t>(mid)].maxY) * kHalf;
    }

    const auto [leftEnd, rightStart] = PartitionBoxes(boxes, begin, end, dimension, medianValue);
    const int nextDimension = (dimension + 1) % kMaxDimensions;

    FindIntersectionsRecursive(boxes, begin, leftEnd, dimension, depth + 1, intersectingPairs);
    FindIntersectionsRecursive(boxes, rightStart, end, dimension, depth + 1, intersectingPairs);
    FindIntersectionsRecursive(boxes, leftEnd, rightStart, nextDimension, depth + 1, intersectingPairs);

    ProcessSpanningBipartite(boxes, begin, end, leftEnd, rightStart, dimension, nextDimension, intersectingPairs);
  }
};
