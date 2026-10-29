#include "Lua/Modules/UIModuleLuaBinding.h"

#include <string>

#include "Components/UIAnchorComponent.h"
#include "Components/UICanvasComponent.h"
#include "Components/UIFlexComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/UIZIndexComponent.h"
#include "ECS/Registry.h"
#include "General/Logger.h"
#include "Lua/Bindings/UIFlexComponentLuaBinding.h"
#include "Lua/LuaBindingContext.h"

namespace {

constexpr float kHalf = 0.5f;
constexpr float kCenter = 0.5f;

UIAnchorComponent ResolvePreset(const std::string& name, const float w, const float h, const float margin) {
  if (name == "fill") return UIAnchorComponent{0.f, 0.f, 1.f, 1.f, margin, margin, -margin, -margin};
  if (name == "center")
    return UIAnchorComponent{kCenter, kCenter, kCenter, kCenter, -w * kHalf, -h * kHalf, w * kHalf, h * kHalf};
  if (name == "top_left") return UIAnchorComponent{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, w, h};
  if (name == "top_right") return UIAnchorComponent{1.f, 0.f, 1.f, 0.f, -w, 0.f, 0.f, h};
  if (name == "bottom_left") return UIAnchorComponent{0.f, 1.f, 0.f, 1.f, 0.f, -h, w, 0.f};
  if (name == "bottom_right") return UIAnchorComponent{1.f, 1.f, 1.f, 1.f, -w, -h, 0.f, 0.f};
  if (name == "top_center") return UIAnchorComponent{kCenter, 0.f, kCenter, 0.f, -w * kHalf, 0.f, w * kHalf, h};
  if (name == "bottom_center") return UIAnchorComponent{kCenter, 1.f, kCenter, 1.f, -w * kHalf, -h, w * kHalf, 0.f};
  if (name == "left") return UIAnchorComponent{0.f, 0.f, 0.f, 1.f, 0.f, 0.f, w, 0.f};
  if (name == "right") return UIAnchorComponent{1.f, 0.f, 1.f, 1.f, -w, 0.f, 0.f, 0.f};
  Logger::Warn("ui.anchor: unknown preset '" + name + "'; defaulting to fill.");
  return UIAnchorComponent{0.f, 0.f, 1.f, 1.f, margin, margin, -margin, -margin};
}

void AttachAnchor(Registry* r, const Entity entity, const UIAnchorComponent& anchor) {
  if (r->HasComponent<UIAnchorComponent>(entity)) {
    r->GetComponent<UIAnchorComponent>(entity) = anchor;
  } else {
    r->AddComponent(entity, anchor);
  }
  if (!r->HasComponent<UIRectComponent>(entity)) {
    r->AddComponent(entity, UIRectComponent{});
  }
}

void ApplyAnchor(Registry* r, const Entity entity, const sol::object& presetOrTable,
                 const sol::optional<sol::table>& opts) {
  if (presetOrTable.is<std::string>()) {
    float w = 0.f;
    float h = 0.f;
    float margin = 0.f;
    if (opts) {
      w = static_cast<float>((*opts)["width"].get_or(0.0));
      h = static_cast<float>((*opts)["height"].get_or(0.0));
      margin = static_cast<float>((*opts)["margin"].get_or(0.0));
    }
    AttachAnchor(r, entity, ResolvePreset(presetOrTable.as<std::string>(), w, h, margin));
    return;
  }
  if (presetOrTable.is<sol::table>()) {
    const auto t = presetOrTable.as<sol::table>();
    UIAnchorComponent anchor;
    anchor.anchorLeft = t["left"].get_or(0.f);
    anchor.anchorTop = t["top"].get_or(0.f);
    anchor.anchorRight = t["right"].get_or(1.f);
    anchor.anchorBottom = t["bottom"].get_or(1.f);
    anchor.offsetLeft = t["offset_left"].get_or(0.f);
    anchor.offsetTop = t["offset_top"].get_or(0.f);
    anchor.offsetRight = t["offset_right"].get_or(0.f);
    anchor.offsetBottom = t["offset_bottom"].get_or(0.f);
    AttachAnchor(r, entity, anchor);
    return;
  }
  Logger::Warn("ui.anchor: second argument must be a preset name (string) or an anchor table.");
}

void ApplyFlex(Registry* r, const Entity entity, const sol::optional<sol::table>& opts) {
  UIFlexComponent flex;
  if (opts) {
    flex = LuaBinding<UIFlexComponent>::fromLua(*opts);
  }
  if (r->HasComponent<UIFlexComponent>(entity)) {
    r->GetComponent<UIFlexComponent>(entity) = flex;
  } else {
    r->AddComponent(entity, flex);
  }
  if (!r->HasComponent<UIRectComponent>(entity)) {
    r->AddComponent(entity, UIRectComponent{});
  }
}

}  // namespace

void LuaModuleBinding<UIModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  sol::table ui = lua.create_table();

  ui.set_function("canvas", [&ctx](const Entity entity, sol::optional<sol::table> opts) {
    Registry* r = ctx.GetRegistry();
    UICanvasComponent canvas;
    if (opts) {
      canvas.isFixed = (*opts)["fixed"].get_or(true);
      canvas.width = static_cast<float>((*opts)["width"].get_or(0.0));
      canvas.height = static_cast<float>((*opts)["height"].get_or(0.0));
      canvas.baseLayer = (*opts)["base_layer"].get_or(0);
    }
    r->AddComponent(entity, canvas);
    if (!r->HasComponent<UIRectComponent>(entity)) {
      r->AddComponent(entity, UIRectComponent{});
    }
  });

  ui.set_function("anchor",
                  [&ctx](const Entity entity, const sol::object& preset_or_table, sol::optional<sol::table> opts) {
                    ApplyAnchor(ctx.GetRegistry(), entity, preset_or_table, opts);
                  });

  ui.set_function("z_index", [&ctx](const Entity entity, const int z) {
    Registry* r = ctx.GetRegistry();
    if (r->HasComponent<UIZIndexComponent>(entity)) {
      r->GetComponent<UIZIndexComponent>(entity).z = z;
    } else {
      r->AddComponent(entity, UIZIndexComponent{z});
    }
  });

  ui.set_function("flex", [&ctx](const Entity entity, const sol::optional<sol::table>& opts) {
    ApplyFlex(ctx.GetRegistry(), entity, opts);
  });

  lua["ui"] = ui;
}
