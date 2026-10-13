#include "CullDistanceSystem.h"

#include <cmath>
#include <cstdlib>
#include <numbers>
#include <sol/sol.hpp>

#include "Components/ScriptComponent.h"
#include "Game/GameConfig.h"

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

  float map_w = 2400.0f;
  float map_h = 2400.0f;
  if (const auto* config = registry->TryGet<GameConfig>(); config != nullptr) {
    if (config->playableAreaWidth > 0.0f) map_w = config->playableAreaWidth;
    if (config->playableAreaHeight > 0.0f) map_h = config->playableAreaHeight;
  }

  const float cull_dist = 1150.0f;
  const float cull_dist2 = cull_dist * cull_dist;

  // Only doing a bulk update, not parallel since we are removing entities.
  query_->Update();
  query_->ForEach([&](Entity entity, PositionComponent& pos, EnemySteeringComponent& steering) {
    if (!registry->IsAlive(steering.target_entity) ||
        !registry->HasComponent<PositionComponent>(steering.target_entity)) {
      return;
    }
    const glm::vec2 player_pos = registry->GetComponent<PositionComponent>(steering.target_entity).value;

    const float dx = pos.value.x - player_pos.x;
    const float dy = pos.value.y - player_pos.y;
    const float d2 = dx * dx + dy * dy;

    if (d2 > cull_dist2) {
      if (steering.is_special) {
        glm::vec2 new_pos;
        PickPerimeterPosition(new_pos, player_pos, map_w, map_h);
        pos.value = new_pos;
      } else {
        if (registry->HasComponent<ScriptComponent>(entity)) {
          auto& script = registry->GetComponent<ScriptComponent>(entity);
          if (script.script_table != sol::lua_nil) {
            sol::optional<int> script_id = script.script_table["id"];
            if (script_id) {
              sol::state_view lua(script.script_table.lua_state());
              sol::table world = lua["world"];
              if (world.valid()) {
                sol::function unreg = world["unregister_enemy"];
                if (unreg.valid()) {
                  unreg(*script_id);
                }
              }
            }
          }
        }
        registry->QueueDespawnEntity(entity);
      }
    }
  });
}

void CullDistanceSystem::PickPerimeterPosition(glm::vec2& out_pos, glm::vec2 player_pos, float map_w,
                                               float map_h) const {
  const float hw = 1280.0f / 2.0f;
  const float hh = 720.0f / 2.0f;
  const float margin = 80.0f;

  const float total_w = (hw + margin) * 2.0f;
  const float total_h = (hh + margin) * 2.0f;
  const float perimeter = total_w * 2.0f + total_h * 2.0f;

  for (int i = 0; i < 6; ++i) {
    const float roll = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * perimeter;
    float sx = 0.0f;
    float sy = 0.0f;

    if (roll < total_w) {
      sx = player_pos.x - (hw + margin) + (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * total_w;
      sy = player_pos.y - (hh + margin);
    } else if (roll < total_w * 2.0f) {
      sx = player_pos.x - (hw + margin) + (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * total_w;
      sy = player_pos.y + (hh + margin);
    } else if (roll < total_w * 2.0f + total_h) {
      sx = player_pos.x - (hw + margin);
      sy = player_pos.y - (hh + margin) + (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * total_h;
    } else {
      sx = player_pos.x + (hw + margin);
      sy = player_pos.y - (hh + margin) + (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * total_h;
    }

    if (sx >= 0.0f && sx <= map_w - 48.0f && sy >= 0.0f && sy <= map_h - 48.0f) {
      out_pos = glm::vec2(sx, sy);
      return;
    }
  }

  const float ang = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 2.0f * std::numbers::pi_v<float>;
  const float dist = std::max(hw, hh) + margin;
  float sx = std::max(0.0f, std::min(map_w - 48.0f, player_pos.x + std::cos(ang) * dist));
  float sy = std::max(0.0f, std::min(map_h - 48.0f, player_pos.y + std::sin(ang) * dist));
  out_pos = glm::vec2(sx, sy);
}
