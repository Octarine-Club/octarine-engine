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

class AnyContext {
 public:
  AnyContext() = default;
  virtual ~AnyContext() = default;
  AnyContext(const AnyContext&) = delete;
  AnyContext& operator=(const AnyContext&) = delete;

  AnyContext(AnyContext&&) = delete;
  AnyContext& operator=(AnyContext&&) = delete;

  [[nodiscard]] virtual Entity GetEntity() const = 0;
  [[nodiscard]] virtual Registry* GetRegistry() const = 0;
  [[nodiscard]] virtual float GetDeltaTime() const = 0;
  [[nodiscard]] virtual void* GetComponentPtr(EntityID id) = 0;
};

class ContextFacade {
 public:
  explicit ContextFacade(AnyContext* impl) : impl_(impl) {}

  [[nodiscard]] Entity GetEntity() const { return impl_->GetEntity(); }
  [[nodiscard]] Registry* GetRegistry() const { return impl_->GetRegistry(); }
  [[nodiscard]] float GetDeltaTime() const { return impl_->GetDeltaTime(); }

  // Per-entity component access via the underlying context implementation.
  template <typename T>
  T& Component() const;

  template <typename T>
  T& Component(const ComponentID id) const {
    void* ptr = impl_->GetComponentPtr(id);
    return *static_cast<T*>(ptr);
  }

 private:
  AnyContext* impl_;
};

namespace Internal {

class BulkContextImpl final : public AnyContext {
 public:
  BulkContextImpl(Registry* registry, const float dt) : registry_(registry), dt_(dt) {}

  [[nodiscard]] Entity GetEntity() const override { return Entity{}; }
  [[nodiscard]] Registry* GetRegistry() const override { return registry_; }
  [[nodiscard]] float GetDeltaTime() const override { return dt_; }
  void* GetComponentPtr(EntityID /*id*/) override {
    Logger::Error(
        "ContextFacade::Component<T>() called on a bulk-system context. "
        "Use ctx.GetRegistry() to access components directly.");
    assert(false && "ContextFacade::Component<T>() not valid in a bulk system");
    return nullptr;
  }

 private:
  Registry* registry_;
  float dt_;
};

}  // namespace Internal
