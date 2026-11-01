#pragma once

#include <sol/sol.hpp>

#include "Components/UIClipComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<UIClipComponent> {
  static constexpr const char* kLuaKey = "ui_clip";
  static constexpr const char* kUsertypeName = "ui_clip_component";

  static UIClipComponent fromLua(const sol::object& data) {
    UIClipComponent clip;
    if (data.is<bool>()) {
      clip.enabled = data.as<bool>();
      return clip;
    }
    if (data.is<sol::table>()) {
      const auto t = data.as<sol::table>();
      clip.enabled = LuaComponentHelpers::SafeGetOptionalValue<bool>(t, "enabled", true);
    }
    return clip;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<UIClipComponent>(kUsertypeName, "enabled", &UIClipComponent::enabled);
  }
};
