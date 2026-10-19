#pragma once

#include <sol/sol.hpp>

#include "Components/CircleColliderComponent.h"
#include "General/Constants.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<CircleColliderComponent> {
  static constexpr const char* kLuaKey = "circle_collider";
  static constexpr const char* kUsertypeName = "circle_collider_component";

  static CircleColliderComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const float radius = SafeGetOptionalValue<float>(t, "radius", 1.0f);
    const glm::vec2 offset =
        t.get<sol::object>("offset").valid() ? SafeGetVec2(t, "offset") : SafeGetVec2(t, "position");
    const bool isFixed = SafeGetOptionalValue<bool>(t, "is_fixed", false);
    const auto collisionMask = SafeGetOptionalValue<int>(t, "collision_mask", Constants::kDefaultEntityMask);
    const auto collisionMaskBits = EntityMask(static_cast<unsigned long long>(collisionMask));
    return CircleColliderComponent(radius, offset, isFixed, collisionMaskBits);
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<CircleColliderComponent>(kUsertypeName, "radius", &CircleColliderComponent::radius, "offset",
                                              &CircleColliderComponent::offset, "is_fixed",
                                              &CircleColliderComponent::isFixed);
  }
};
