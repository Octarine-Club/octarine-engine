#pragma once
#include <condition_variable>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

#include "Component.h"
#include "Context.h"
#include "Entity.h"
#include "General/PerfUtils.h"
#include "General/ThreadPool.h"

template <typename... TComponents>
class ArchetypeQuery {
 public:
  ArchetypeQuery() = default;

  explicit ArchetypeQuery(ArchetypeType type, std::vector<Archetype*> matching_archetypes,
                          const bool include_inactive = false)
      : type_(std::move(type)),
        matching_archetypes_(std::move(matching_archetypes)),
        include_inactive_(include_inactive) {
    assert(sizeof...(TComponents) == type_.size());
    arch_component_offsets_.reserve(matching_archetypes_.size());
    for (const auto* arch : matching_archetypes_) {
      std::array<size_t, sizeof...(TComponents)> offsets;
      for (size_t i = 0; i < type_.size(); ++i) {
        offsets[i] = arch->GetComponentIndex(type_[i]);
      }
      arch_component_offsets_.push_back(offsets);
    }
  }

  [[nodiscard]] size_t GetTotalEntityCount() const {
    size_t total = 0;
    for (const auto* arch : matching_archetypes_) {
      for (const auto& chunk : arch->chunks_) {
        total += include_inactive_ ? chunk.GetEntityCount() : chunk.GetActiveCount();
      }
    }
    return total;
  }

  // Process matching entities serially via direct chunk-batched loops.
  template <typename Func>
  void ForEach(Func&& func) const {
    for (size_t a = 0; a < matching_archetypes_.size(); ++a) {
      auto* arch = matching_archetypes_[a];
      const auto& offsets = arch_component_offsets_[a];
      for (size_t c = 0; c < arch->chunks_.size(); ++c) {
        const size_t count = include_inactive_ ? arch->chunks_[c].GetEntityCount() : arch->chunks_[c].GetActiveCount();
        if (count == 0) continue;
        ProcessChunk(arch, offsets, c, count, func);
      }
    }
  }

  // Process matching entities in parallel across chunks.
  // serialBelowEntities: threshold below which execution runs serially on the calling thread.
  template <typename Func>
  void ParallelForEach(Func&& func, const size_t serialBelowEntities = 0) const {
    const auto work = CollectChunkWork();
    if (work.empty()) return;

    if (serialBelowEntities > 0) {
      size_t totalEntities = 0;
      for (const auto& w : work) {
        totalEntities += w.entityCount;
      }
      if (totalEntities < serialBelowEntities) {
        PROFILE_COUNTER_ADD("ParallelForEach: SerialGated", 1);
        ProcessChunks(work, 0, work.size(), func);
        return;
      }
    }

    const size_t num_batches = std::min(work.size(), ThreadPool::Instance().Size());
    PROFILE_COUNTER_ADD("ParallelForEach: Batches", static_cast<long long>(num_batches));
    PROFILE_COUNTER_ADD("ParallelForEach: Chunks", static_cast<long long>(work.size()));
    if (num_batches <= 1) {
      ProcessChunks(work, 0, work.size(), func);
      return;
    }

    const size_t items_per_batch = (work.size() + num_batches - 1) / num_batches;
    // Dispatch batches to pool; caller runs the remaining batch inline.
    BatchBarrier barrier(num_batches - 1);
    for (size_t t = 0; t < num_batches - 1; ++t) {
      const size_t begin = t * items_per_batch;
      const size_t end = std::min(begin + items_per_batch, work.size());
      DispatchBatch(work, begin, end, barrier, func);
    }

    const size_t inline_begin = (num_batches - 1) * items_per_batch;
    const size_t inline_end = std::min(inline_begin + items_per_batch, work.size());
    if (inline_begin < inline_end) {
      ProcessChunks(work, inline_begin, inline_end, func);
    }

    barrier.Wait();
  }

 private:
  struct ChunkWork {
    Archetype* archetype;
    const std::array<size_t, sizeof...(TComponents)>* offsets;
    size_t chunkIdx;
    size_t entityCount;
  };

  // Synchronizes batch completion before unwinding stack.
  struct BatchBarrier {
    size_t remaining;
    std::mutex mutex;
    std::condition_variable cv;

    explicit BatchBarrier(size_t count) : remaining(count) {}

    void Signal() {
      std::lock_guard<std::mutex> lk(mutex);
      if (--remaining == 0) {
        cv.notify_one();
      }
    }

    void Wait() {
      std::unique_lock<std::mutex> lk(mutex);
      cv.wait(lk, [this] { return remaining == 0; });
    }
  };
  using ArrayTuple = std::tuple<Internal::resolve_pointer_t<TComponents>...>;

  // Resolves typed component arrays for a single chunk.
  auto ResolveChunkArrays(const Archetype* arch, const std::array<size_t, sizeof...(TComponents)>& offsets,
                          const size_t chunkIdx) const {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      return ArrayTuple{[&]() -> Internal::resolve_pointer_t<TComponents> {
        using Comp = std::tuple_element_t<Is, std::tuple<TComponents...>>;
        using RawT = Internal::unwrap_opt_t<Comp>;
        if constexpr (Internal::is_optional_v<Comp>) {
          if (offsets[Is] == Archetype::kInvalidComponentIndex) return nullptr;
        }
        return arch->template GetComponentArrayByIndex<RawT>(chunkIdx, offsets[Is]);
      }()...};
    }(std::index_sequence_for<TComponents...>{});
  }

  // Yields a component reference (required) or pointer (optional) at the given entity index.
  template <std::size_t I>
  static auto ResolveComponent(const ArrayTuple& arrays, size_t e)
      -> Internal::resolve_yield_t<std::tuple_element_t<I, std::tuple<TComponents...>>> {
    using Comp = std::tuple_element_t<I, std::tuple<TComponents...>>;
    auto* array = std::get<I>(arrays);
    if constexpr (Internal::is_optional_v<Comp>) {
      return array ? &array[e] : nullptr;
    } else {
      return array[e];
    }
  }

  // Dispatches a single entity to the callback with resolved component arguments.
  template <typename Func, std::size_t... Is>
  static void InvokePerEntity(Func& func, Entity entity, const ArrayTuple& arrays, size_t e,
                              std::index_sequence<Is...>) {
    if constexpr (std::is_invocable_v<Func, Entity, Internal::resolve_yield_t<TComponents>...>) {
      func(entity, ResolveComponent<Is>(arrays, e)...);
    } else if constexpr (std::is_invocable_v<Func, Internal::resolve_yield_t<TComponents>...>) {
      func(ResolveComponent<Is>(arrays, e)...);
    } else {
      static_assert(!std::is_same_v<Func, Func>,
                    "The function passed to ForEach does not match the required signatures. "
                    "Expected one of: void(Entity, T&..., U*...) or void(T&..., U*...).");
    }
  }

  // Resolves typed component arrays and iterates entities within a single chunk.
  template <typename Func>
  void ProcessChunk(Archetype* arch, const std::array<size_t, sizeof...(TComponents)>& offsets, size_t chunkIdx,
                    size_t count, Func& func) const {
    const auto arrays = ResolveChunkArrays(arch, offsets, chunkIdx);
    const Entity* entities = arch->chunks_[chunkIdx].GetEntityArray();
    for (size_t e = 0; e < count; ++e) {
      InvokePerEntity(func, entities[e], arrays, e, std::index_sequence_for<TComponents...>{});
    }
  }

  std::vector<ChunkWork> CollectChunkWork() const {
    std::vector<ChunkWork> work;
    for (size_t a = 0; a < matching_archetypes_.size(); ++a) {
      auto* arch = matching_archetypes_[a];
      const auto& offsets = arch_component_offsets_[a];
      for (size_t c = 0; c < arch->chunks_.size(); ++c) {
        const size_t count = include_inactive_ ? arch->chunks_[c].GetEntityCount() : arch->chunks_[c].GetActiveCount();
        if (count > 0) {
          work.push_back({arch, &offsets, c, count});
        }
      }
    }
    return work;
  }

  template <typename Func>
  void DispatchBatch(const std::vector<ChunkWork>& work, size_t begin, size_t end, BatchBarrier& barrier,
                     Func& func) const {
    if (begin >= end) {
      barrier.Signal();
      return;
    }
    ThreadPool::Instance().Submit([&work, &func, this, begin, end, &barrier] {
      ProcessChunks(work, begin, end, func);
      barrier.Signal();
    });
  }

  template <typename Func>
  void ProcessChunks(const std::vector<ChunkWork>& work, size_t begin, size_t end, Func& func) const {
    for (size_t i = begin; i < end; ++i) {
      const auto& w = work[i];
      ProcessChunk(w.archetype, *w.offsets, w.chunkIdx, w.entityCount, func);
    }
  }

  ArchetypeType type_;
  std::vector<Archetype*> matching_archetypes_;
  std::vector<std::array<size_t, sizeof...(TComponents)>> arch_component_offsets_;
  bool include_inactive_ = false;
};