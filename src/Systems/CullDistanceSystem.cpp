#include "Systems/CullDistanceSystem.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <sol/sol.hpp>

#include "Components/ScriptComponent.h"
#include "Game/GameConfig.h"

namespace {

float RandomFloat01() {
  thread_local std::minstd_rand rng(std::random_device{}());
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng);
}

void UnregisterScriptEnemy(Registry* registry, Entity entity) {
  if (!registry->HasComponent<ScriptComponent>(entity)) {
    return;
  }
  auto& script = registry->GetComponent<ScriptComponent>(entity);
  if (script.script_table == sol::lua_nil) {
    return;
  }
  const sol::optional<int> script_id = script.script_table["id"];
  if (!script_id) {
    return;
  }
  sol::state_view lua(script.script_table.lua_state());
  sol::table world = lua["world"];
  if (!world.valid()) {
    return;
  }
  sol::function unreg = world["unregister_enemy"];
  if (unreg.valid()) {
    unreg(*script_id);
  }
}

glm::vec2 GetPlayableBounds(Registry* registry) {
  constexpr float kDefaultMapDimension = 2400.0f;
  float map_w = kDefaultMapDimension;
  float map_h = kDefaultMapDimension;
  if (const auto* config = registry->TryGet<GameConfig>(); config != nullptr) {
    if (config->playableAreaWidth > 0.0f) map_w = config->playableAreaWidth;
    if (config->playableAreaHeight > 0.0f) map_h = config->playableAreaHeight;
  }
  return {map_w, map_h};
}

void CullOrRepositionEnemy(Registry* registry, Entity entity, PositionComponent& pos,
                           const EnemySteeringComponent& steering, const glm::vec2& player_pos, float cull_dist2,
                           float map_w, float map_h, const CullDistanceSystem& system) {
  const float dx = pos.value.x - player_pos.x;
  const float dy = pos.value.y - player_pos.y;
  const float d2 = dx * dx + dy * dy;

  if (d2 <= cull_dist2) {
    return;
  }

  if (steering.is_special) {
    glm::vec2 new_pos{};
    system.PickPerimeterPosition(new_pos, player_pos, map_w, map_h);
    pos.value = new_pos;
    return;
  }

  UnregisterScriptEnemy(registry, entity);
  registry->QueueDespawnEntity(entity);
}

glm::vec2 SamplePerimeterEdge(float roll, float total_w, float total_h, const glm::vec2& player_pos, float half_ext_w,
                              float half_ext_h) {
  float sx = 0.0f;
  float sy = 0.0f;
  if (roll < total_w) {
    sx = player_pos.x - half_ext_w + RandomFloat01() * total_w;
    sy = player_pos.y - half_ext_h;
  } else if (roll < total_w * 2.0f) {
    sx = player_pos.x - half_ext_w + RandomFloat01() * total_w;
    sy = player_pos.y + half_ext_h;
  } else if (roll < total_w * 2.0f + total_h) {
    sx = player_pos.x - half_ext_w;
    sy = player_pos.y - half_ext_h + RandomFloat01() * total_h;
  } else {
    sx = player_pos.x + half_ext_w;
    sy = player_pos.y - half_ext_h + RandomFloat01() * total_h;
  }
  return {sx, sy};
}

}  // namespace

void CullDistanceSystem::operator()(const ContextFacade& context) {
  if (context.IsPaused()) return;
  auto* registry = context.GetRegistry();
  const float dt = context.GetDeltaTime();

  cull_t_ -= dt;
  if (cull_t_ > 0.0f) {
    return;
  }
  cull_t_ = 0.5f;

  if (!query_) {
    query_ = registry->CreateQuery<PositionComponent, EnemySteeringComponent>();
  }

  const glm::vec2 bounds = GetPlayableBounds(registry);
  constexpr float kCullDist = 1150.0f;
  constexpr float kCullDist2 = kCullDist * kCullDist;

  query_->Update();
  query_->ForEach([&](Entity entity, PositionComponent& pos, EnemySteeringComponent& steering) {
    if (!registry->IsAlive(steering.target_entity) ||
        !registry->HasComponent<PositionComponent>(steering.target_entity)) {
      return;
    }
    const glm::vec2 player_pos = registry->GetComponent<PositionComponent>(steering.target_entity).value;
    CullOrRepositionEnemy(registry, entity, pos, steering, player_pos, kCullDist2, bounds.x, bounds.y, *this);
  });
}

void CullDistanceSystem::PickPerimeterPosition(glm::vec2& out_pos, glm::vec2 player_pos, float map_w,
                                               float map_h) const {
  constexpr float kHalfWidth = 1280.0f / 2.0f;
  constexpr float kHalfHeight = 720.0f / 2.0f;
  constexpr float kMargin = 80.0f;
  constexpr float kBoundaryMargin = 48.0f;

  constexpr float kTotalW = (kHalfWidth + kMargin) * 2.0f;
  constexpr float kTotalH = (kHalfHeight + kMargin) * 2.0f;
  constexpr float kPerimeter = kTotalW * 2.0f + kTotalH * 2.0f;

  constexpr int kMaxAttempts = 6;
  for (int i = 0; i < kMaxAttempts; ++i) {
    const float roll = RandomFloat01() * kPerimeter;
    const glm::vec2 sample =
        SamplePerimeterEdge(roll, kTotalW, kTotalH, player_pos, kHalfWidth + kMargin, kHalfHeight + kMargin);

    if (sample.x >= 0.0f && sample.x <= map_w - kBoundaryMargin && sample.y >= 0.0f &&
        sample.y <= map_h - kBoundaryMargin) {
      out_pos = sample;
      return;
    }
  }

  const float ang = RandomFloat01() * 2.0f * std::numbers::pi_v<float>;
  constexpr float kDist = std::max(kHalfWidth, kHalfHeight) + kMargin;
  const float sx = std::clamp(player_pos.x + std::cos(ang) * kDist, 0.0f, map_w - kBoundaryMargin);
  const float sy = std::clamp(player_pos.y + std::sin(ang) * kDist, 0.0f, map_h - kBoundaryMargin);
  out_pos = glm::vec2(sx, sy);
}
