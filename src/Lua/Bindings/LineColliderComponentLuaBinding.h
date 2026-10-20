#pragma once

#include <sol/sol.hpp>

#include "Components/LineColliderComponent.h"
#include "General/Constants.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<LineColliderComponent> {
  static constexpr const char* kLuaKey = "line_collider";
  static constexpr const char* kUsertypeName = "line_collider_component";

  static LineColliderComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const glm::vec2 start = SafeGetVec2(t, "start");
    const glm::vec2 end = SafeGetVec2(t, "end");
    const auto isFixed = SafeGetOptionalValue<bool>(t, "is_fixed", false);
    const auto collisionMask = SafeGetOptionalValue<int>(t, "collision_mask", Constants::kDefaultEntityMask);
    const auto collisionMaskBits = EntityMask(static_cast<unsigned long long>(collisionMask));
    return LineColliderComponent(start, end, isFixed, collisionMaskBits);
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<LineColliderComponent>(kUsertypeName, "start", &LineColliderComponent::start, "end",
                                            &LineColliderComponent::end, "is_fixed", &LineColliderComponent::isFixed);
  }
};
