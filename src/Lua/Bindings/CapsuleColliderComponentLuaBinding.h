#pragma once

#include <sol/sol.hpp>

#include "Components/CapsuleColliderComponent.h"
#include "General/Constants.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<CapsuleColliderComponent> {
  static constexpr const char* kLuaKey = "capsule_collider";
  static constexpr const char* kUsertypeName = "capsule_collider_component";

  static CapsuleColliderComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const glm::vec2 start = SafeGetVec2(t, "start");
    const glm::vec2 end = SafeGetVec2(t, "end");
    const float radius = SafeGetOptionalValue<float>(t, "radius", 1.0f);
    const bool isFixed = SafeGetOptionalValue<bool>(t, "is_fixed", false);
    const auto collisionMask = SafeGetOptionalValue<int>(t, "collision_mask", Constants::kDefaultEntityMask);
    const auto collisionMaskBits = EntityMask(static_cast<unsigned long long>(collisionMask));
    return CapsuleColliderComponent(start, end, radius, isFixed, collisionMaskBits);
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<CapsuleColliderComponent>(
        kUsertypeName, "start", &CapsuleColliderComponent::start, "end", &CapsuleColliderComponent::end, "radius",
        &CapsuleColliderComponent::radius, "is_fixed", &CapsuleColliderComponent::isFixed);
  }
};
