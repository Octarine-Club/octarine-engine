#pragma once

#include <cassert>
#include <type_traits>

#include "Entity.h"
#include "General/Logger.h"

template <typename T>
struct Opt {};  // marker only — never instantiated

namespace Internal {
template <typename T>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<Opt<T>> : std::true_type {};
template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

template <typename T>
struct unwrap_opt {
  using type = T;
};
template <typename T>
struct unwrap_opt<Opt<T>> {
  using type = T;
};
template <typename T>
using unwrap_opt_t = typename unwrap_opt<T>::type;

template <typename T>
struct resolve_yield {
  using type = T&;
};
template <typename T>
struct resolve_yield<Opt<T>> {
  using type = T*;
};
template <typename T>
using resolve_yield_t = typename resolve_yield<T>::type;

template <typename T>
struct resolve_pointer {
  using type = T*;
};
template <typename T>
struct resolve_pointer<Opt<T>> {
  using type = T*;
};
template <typename T>
using resolve_pointer_t = typename resolve_pointer<T>::type;
}  // namespace Internal

class Registry;

class ContextFacade {
 public:
  using GetComponentFn = void* (*)(const void*, EntityID);

  ContextFacade(Registry* registry, float dt, const void* ctxData, GetComponentFn getComponentPtr)
      : registry_(registry), dt_(dt), ctx_data_(ctxData), get_component_ptr_(getComponentPtr) {}

  [[nodiscard]] Entity GetEntity() const { return entity_; }
  [[nodiscard]] Registry* GetRegistry() const { return registry_; }
  [[nodiscard]] float GetDeltaTime() const { return dt_; }

  // Used internally to update the entity being processed.
  void SetEntity(const Entity entity) { entity_ = entity; }

  // Per-entity component access via the underlying context implementation.
  template <typename T>
  T& Component() const;

  template <typename T>
  T& Component(const ComponentID id) const {
    void* ptr = get_component_ptr_(ctx_data_, id);
    return *static_cast<T*>(ptr);
  }

 private:
  Entity entity_{};
  Registry* registry_;
  float dt_;
  const void* ctx_data_;
  GetComponentFn get_component_ptr_;
};

namespace Internal {

inline void* BulkGetComponent(const void*, EntityID) {
  Logger::Error(
      "ContextFacade::Component<T>() called on a bulk-system context. "
      "Use ctx.GetRegistry() to access components directly.");
  assert(false && "ContextFacade::Component<T>() not valid in a bulk system");
  return nullptr;
}

}  // namespace Internal
