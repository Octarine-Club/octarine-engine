#pragma once

#include <sol/sol.hpp>

#include "Components/UINineSliceComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<UINineSliceComponent> {
  static constexpr const char* kLuaKey = "ui_nine_slice";
  static constexpr const char* kUsertypeName = "ui_nine_slice_component";

  static UINineSliceComponent fromLua(const sol::object& data) {
    using namespace LuaComponentHelpers;
    if (data.get_type() == sol::type::number) {
      return UINineSliceComponent(data.as<float>());
    }
    if (!data.is<sol::table>()) return UINineSliceComponent{};
    const auto t = data.as<sol::table>();
    const float border = SafeGetOptionalValue<float>(t, "border", 0.0f);
    return UINineSliceComponent(
        SafeGetOptionalValue<float>(t, "left", border), SafeGetOptionalValue<float>(t, "top", border),
        SafeGetOptionalValue<float>(t, "right", border), SafeGetOptionalValue<float>(t, "bottom", border));
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<UINineSliceComponent>(kUsertypeName, "left", &UINineSliceComponent::left, "top",
                                           &UINineSliceComponent::top, "right", &UINineSliceComponent::right, "bottom",
                                           &UINineSliceComponent::bottom);
  }
};
