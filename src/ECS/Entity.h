#pragma once
#include <bitset>
#include <vector>

#include "ECS.h"

constexpr unsigned int kMaxEntityMasks = 32;
constexpr unsigned int kStartingEntityPoolSize = 1000;
constexpr unsigned int kEntityGenerationOffset = 32;

typedef std::bitset<kMaxEntityMasks> EntityMask;
typedef std::uint32_t EntityGeneration;

struct Entity {
  EntityID id;

  Entity() = default;

  explicit Entity(const EcsId t_id) : id(t_id) {}

  [[nodiscard]] std::uint32_t GetId() const { return static_cast<std::uint32_t>(id); }
  [[nodiscard]] EntityGeneration GetGeneration() const {
    return static_cast<EntityGeneration>(id >> kEntityGenerationOffset);
  }

  bool operator==(const Entity& other) const { return id == other.id; }

  bool operator!=(const Entity& other) const { return id != other.id; }

  bool operator<(const Entity& other) const { return id < other.id; }

  bool operator>(const Entity& other) const { return id > other.id; }

  bool operator<=(const Entity& other) const { return id <= other.id; }

  bool operator>=(const Entity& other) const { return id >= other.id; }

  explicit operator EntityID() const { return id; }
};

// Manages entity slot allocation and generation tracking to invalidate stale handles.
class EntityManager {
 public:
  EntityManager() : next_base_id_(kStartingEntityPoolSize) {
    available_entities_.reserve(kStartingEntityPoolSize);
    for (uint32_t i = kStartingEntityPoolSize; i > 0; --i) {
      available_entities_.push_back(i - 1);
    }
    generations_.resize(kStartingEntityPoolSize);
  }

  Entity CreateEntity() {
    std::uint32_t baseId;
    if (!available_entities_.empty()) {
      baseId = available_entities_.back();
      available_entities_.pop_back();
    } else {
      baseId = static_cast<std::uint32_t>(next_base_id_++);
      if (baseId >= generations_.size()) {
        generations_.resize(baseId + 1);
      }
    }
    const EntityID packed =
        static_cast<EntityID>(baseId) | (static_cast<EntityID>(generations_[baseId]) << kEntityGenerationOffset);
    return Entity(packed);
  }

  void BlamEntity(const Entity entity) {
    if (!IsValid(entity)) return;
    const std::uint32_t baseId = entity.GetId();
    generations_[baseId]++;
    available_entities_.push_back(baseId);
  }

  [[nodiscard]] bool IsValid(const Entity entity) const {
    const std::uint32_t baseId = entity.GetId();
    return baseId < generations_.size() && generations_[baseId] == entity.GetGeneration();
  }

  [[nodiscard]] EntityGeneration GetGeneration(const std::uint32_t baseId) const {
    return baseId < generations_.size() ? generations_[baseId] : 0;
  }

  [[nodiscard]] size_t GetPoolCapacity() const { return generations_.size(); }

 private:
  std::vector<std::uint32_t> available_entities_;
  std::vector<EntityGeneration> generations_;
  EntityID next_base_id_ = 0;
};