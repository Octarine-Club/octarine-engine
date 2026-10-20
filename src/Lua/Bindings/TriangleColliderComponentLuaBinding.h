#pragma once

#include <sol/sol.hpp>

#include "Components/TriangleColliderComponent.h"
#include "General/Constants.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<TriangleColliderComponent> {
  static constexpr const char* kLuaKey = "triangle_collider";
  static constexpr const char* kUsertypeName = "triangle_collider_component";

  static TriangleColliderComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const glm::vec2 p1 = SafeGetVec2(t, "p1");
    const glm::vec2 p2 = SafeGetVec2(t, "p2");
    const glm::vec2 p3 = SafeGetVec2(t, "p3");
    const auto isFixed = SafeGetOptionalValue<bool>(t, "is_fixed", false);
    const auto collisionMask = SafeGetOptionalValue<int>(t, "collision_mask", Constants::kDefaultEntityMask);
    const auto collisionMaskBits = EntityMask(static_cast<unsigned long long>(collisionMask));
    return TriangleColliderComponent(p1, p2, p3, isFixed, collisionMaskBits);
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<TriangleColliderComponent>(kUsertypeName, "p1", &TriangleColliderComponent::p1, "p2",
                                                &TriangleColliderComponent::p2, "p3", &TriangleColliderComponent::p3,
                                                "is_fixed", &TriangleColliderComponent::isFixed);
  }
};
