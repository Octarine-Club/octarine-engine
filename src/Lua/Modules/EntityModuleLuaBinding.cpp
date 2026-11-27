#include "Lua/Modules/EntityModuleLuaBinding.h"

#include <glm/glm.hpp>
#include <string>
#include <tuple>

#include "Components/NameComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RotationComponent.h"
#include "Components/SpriteComponent.h"
#include "Components/TextLabelComponent.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "General/AngleUnit.h"
#include "General/Logger.h"
#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Lua/LuaBindingContext.h"
#include "Lua/LuaSchemaRegistry.h"
#include "Renderer/SpriteRenderCache.h"
#include "Systems/KDTreeCollisionSystem.h"

namespace {
using octarine::lua_schema::LuaSchemaRegistry;
glm::vec2 GetEntityPosition(Registry* registry, const Entity entity) {
  if (!registry->HasComponent<PositionComponent>(entity)) {
    Logger::Error("Entity does not have PositionComponent.");
    return {0, 0};
  }
  return registry->GetComponent<PositionComponent>(entity).value;
}

std::string GetEntityName(Registry* registry, const Entity entity) {
  if (!registry->HasComponent<NameComponent>(entity)) return {};
  return registry->GetComponent<NameComponent>(entity).name;
}

void SetEntityName(Registry* registry, const Entity entity, const std::string& name) {
  registry->AddComponent(entity, NameComponent(name));
}

void ScanArchetypeForEntityName(const Archetype& arch, const std::size_t compIdx, const std::string& name,
                                Entity& bestMatch) {
  const std::size_t chunkCount = arch.GetChunkCount();
  for (std::size_t c = 0; c < chunkCount; ++c) {
    const std::size_t entCount = arch.GetEntityCountInChunk(c);
    const auto* names = arch.GetComponentArrayByIndex<NameComponent>(c, compIdx);
    for (std::size_t i = 0; i < entCount; ++i) {
      if (names[i].name != name) continue;
      const Entity candidate = arch.GetEntity(c, i);
      if (bestMatch.id == kInvalidEntityID || candidate.GetId() < bestMatch.GetId()) {
        bestMatch = candidate;
      }
    }
  }
}

sol::object FindEntityByName(sol::this_state state, Registry* registry, const std::string& name) {
  if (registry == nullptr) return sol::lua_nil;
  const auto nameComp = registry->TryComponent<NameComponent>();
  if (!nameComp.has_value()) return sol::lua_nil;
  const ComponentID nameCompId = nameComp->GetId();

  Entity bestMatch{kInvalidEntityID};
  for (const Archetype* arch : registry->ArchetypeLog()) {
    if (arch == nullptr || arch->IsEmpty()) continue;
    const std::size_t compIdx = arch->GetComponentIndex(nameCompId);
    if (compIdx == Archetype::kInvalidComponentIndex) continue;
    ScanArchetypeForEntityName(*arch, compIdx, name, bestMatch);
  }
  if (bestMatch.id == kInvalidEntityID) return sol::lua_nil;
  return sol::make_object(state, bestMatch);
}

void SetEntityPosition(Registry* registry, const Entity entity, const double x, const double y) {
  if (!registry->HasComponent<PositionComponent>(entity)) {
    Logger::Error("Entity does not have PositionComponent.");
    return;
  }
  registry->GetComponent<PositionComponent>(entity).value = glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

void SetEntityRotation(Registry* registry, const Entity entity, const float value) {
  if (!registry->HasComponent<RotationComponent>(entity)) {
    Logger::Error("Entity does not have RotationComponent.");
    return;
  }
  registry->GetComponent<RotationComponent>(entity).value = octarine::AngleUnits::ToRadians(value);
}

void SetEntitySpriteSrcRect(Registry* registry, const Entity entity, const float srcRectX, const float srcRectY) {
  if (!registry->HasComponent<SpriteComponent>(entity)) {
    Logger::Error("Entity does not have SpriteComponent.");
    return;
  }
  auto& sprite = registry->GetComponent<SpriteComponent>(entity);
  sprite.srcRect.x = srcRectX;
  sprite.srcRect.y = srcRectY;
}

void SetTextLabelFont(Registry* registry, const Entity entity, const std::string& fontId) {
  if (registry != nullptr && registry->IsAlive(entity) && registry->HasComponent<TextLabelComponent>(entity)) {
    registry->GetComponent<TextLabelComponent>(entity).fontId = fontId;
  }
}

std::string GetTextLabelFont(Registry* registry, const Entity entity) {
  if (registry != nullptr && registry->IsAlive(entity) && registry->HasComponent<TextLabelComponent>(entity)) {
    return registry->GetComponent<TextLabelComponent>(entity).fontId;
  }
  return "";
}

void SetSpriteWorldSize(Registry* registry, const Entity entity, const float width, const float height) {
  if (registry != nullptr && registry->IsAlive(entity) && registry->HasComponent<SpriteComponent>(entity)) {
    auto& sp = registry->GetComponent<SpriteComponent>(entity);
    sp.width = width;
    sp.height = height;
  }
}

std::tuple<float, float, float, float> GetSpriteSrcRect(Registry* registry, const Entity entity) {
  if (registry != nullptr && registry->IsAlive(entity) && registry->HasComponent<SpriteComponent>(entity)) {
    const auto& sp = registry->GetComponent<SpriteComponent>(entity);
    return {sp.srcRect.x, sp.srcRect.y, sp.srcRect.w, sp.srcRect.h};
  }
  return {0.0F, 0.0F, 0.0F, 0.0F};
}

void SetSpriteRegion(Registry* registry, const Entity entity, const std::string& assetId, const float x, const float y,
                     const float w, const float h) {
  if (registry == nullptr || !registry->IsAlive(entity)) return;
  if (registry->HasComponent<SpriteComponent>(entity)) {
    auto& sp = registry->GetComponent<SpriteComponent>(entity);
    if (sp.assetId != assetId) {
      sp.assetId = assetId;
      if (auto* cache = registry->TryGet<SpriteRenderCache>()) {
        cache->Forget(entity);
      }
    }
    sp.srcRect.x = x;
    sp.srcRect.y = y;
    sp.srcRect.w = w;
    sp.srcRect.h = h;
  } else {
    SpriteComponent sp(assetId, w, h, 0, true, x, y);
    sp.srcRect.w = w;
    sp.srcRect.h = h;
    registry->AddComponent(entity, sp);
  }
}

sol::table GetEntityChildren(sol::this_state state, Registry* registry, const Entity entity) {
  sol::state_view luaView(state);
  sol::table out = luaView.create_table();
  if (registry == nullptr || !registry->IsAlive(entity)) return out;
  const auto children = registry->GetChildren(entity);
  for (std::size_t i = 0; i < children.size(); ++i) {
    out[i + 1] = children[i];
  }
  return out;
}

void SetEntityParent(Registry* registry, const Entity child, const Entity parent) {
  if (child.id == parent.id) {
    Logger::Error("set_parent: entity cannot be its own parent.");
    return;
  }
  // Reject cycles: the new parent must not already be a descendant of the child.
  for (auto ancestor = registry->GetParent(parent); ancestor.has_value(); ancestor = registry->GetParent(*ancestor)) {
    if (ancestor->id == child.id) {
      Logger::Error("set_parent: would create a hierarchy cycle.");
      return;
    }
  }
  registry->SetParent(child, parent);
}

void BlamEntities(Registry* registry, const sol::table& entities) {
  for (const auto& [key, value] : entities) {
    if (!value.is<Entity>()) {
      Logger::Error("blam: table entry is not an entity; skipping.");
      continue;
    }
    registry->QueueBlamEntity(value.as<Entity>());
  }
}

bool HasEntityTag(const Registry* registry, const Entity entity, const std::string& tag) {
  if (registry == nullptr || tag.empty() || !registry->IsAlive(entity)) return false;
  return registry->HasTag(entity, tag);
}

void AddEntityTag(Registry* registry, const Entity entity, const std::string& tag) {
  if (registry == nullptr || tag.empty() || !registry->IsAlive(entity)) return;
  registry->AddTag(entity, tag);
}

void RemoveEntityTag(Registry* registry, const Entity entity, const std::string& tag) {
  if (registry == nullptr || tag.empty() || !registry->IsAlive(entity)) return;
  registry->RemoveTag(entity, tag);
}

void BindRegistryTable(sol::table& reg, LuaBindingContext& ctx) {
  LuaSchemaRegistry::SetFunction(reg, "registry", "is_alive",
                                 sol::overload(
                                     [&ctx](const sol::object& entity) -> bool {
                                       if (!entity.is<Entity>()) return false;
                                       return ctx.GetRegistry()->IsAlive(entity.as<Entity>());
                                     },
                                     []() -> bool { return false; }));

  LuaSchemaRegistry::SetFunction(reg, "registry", "is_paused",
                                 [&ctx]() -> bool { return ctx.GetRegistry()->IsPaused(); });
  LuaSchemaRegistry::SetFunction(reg, "registry", "set_paused",
                                 [&ctx](const bool paused) { ctx.GetRegistry()->SetPaused(paused); });

  LuaSchemaRegistry::SetFunction(reg, "registry", "get_parent",
                                 [&ctx](sol::this_state state, const Entity entity) -> sol::object {
                                   const auto parent = ctx.GetRegistry()->GetParent(entity);
                                   if (!parent.has_value()) return sol::lua_nil;
                                   return sol::make_object(state, parent.value());
                                 });

  LuaSchemaRegistry::SetFunction(reg, "registry", "set_parent", [&ctx](const Entity child, const Entity parent) {
    SetEntityParent(ctx.GetRegistry(), child, parent);
  });

  LuaSchemaRegistry::SetFunction(reg, "registry", "has_tag", [&ctx](const Entity entity, const std::string& tag) {
    return HasEntityTag(ctx.GetRegistry(), entity, tag);
  });
  LuaSchemaRegistry::SetFunction(reg, "registry", "add_tag", [&ctx](const Entity entity, const std::string& tag) {
    AddEntityTag(ctx.GetRegistry(), entity, tag);
  });
  LuaSchemaRegistry::SetFunction(reg, "registry", "remove_tag", [&ctx](const Entity entity, const std::string& tag) {
    RemoveEntityTag(ctx.GetRegistry(), entity, tag);
  });

  for (const auto& entry : LuaComponentRegistry::all()) {
    entry.bindRegistryAccessors(reg, ctx.GetRegistry());
  }
}
}  // namespace

void LuaModuleBinding<EntityModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  // Destruction is deferred: blam queues the entity and Registry::Update destroys the whole
  // batch after all systems have run, so a script can never pull a chunk out from under an
  // iterating system mid-frame.
  LuaSchemaRegistry::SetFunction(
      lua, "blam",
      sol::overload([&ctx](const Entity entity) { ctx.GetRegistry()->QueueBlamEntity(entity); },
                    [&ctx](const sol::table& entities) { BlamEntities(ctx.GetRegistry(), entities); }));
  LuaSchemaRegistry::SetFunction(lua, "get_name",
                                 [&ctx](const Entity entity) { return GetEntityName(ctx.GetRegistry(), entity); });
  LuaSchemaRegistry::SetFunction(lua, "set_name", [&ctx](const Entity entity, const std::string& name) {
    SetEntityName(ctx.GetRegistry(), entity, name);
  });
  LuaSchemaRegistry::SetFunction(lua, "find_entity_by_name", [&ctx](sol::this_state state, const std::string& name) {
    return FindEntityByName(state, ctx.GetRegistry(), name);
  });
  LuaSchemaRegistry::SetFunction(lua, "has_tag", [&ctx](const Entity entity, const std::string& tag) {
    return HasEntityTag(ctx.GetRegistry(), entity, tag);
  });
  LuaSchemaRegistry::SetFunction(lua, "add_tag", [&ctx](const Entity entity, const std::string& tag) {
    AddEntityTag(ctx.GetRegistry(), entity, tag);
  });
  LuaSchemaRegistry::SetFunction(lua, "remove_tag", [&ctx](const Entity entity, const std::string& tag) {
    RemoveEntityTag(ctx.GetRegistry(), entity, tag);
  });
  LuaSchemaRegistry::SetFunction(lua, "get_position",
                                 [&ctx](const Entity entity) { return GetEntityPosition(ctx.GetRegistry(), entity); });
  LuaSchemaRegistry::SetFunction(lua, "get_position_xy", [&ctx](const Entity entity) -> std::tuple<float, float> {
    const auto pos = GetEntityPosition(ctx.GetRegistry(), entity);
    return {pos.x, pos.y};
  });
  LuaSchemaRegistry::SetFunction(lua, "set_position", [&ctx](const Entity entity, const double x, const double y) {
    SetEntityPosition(ctx.GetRegistry(), entity, x, y);
  });
  LuaSchemaRegistry::SetFunction(lua, "set_rotation", [&ctx](const Entity entity, const float value) {
    SetEntityRotation(ctx.GetRegistry(), entity, value);
  });
  LuaSchemaRegistry::SetFunction(lua, "set_sprite_src_rect", [&ctx](const Entity entity, const float x, const float y) {
    SetEntitySpriteSrcRect(ctx.GetRegistry(), entity, x, y);
  });
  LuaSchemaRegistry::SetFunction(lua, "set_text_label_font", [&ctx](const Entity entity, const std::string& fontId) {
    SetTextLabelFont(ctx.GetRegistry(), entity, fontId);
  });
  LuaSchemaRegistry::SetFunction(lua, "get_text_label_font",
                                 [&ctx](const Entity entity) { return GetTextLabelFont(ctx.GetRegistry(), entity); });
  LuaSchemaRegistry::SetFunction(lua, "set_sprite_world_size",
                                 [&ctx](const Entity entity, const float width, const float height) {
                                   SetSpriteWorldSize(ctx.GetRegistry(), entity, width, height);
                                 });
  LuaSchemaRegistry::SetFunction(lua, "get_sprite_src_rect",
                                 [&ctx](const Entity entity) { return GetSpriteSrcRect(ctx.GetRegistry(), entity); });
  LuaSchemaRegistry::SetFunction(
      lua, "set_sprite_region",
      [&ctx](const Entity entity, const std::string& assetId, const float x, const float y, const float w,
             const float h) { SetSpriteRegion(ctx.GetRegistry(), entity, assetId, x, y, w, h); });
  LuaSchemaRegistry::SetFunction(lua, "get_entity_children", [&ctx](sol::this_state state, const Entity entity) {
    return GetEntityChildren(state, ctx.GetRegistry(), entity);
  });

  LuaSchemaRegistry::SetFunction(lua, "are_colliding", [&ctx](const Entity a, const Entity b) -> bool {
    KDTreeCollisionSystem* const* cs = ctx.GetRegistry()->TryGet<KDTreeCollisionSystem*>();
    return cs != nullptr && *cs != nullptr && (*cs)->IsOverlapping(a, b);
  });

  lua["registry"] = lua.create_table();
  sol::table reg = lua["registry"];
  BindRegistryTable(reg, ctx);
}
