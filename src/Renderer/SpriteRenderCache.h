#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string_view>
#include <vector>

#include "ECS/Entity.h"

// Render-side cache mapping Entity -> last-resolved SDL_Texture* + atlas-slice origin + assetId
// hash + the AssetManager texture-generation the values were resolved against. Indexed directly by
// dense 32-bit `entity.GetId()` in a flat vector to eliminate hash-node allocations and pointer
// chasing across 10k-30k sprites.
//
// Single-threaded: callers must serialize Store/Forget/SweepDead/Clear. RenderSpriteSystem warms
// the cache in a serial Prepare pass and reads it in the parallel emit pass.
class SpriteRenderCache {
 public:
  struct Entry {
    std::uint64_t entityId = kInvalidEntityID;
    std::uint64_t assetIdHash = 0;
    SDL_Texture* texture = nullptr;
    SDL_FRect atlasOffset{};
    std::uint64_t generation = 0;
    bool hasAssetHash = false;
  };

  [[nodiscard]] static constexpr std::uint64_t HashAssetId(const std::string_view assetId) noexcept {
    constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    std::uint64_t hash = kFnvOffsetBasis;
    for (const char ch : assetId) {
      hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
      hash *= kFnvPrime;
    }
    return hash;
  }

  [[nodiscard]] const Entry* Lookup(const Entity entity, const std::uint64_t currentGen) const noexcept {
    const auto slotIdx = static_cast<size_t>(entity.GetId());
    if (slotIdx >= entries_.size()) return nullptr;
    const Entry& e = entries_[slotIdx];
    if (e.entityId == entity.id && e.generation == currentGen && e.generation != 0) {
      return &e;
    }
    return nullptr;
  }

  [[nodiscard]] const Entry* LookupByHash(const Entity entity, const std::uint64_t currentGen,
                                          const std::uint64_t assetHash) const noexcept {
    const Entry* e = Lookup(entity, currentGen);
    if (e != nullptr && e->hasAssetHash && e->assetIdHash == assetHash) {
      return e;
    }
    return nullptr;
  }

  [[nodiscard]] const Entry* Lookup(const Entity entity, const std::uint64_t currentGen,
                                    const std::string_view assetId) const noexcept {
    return LookupByHash(entity, currentGen, HashAssetId(assetId));
  }

  void Store(const Entity entity, SDL_Texture* texture, const SDL_FRect atlasOffset, const std::uint64_t generation) {
    EnsureSlot(entity.GetId()) = Entry{entity.id, 0, texture, atlasOffset, generation, false};
  }

  void StoreByHash(const Entity entity, SDL_Texture* texture, const SDL_FRect atlasOffset,
                   const std::uint64_t generation, const std::uint64_t assetHash) {
    EnsureSlot(entity.GetId()) = Entry{entity.id, assetHash, texture, atlasOffset, generation, true};
  }

  void Store(const Entity entity, SDL_Texture* texture, const SDL_FRect atlasOffset, const std::uint64_t generation,
             const std::string_view assetId) {
    StoreByHash(entity, texture, atlasOffset, generation, HashAssetId(assetId));
  }

  void Forget(const Entity entity) noexcept {
    const auto slotIdx = static_cast<size_t>(entity.GetId());
    if (slotIdx < entries_.size() && entries_[slotIdx].entityId == entity.id) {
      entries_[slotIdx] = Entry{};
    }
  }

  template <typename KeepPredicate>
  void SweepDead(KeepPredicate&& shouldKeep) {
    for (Entry& e : entries_) {
      if (e.entityId != kInvalidEntityID && !shouldKeep(Entity(e.entityId))) {
        e = Entry{};
      }
    }
    while (!entries_.empty() && entries_.back().entityId == kInvalidEntityID) {
      entries_.pop_back();
    }
  }

  void Clear() noexcept { entries_.clear(); }

  [[nodiscard]] size_t ActiveEntryCount() const noexcept {
    size_t count = 0;
    for (const Entry& e : entries_) {
      if (e.entityId != kInvalidEntityID) ++count;
    }
    return count;
  }

  [[nodiscard]] size_t SlotCapacity() const noexcept { return entries_.size(); }

 private:
  Entry& EnsureSlot(const std::uint32_t baseId) {
    const auto slotIdx = static_cast<size_t>(baseId);
    if (slotIdx >= entries_.size()) {
      entries_.resize(slotIdx + 1);
    }
    return entries_[slotIdx];
  }

  std::vector<Entry> entries_;
};
