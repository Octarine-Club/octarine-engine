#pragma once
#include <algorithm>
#include <array>
#include <tuple>

#include "ArchetypeQuery.h"
#include "General/PerfUtils.h"
#include "Registry.h"

namespace Internal {

template <typename... TComponents>
class ContextImpl final : public AnyContext {
 public:
  ContextImpl(Registry* registry, const float dt)
      : registry_(registry), dt_(dt), ids_{registry->Component<Internal::unwrap_opt_t<TComponents>>().GetId()...} {}

  void Update(const Entity entity, std::tuple<Internal::resolve_yield_t<TComponents>...> components) {
    entity_ = entity;
    components_ = std::apply(
        []<typename... T0>(T0&&... comps) {
          return std::make_tuple([&]() {
            if constexpr (std::is_pointer_v<std::remove_reference_t<T0>>) {
              return comps;
            } else {
              return &comps;
            }
          }()...);
        },
        components);
  }

  [[nodiscard]] Entity GetEntity() const override { return entity_; }
  [[nodiscard]] Registry* GetRegistry() const override { return registry_; }
  [[nodiscard]] float GetDeltaTime() const override { return dt_; }

  void* GetComponentPtr(EntityID id) override {
    void* ptr = nullptr;
    size_t i = 0;
    auto check = [&]<typename T0>(T0* component) {
      if (ptr) {
        return;
      }
      if (ids_[i] == id) {
        ptr = component;
      }
      ++i;
    };
    std::apply([&](auto... comps) { (check(comps), ...); }, components_);
    return ptr;
  }

 private:
  Registry* registry_;
  float dt_;
  Entity entity_{};
  std::tuple<Internal::unwrap_opt_t<TComponents>*...> components_;
  std::array<ComponentID, sizeof...(TComponents)> ids_;
};

}  // namespace Internal

class Query {
 public:
  Query() = default;
  virtual ~Query() = default;

  Query(Query&&) = default;
  Query(Query& query) = default;

  Query& operator=(Query&&) = default;
  Query& operator=(const Query& query) = default;

  virtual void Update() = 0;
};

template <typename... TComponents>
class ComponentQuery final : public Query {
 public:
  explicit ComponentQuery(Registry* registry)
      : registry_(registry), type_({(registry->Component<Internal::unwrap_opt_t<TComponents>>().GetId())...}) {
    // type_ preserves template pack order; sorted_type_ is used for archetype matching.
    RebuildSorted();
  }

  void Update() override {
    ACCUMULATE_PROFILE_SCOPE("Query::Update");
    const uint64_t current_gen = registry_->ArchetypeGeneration();
    if (current_gen == cached_generation_) {
      return;
    }
    ACCUMULATE_PROFILE_SCOPE("Query::Update (rematch)");

    if (cached_generation_ == UINT64_MAX) {
      matched_ = registry_->GetMatchingArchetypes(sorted_type_);
      if (!excluded_.empty()) {
        std::erase_if(matched_, [&](const Archetype* arch) { return IsExcluded(*arch); });
      }
    } else if (!sorted_type_.empty()) {
      // Incrementally test archetypes created since last update.
      const auto& log = registry_->ArchetypeLog();
      for (uint64_t gen = cached_generation_; gen < current_gen; ++gen) {
        Archetype* arch = log[gen];
        if (Registry::MatchesType(*arch, sorted_type_) && !IsExcluded(*arch)) {
          matched_.push_back(arch);
        }
      }
    }

    cached_generation_ = current_gen;
    archetype_query_ = ArchetypeQuery<TComponents...>(type_, matched_, include_inactive_);
  }

  // Requires tag for archetype matching without yielding it to iteration.
  ComponentQuery& WithTag(const Entity tag) {
    extra_required_.push_back(tag.GetId());
    RebuildSorted();
    cached_generation_ = UINT64_MAX;
    return *this;
  }

  ComponentQuery& WithTag(const std::string& name) { return WithTag(registry_->TagId(name)); }

  ComponentQuery& WithoutTag(const Entity tag) {
    excluded_.push_back(tag.GetId());
    cached_generation_ = UINT64_MAX;
    return *this;
  }

  ComponentQuery& WithoutTag(const std::string& name) { return WithoutTag(registry_->TagId(name)); }

  // Type-based tag filters resolved via component index.
  template <typename T>
  ComponentQuery& With() {
    return WithTag(registry_->template Tag<T>());
  }

  template <typename T>
  ComponentQuery& Without() {
    return WithoutTag(registry_->template Tag<T>());
  }

  // Includes inactive/parked entities in query iteration.
  ComponentQuery& IncludeInactive() {
    include_inactive_ = true;
    cached_generation_ = UINT64_MAX;
    return *this;
  }

  template <typename Func>
  void ForEach(Func&& func) {
    if constexpr (std::is_invocable_v<Func, ContextFacade&, Entity, TComponents&...> ||
                  std::is_invocable_v<Func, ContextFacade&, TComponents&...>) {
      ForEachWithFacade(std::forward<Func>(func));
    } else {
      archetype_query_.ForEach(std::forward<Func>(func));
    }
  }

  // Parallel ForEach distributing chunks across thread pool.
  // serialBelowEntities: threshold below which execution runs serially on calling thread.
  template <typename Func>
  void ParallelForEach(Func&& func, const size_t serialBelowEntities = 0) {
    archetype_query_.ParallelForEach(std::forward<Func>(func), serialBelowEntities);
  }

  [[nodiscard]] size_t GetCount() const { return archetype_query_.GetTotalEntityCount(); }

 private:
  // ContextFacade-based iteration doesn't support optional components yet. Systems using
  // Opt<T> must use the simpler void(Entity, T&, U*...) or void(T&, U*...) signatures.
  template <typename Func>
  void ForEachWithFacade(Func&& func) {
    static_assert(!(... || Internal::is_optional_v<TComponents>),
                  "Optional components are not yet supported in ContextFacade-based ForEach.");
    Internal::ContextImpl<TComponents...> contextImpl(registry_, registry_->DeltaTime());
    ContextFacade facade(&contextImpl);
    archetype_query_.ForEach([&](Entity entity, TComponents&... comps) {
      contextImpl.Update(entity, std::forward_as_tuple(comps...));
      if constexpr (std::is_invocable_v<Func, ContextFacade&, Entity, TComponents&...>) {
        func(facade, entity, comps...);
      } else {
        func(facade, comps...);
      }
    });
  }

  [[nodiscard]] bool IsExcluded(const Archetype& arch) const {
    return std::ranges::any_of(excluded_, [&](const ComponentID id) { return arch.HasComponent(id); });
  }

  void RebuildSorted() {
    sorted_type_.clear();
    // Only components NOT wrapped in Opt<T> drive matching.
    (
        [&] {
          if constexpr (!Internal::is_optional_v<TComponents>) {
            sorted_type_.push_back(registry_->Component<Internal::unwrap_opt_t<TComponents>>().GetId());
          }
        }(),
        ...);

    for (const ComponentID id : extra_required_) {
      if (std::ranges::find(sorted_type_, id) == sorted_type_.end()) {
        sorted_type_.push_back(id);
      }
    }
    std::ranges::sort(sorted_type_);
  }

  Registry* registry_;
  ArchetypeType type_;               // user-pack order — used by ArchetypeQuery::ForEach
  ArchetypeType sorted_type_;        // sorted ascending — used for archetype matching
  ArchetypeType extra_required_;     // additional ComponentIDs required (e.g., tag filters)
  ArchetypeType excluded_;           // ComponentIDs that disqualify an archetype
  std::vector<Archetype*> matched_;  // Persistent match list, appended to incrementally.
  ArchetypeQuery<TComponents...> archetype_query_;
  uint64_t cached_generation_{UINT64_MAX};  // Forces first Update to always match.
  bool include_inactive_ = false;
};
