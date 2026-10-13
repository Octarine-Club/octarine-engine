#include "Systems/MagnetSystem.h"

#include <cmath>
#include <sol/sol.hpp>

#include "Components/BoxColliderComponent.h"
#include "Components/ScriptComponent.h"

static glm::vec2 GetCenter(const PositionComponent& pos, const BoxColliderComponent* box) {
  if (box != nullptr) {
    return pos.value + glm::vec2(box->offset.x + static_cast<float>(box->width) / 2.0f,
                                 box->offset.y + static_cast<float>(box->height) / 2.0f);
  }
  return pos.value;
}

static float GetAttractionRadius(Registry* registry, Entity e, const MagnetComponent& mag) {
  float radius = mag.attraction_radius;
  Entity ref = registry->IsAlive(mag.reference_entity) ? mag.reference_entity : e;
  if (!registry->HasComponent<ScriptComponent>(ref)) {
    return radius;
  }
  auto& script = registry->GetComponent<ScriptComponent>(ref);
  if (script.script_table == sol::lua_nil) {
    return radius;
  }
  sol::state_view lua(script.script_table.lua_state());
  sol::table world = lua["world"];
  if (!world.valid()) {
    return radius;
  }
  sol::table player_stats = world["player_stats"];
  if (!player_stats.valid()) {
    return radius;
  }
  return radius * player_stats.get_or("pickup_range", 1.0f);
}

static void ApplyAttraction(PositionComponent& pos, const BoxColliderComponent* box, const MagnetComponent& mag,
                            const std::vector<MagnetSystem::Attractor>& attractors, float dt) {
  if (mag.layer == 0) {
    return;
  }

  const glm::vec2 center = GetCenter(pos, box);
  float min_d2 = -1.0f;
  float best_dx = 0.0f;
  float best_dy = 0.0f;
  bool found = false;

  for (const auto& attractor : attractors) {
    if ((attractor.attracts_mask & mag.layer) == 0) {
      continue;
    }

    const float dx = attractor.center.x - center.x;
    const float dy = attractor.center.y - center.y;
    const float d2 = dx * dx + dy * dy;

    if (d2 <= attractor.attraction_radius * attractor.attraction_radius) {
      if (!found || d2 < min_d2) {
        min_d2 = d2;
        best_dx = dx;
        best_dy = dy;
        found = true;
      }
    }
  }

  if (!found) {
    return;
  }

  const float d = std::sqrt(min_d2);
  if (d > 0.0001f) {
    pos.value.x += (best_dx / d) * mag.speed * dt;
    pos.value.y += (best_dy / d) * mag.speed * dt;
  }
}

void MagnetSystem::operator()(const ContextFacade& context) {
  if (context.IsPaused()) return;
  auto* registry = context.GetRegistry();
  const float dt = context.GetDeltaTime();

  if (!query_) {
    query_ = registry->CreateQuery<PositionComponent, MagnetComponent, Opt<BoxColliderComponent>>();
  }
  query_->Update();

  attractors_.clear();
  query_->ForEach([&](Entity e, PositionComponent& pos, MagnetComponent& mag, const BoxColliderComponent* box) {
    if (mag.attracts_mask != 0) {
      attractors_.push_back({GetCenter(pos, box), mag.attracts_mask, GetAttractionRadius(registry, e, mag)});
    }
  });

  if (attractors_.empty()) {
    return;
  }

  query_->ForEach([&](Entity /*e*/, PositionComponent& pos, MagnetComponent& mag, const BoxColliderComponent* box) {
    ApplyAttraction(pos, box, mag, attractors_, dt);
  });
}
