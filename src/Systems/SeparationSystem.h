#pragma once

#include <vector>

#include "ECS/Query.h"
#include "ECS/Registry.h"

struct PositionComponent;

struct SeparationEntity {
  PositionComponent* targetPos;
  Entity entity;
  float cx, cy;
  float radius;
  float weight;
};

class SeparationSystem {
 public:
  void operator()(const ContextFacade& ctx);

 private:
  void ResolvePair(int i, int j);
  void ResolveCell(int begin1, int end1);
  void ResolveNeighbors(int cellIdx, int cx, int begin1, int end1);
  void ResolveGrid(size_t count);
  void ApplyForces(size_t count);
  void WritePositions();

  std::unique_ptr<ComponentQuery<struct PositionComponent, struct SeparationComponent>> query_;

  // Reused across frames to avoid per-frame heap allocation.
  std::vector<SeparationEntity> entities_;
  std::vector<SeparationEntity> grid_entities_;
  std::vector<int> cell_counts_;
  std::vector<int> cell_offsets_;
  std::vector<int> current_offsets_;
  std::vector<int> active_cells_;

  std::vector<float> accum_dx_;
  std::vector<float> accum_dy_;
};
