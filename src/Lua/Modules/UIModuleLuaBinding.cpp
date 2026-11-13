#include "Lua/Modules/UIModuleLuaBinding.h"

#include <string>

#include "Components/UIAnchorComponent.h"
#include "Components/UICanvasComponent.h"
#include "Components/UIClipComponent.h"
#include "Components/UIFlexComponent.h"
#include "Components/UIFocusComponent.h"
#include "Components/UILayoutBoxComponent.h"
#include "Components/UINineSliceComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/UIScrollComponent.h"
#include "Components/UIStyleStateComponent.h"
#include "Components/UIZIndexComponent.h"
#include "ECS/Registry.h"
#include "General/Logger.h"
#include "Lua/Bindings/UIClipComponentLuaBinding.h"
#include "Lua/Bindings/UIFlexComponentLuaBinding.h"
#include "Lua/Bindings/UIFocusComponentLuaBinding.h"
#include "Lua/Bindings/UILayoutBoxComponentLuaBinding.h"
#include "Lua/Bindings/UINineSliceComponentLuaBinding.h"
#include "Lua/Bindings/UIScrollComponentLuaBinding.h"
#include "Lua/Bindings/UIStyleStateComponentLuaBinding.h"
#include "Lua/LuaBindingContext.h"
#include "Systems/UIButtonSystem.h"
#include "UI/UIRuntime.h"

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
  if (r == nullptr || !r->IsAlive(entity)) return;
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
  if (r == nullptr || !r->IsAlive(entity)) return;
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
  if (r == nullptr || !r->IsAlive(entity)) return;
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

void ApplyCanvas(Registry* r, const Entity entity, const sol::optional<sol::table>& opts) {
  if (r == nullptr || !r->IsAlive(entity)) return;
  UICanvasComponent canvas;
  if (opts) {
    canvas.isFixed = (*opts)["fixed"].get_or(true);
    canvas.width = static_cast<float>((*opts)["width"].get_or(0.0));
    canvas.height = static_cast<float>((*opts)["height"].get_or(0.0));
    canvas.baseLayer = (*opts)["base_layer"].get_or(0);
  }
  if (r->HasComponent<UICanvasComponent>(entity)) {
    r->GetComponent<UICanvasComponent>(entity) = canvas;
  } else {
    r->AddComponent(entity, canvas);
  }
  if (!r->HasComponent<UIRectComponent>(entity)) {
    r->AddComponent(entity, UIRectComponent{});
  }
}

void ApplyZIndex(Registry* r, const Entity entity, const int z) {
  if (r == nullptr || !r->IsAlive(entity)) return;
  if (r->HasComponent<UIZIndexComponent>(entity)) {
    r->GetComponent<UIZIndexComponent>(entity).z = z;
  } else {
    r->AddComponent(entity, UIZIndexComponent{z});
  }
}

void ApplyBox(Registry* r, const Entity entity, const sol::optional<sol::table>& opts) {
  if (r == nullptr || !r->IsAlive(entity)) return;
  const UILayoutBoxComponent box = opts ? LuaBinding<UILayoutBoxComponent>::fromLua(*opts) : UILayoutBoxComponent{};
  if (r->HasComponent<UILayoutBoxComponent>(entity)) {
    r->GetComponent<UILayoutBoxComponent>(entity) = box;
  } else {
    r->AddComponent(entity, box);
  }
  if (!r->HasComponent<UIRectComponent>(entity)) {
    r->AddComponent(entity, UIRectComponent{});
  }
}

void ApplyClip(Registry* r, const Entity entity, const sol::optional<sol::object>& opts) {
  if (r == nullptr || !r->IsAlive(entity)) return;
  const UIClipComponent clip = opts ? LuaBinding<UIClipComponent>::fromLua(*opts) : UIClipComponent{};
  if (r->HasComponent<UIClipComponent>(entity)) {
    r->GetComponent<UIClipComponent>(entity) = clip;
  } else {
    r->AddComponent(entity, clip);
  }
  if (!r->HasComponent<UIRectComponent>(entity)) {
    r->AddComponent(entity, UIRectComponent{});
  }
}

void ApplyNineSlice(Registry* r, const Entity entity, const sol::object& opts) {
  if (r == nullptr || !r->IsAlive(entity)) return;
  const UINineSliceComponent nine = LuaBinding<UINineSliceComponent>::fromLua(opts);
  if (r->HasComponent<UINineSliceComponent>(entity)) {
    r->GetComponent<UINineSliceComponent>(entity) = nine;
  } else {
    r->AddComponent(entity, nine);
  }
}

void ApplyScroll(Registry* r, const Entity entity, const sol::optional<sol::table>& opts) {
  if (r == nullptr || !r->IsAlive(entity)) return;
  const UIScrollComponent scroll = opts ? LuaBinding<UIScrollComponent>::fromLua(*opts) : UIScrollComponent{};
  if (r->HasComponent<UIScrollComponent>(entity)) {
    r->GetComponent<UIScrollComponent>(entity) = scroll;
  } else {
    r->AddComponent(entity, scroll);
  }
  if (!r->HasComponent<UIClipComponent>(entity)) {
    r->AddComponent(entity, UIClipComponent{});
  }
  if (!r->HasComponent<UIRectComponent>(entity)) {
    r->AddComponent(entity, UIRectComponent{});
  }
}

void SetFocusFallback(Registry* r, const Entity target) {
  auto q = r->CreateQuery<UIFocusComponent>();
  q->Update();
  q->ForEach([target](const Entity e, UIFocusComponent& foc) {
    foc.isFocused = (e == target && foc.isFocusable && !foc.isDisabled && !foc.isInert);
  });
}

sol::optional<Entity> GetFocusedFallback(Registry* r) {
  auto q = r->CreateQuery<UIFocusComponent>();
  q->Update();
  Entity found{static_cast<EcsId>(-1)};
  q->ForEach([r, &found](const Entity e, const UIFocusComponent& foc) {
    const bool visible = !r->HasComponent<UIRectComponent>(e) || r->GetComponent<UIRectComponent>(e).visible;
    if (foc.isFocused && foc.isFocusable && !foc.isDisabled && !foc.isInert && visible) {
      found = e;
    }
  });
  if (found.GetId() != static_cast<std::uint32_t>(-1)) return found;
  return sol::nullopt;
}

void ApplyFocus(Registry* r, const sol::optional<Entity> maybeEntity) {
  if (r == nullptr) return;
  if (!maybeEntity.has_value()) {
    const Entity invalid{static_cast<EcsId>(-1)};
    if (auto* btnSys = r->TryGet<UIButtonSystem>()) {
      btnSys->SetFocus(invalid);
    } else {
      SetFocusFallback(r, invalid);
    }
    return;
  }
  const Entity entity = *maybeEntity;
  if (!r->IsAlive(entity)) return;
  if (!r->HasComponent<UIFocusComponent>(entity)) {
    r->AddComponent(entity, UIFocusComponent{});
  }
  if (auto* btnSys = r->TryGet<UIButtonSystem>()) {
    btnSys->SetFocus(entity);
  } else {
    SetFocusFallback(r, entity);
  }
}

sol::optional<Entity> GetFocusedEntity(Registry* r) {
  if (r == nullptr) return sol::nullopt;
  if (auto* btnSys = r->TryGet<UIButtonSystem>()) {
    const Entity focused = btnSys->GetFocusedEntity();
    if (focused.GetId() != static_cast<std::uint32_t>(-1)) return focused;
    return sol::nullopt;
  }
  return GetFocusedFallback(r);
}

bool ApplyDebugDraw(Registry* r, const sol::optional<bool>& enabled) {
  if (!r) return false;
  auto* cfg = r->TryGet<UIDebugConfig>();
  if (!cfg) {
    cfg = &r->Set<UIDebugConfig>(UIDebugConfig{});
  }
  cfg->enabled = enabled.has_value() ? *enabled : !cfg->enabled;
  return cfg->enabled;
}

}  // namespace

void LuaModuleBinding<UIModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  sol::table ui = lua.create_table();

  ui.set_function("canvas", [&ctx](const Entity entity, const sol::optional<sol::table>& opts) {
    ApplyCanvas(ctx.GetRegistry(), entity, opts);
  });
  ui.set_function("anchor",
                  [&ctx](const Entity entity, const sol::object& preset_or_table, sol::optional<sol::table> opts) {
                    ApplyAnchor(ctx.GetRegistry(), entity, preset_or_table, opts);
                  });
  ui.set_function("z_index", [&ctx](const Entity entity, const int z) { ApplyZIndex(ctx.GetRegistry(), entity, z); });
  ui.set_function("flex", [&ctx](const Entity entity, const sol::optional<sol::table>& opts) {
    ApplyFlex(ctx.GetRegistry(), entity, opts);
  });
  ui.set_function("box", [&ctx](const Entity entity, const sol::optional<sol::table>& opts) {
    ApplyBox(ctx.GetRegistry(), entity, opts);
  });
  ui.set_function("clip", [&ctx](const Entity entity, const sol::optional<sol::object>& opts) {
    ApplyClip(ctx.GetRegistry(), entity, opts);
  });
  ui.set_function("nine_slice", [&ctx](const Entity entity, const sol::object& opts) {
    ApplyNineSlice(ctx.GetRegistry(), entity, opts);
  });
  ui.set_function("scroll", [&ctx](const Entity entity, const sol::optional<sol::table>& opts) {
    ApplyScroll(ctx.GetRegistry(), entity, opts);
  });
  ui.set_function("focus", [&ctx](const sol::optional<Entity> entity) { ApplyFocus(ctx.GetRegistry(), entity); });
  ui.set_function("get_focused", [&ctx]() { return GetFocusedEntity(ctx.GetRegistry()); });
  ui.set_function("debug_draw",
                  [&ctx](const sol::optional<bool>& enabled) { return ApplyDebugDraw(ctx.GetRegistry(), enabled); });

  if (Registry* r = ctx.GetRegistry(); r != nullptr) {
    auto& runtime = r->Set<UIRuntime>(UIRuntime(&lua, r, nullptr));
    runtime.InstallLuaFunctions(ui);
  }

  lua["ui"] = ui;
}
