#pragma once

#include <sol/sol.hpp>

#include "Components/UIStyleStateComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<UIStyleStateComponent> {
  static constexpr const char* kLuaKey = "ui_style_state";
  static constexpr const char* kUsertypeName = "ui_style_state_component";

  static UIStylePatch ParsePatch(const sol::object& obj) {
    UIStylePatch patch;
    if (!obj.is<sol::table>()) return patch;
    const auto t = obj.as<sol::table>();
    using namespace LuaComponentHelpers;
    if (t["bg_color"].valid()) patch.SetBg(SafeGetColor(t, "bg_color"));
    if (t["border_color"].valid()) patch.SetBorder(SafeGetColor(t, "border_color"));
    if (t["text_color"].valid()) patch.SetText(SafeGetColor(t, "text_color"));
    if (t["opacity"].valid()) {
      patch.SetOpacity(static_cast<std::uint8_t>(SafeGetOptionalValue<int>(t, "opacity", 255)));
    }
    return patch;
  }

  static UIStyleStateComponent fromLua(const sol::object& data) {
    UIStyleStateComponent st;
    if (!data.is<sol::table>()) return st;
    const auto t = data.as<sol::table>();
    st.hover = ParsePatch(t["hover"]);
    st.press = ParsePatch(t["press"]);
    st.focus = ParsePatch(t["focus"]);
    st.checked = ParsePatch(t["checked"]);
    st.disabled = ParsePatch(t["disabled"]);
    return st;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<UIStylePatch>("ui_style_patch", "bg_color", &UIStylePatch::bgColor, "border_color",
                                   &UIStylePatch::borderColor, "text_color", &UIStylePatch::textColor, "opacity",
                                   &UIStylePatch::opacity, "flags", &UIStylePatch::flags);
    lua.new_usertype<UIStyleStateComponent>(kUsertypeName, "hover", &UIStyleStateComponent::hover, "press",
                                            &UIStyleStateComponent::press, "focus", &UIStyleStateComponent::focus,
                                            "checked", &UIStyleStateComponent::checked, "disabled",
                                            &UIStyleStateComponent::disabled);
  }
};
