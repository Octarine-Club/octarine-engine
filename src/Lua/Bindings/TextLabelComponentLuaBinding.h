#pragma once

#include <algorithm>
#include <sol/sol.hpp>
#include <string>

#include "Components/TextLabelComponent.h"
#include "Lua/Bindings/LuaBinding.h"
#include "UI/OssParser.h"

template <>
struct LuaBinding<TextLabelComponent> {
  static constexpr const char* kLuaKey = "text_label";
  static constexpr const char* kUsertypeName = "text_label_component";

  // "left"/"top" → Start, "center" → Center, "right"/"bottom" → End. Unknown values fall back to
  // Start (the historical top-left placement).
  static TextAlign ParseAlign(const std::string& value) {
    if (value == "center") return TextAlign::Center;
    if (value == "right" || value == "bottom" || value == "end") return TextAlign::End;
    return TextAlign::Start;
  }

  static void ApplyOutlineWidthAfterColor(TextLabelComponent& lbl, const sol::optional<float>& maybeWidth) {
    if (maybeWidth.has_value()) {
      lbl.outlineWidth = std::max(0.0f, *maybeWidth);
    } else if (lbl.outlineColor.a == 0) {
      lbl.outlineWidth = 0.0f;
    } else if (lbl.outlineWidth <= 0.0f) {
      lbl.outlineWidth = 1.0f;
    }
  }

  static void ApplyOutline(TextLabelComponent& lbl, const sol::object& colorOrWidth,
                           const sol::optional<float>& maybeWidth) {
    if (colorOrWidth.is<bool>()) {
      lbl.outlineWidth = colorOrWidth.as<bool>() ? std::max(0.0f, maybeWidth.value_or(1.0f)) : 0.0f;
      return;
    }
    if (colorOrWidth.is<float>()) {
      lbl.outlineWidth = std::max(0.0f, colorOrWidth.as<float>());
      return;
    }
    if (colorOrWidth.is<std::string>()) {
      octarine::Color oc{0, 0, 0, 255};
      if (OssParser::ParseColor(colorOrWidth.as<std::string>(), oc)) {
        lbl.outlineColor = oc;
        ApplyOutlineWidthAfterColor(lbl, maybeWidth);
      }
      return;
    }
    if (colorOrWidth.is<sol::table>()) {
      const auto ct = colorOrWidth.as<sol::table>();
      lbl.outlineColor = octarine::Color{
          static_cast<uint8_t>(ct.get_or("r", ct.get_or(1, 0))),
          static_cast<uint8_t>(ct.get_or("g", ct.get_or(2, 0))),
          static_cast<uint8_t>(ct.get_or("b", ct.get_or(3, 0))),
          static_cast<uint8_t>(ct.get_or("a", ct.get_or(4, 255))),
      };
      ApplyOutlineWidthAfterColor(lbl, maybeWidth);
    }
  }

  static void ApplyLuaOutlineTable(const sol::table& t, TextLabelComponent& lbl) {
    using namespace LuaComponentHelpers;
    bool explicitOutlineToggle = false;
    bool outlineEnabled = false;
    if (t["outline"].is<bool>()) {
      explicitOutlineToggle = true;
      outlineEnabled = t["outline"].get<bool>();
      lbl.outlineWidth = outlineEnabled ? 1.0f : 0.0f;
    } else if (t["outline"].is<float>()) {
      lbl.outlineWidth = std::max(0.0f, t["outline"].get<float>());
    } else if (t["outline"].is<sol::table>()) {
      lbl.outlineColor = SafeGetColor(t, "outline", 0, 0, 0, 255);
      if (lbl.outlineColor.a > 0) lbl.outlineWidth = 1.0f;
    }
    for (const char* widthKey : {"outline_width", "outline_thickness"}) {
      if (t[widthKey].is<float>()) {
        lbl.outlineWidth = std::max(0.0f, t[widthKey].get<float>());
        break;
      }
    }
    if (t["outline_color"].is<sol::table>()) {
      lbl.outlineColor = SafeGetColor(t, "outline_color", 0, 0, 0, 255);
      if (lbl.outlineWidth <= 0.0f && (!explicitOutlineToggle || outlineEnabled) && lbl.outlineColor.a > 0) {
        lbl.outlineWidth = 1.0f;
      }
    }
  }

  static TextLabelComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    using namespace LuaComponentHelpers;
    const glm::vec2 offsetPosition = SafeGetVec2(t, "position");
    const int layer = SafeGetOptionalValue<int>(t, "layer", 1);
    const std::string text = t["text"].get<std::string>();
    const std::string fontId = t["font_id"].get<std::string>();
    const octarine::Color color = SafeGetColor(t, "color");
    const bool isFixed = SafeGetOptionalValue<bool>(t, "is_fixed", true);
    const TextAlign hAlign = ParseAlign(SafeGetOptionalValue<std::string>(t, "align", "left"));
    const TextAlign vAlign = ParseAlign(SafeGetOptionalValue<std::string>(t, "valign", "top"));
    TextLabelComponent lbl(offsetPosition, layer, text, fontId, color, isFixed, hAlign, vAlign);
    lbl.wrapWidth = SafeGetOptionalValue<int>(t, "wrap_width", 0);
    lbl.wrapToRect = SafeGetOptionalValue<bool>(t, "wrap", false);
    if (t["shadow"].is<bool>() && t["shadow"].get<bool>()) {
      lbl.shadowOffset = glm::vec2(1.0f, 1.0f);
    }
    if (t["shadow_offset"].is<sol::table>()) {
      lbl.shadowOffset = SafeGetVec2(t, "shadow_offset", 1.0f, 1.0f);
    }
    if (t["shadow_color"].is<sol::table>()) {
      lbl.shadowColor = SafeGetColor(t, "shadow_color", 0, 0, 0, 200);
    }
    ApplyLuaOutlineTable(t, lbl);
    return lbl;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<TextLabelComponent>(
        kUsertypeName, sol::no_constructor, "text", &TextLabelComponent::text, "color", &TextLabelComponent::color,
        "layer", &TextLabelComponent::layer, "position", &TextLabelComponent::position, "is_fixed",
        &TextLabelComponent::isFixed, "h_align", &TextLabelComponent::hAlign, "v_align", &TextLabelComponent::vAlign,
        "wrap_width", &TextLabelComponent::wrapWidth, "wrap", &TextLabelComponent::wrapToRect, "shadow_offset",
        &TextLabelComponent::shadowOffset, "shadow_color", &TextLabelComponent::shadowColor, "outline_width",
        &TextLabelComponent::outlineWidth, "outline_thickness", &TextLabelComponent::outlineWidth, "outline_color",
        &TextLabelComponent::outlineColor, "set_outline",
        [](TextLabelComponent& self, const sol::object& colorOrWidth, const sol::optional<float>& maybeWidth) {
          ApplyOutline(self, colorOrWidth, maybeWidth);
        });
  }
};
