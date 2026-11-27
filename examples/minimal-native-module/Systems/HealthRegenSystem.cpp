#include "Systems/HealthRegenSystem.h"

#include "ECS/Registry.h"

void HealthRegenSystem::operator()(const ContextFacade& ctx) {
  if (ctx.IsPaused()) {
    return;
  }
  auto* registry = ctx.GetRegistry();
  if (registry == nullptr) {
    return;
  }

  accum_seconds_ += ctx.GetDeltaTime();
  const int wholeSeconds = static_cast<int>(accum_seconds_);
  if (wholeSeconds <= 0) {
    return;
  }
  accum_seconds_ -= static_cast<float>(wholeSeconds);

  if (!query_) {
    query_ = registry->CreateQuery<HealthComponent>();
  }
  query_->Update();
  query_->ForEach([wholeSeconds](Entity /*entity*/, HealthComponent& health) {
    if (!health.IsDead() && health.regen_per_second > 0) {
      health.Heal(health.regen_per_second * wholeSeconds);
    }
  });
}
