#include "Lua/Modules/DevModuleLuaBinding.h"

#include <sol/sol.hpp>
#include <string>

#include "Debug/DebugMenuRegistry.h"
#include "ECS/Registry.h"
#include "General/Logger.h"
#include "Lua/LuaBindingContext.h"
#ifndef OCTARINE_SHIPPED
#include "UI/UIPreviewer.h"
#endif

namespace {

using octarine::debug::DebugMenuRegistry;
using octarine::debug::DebugPanelDescriptor;

DebugMenuRegistry* GetDebugRegistry(LuaBindingContext& ctx) {
  auto* reg = ctx.GetRegistry();
  if (reg == nullptr) return nullptr;
  if (reg->TryGet<DebugMenuRegistry>() == nullptr) {
    reg->Set<DebugMenuRegistry>(DebugMenuRegistry());
  }
  return reg->TryGet<DebugMenuRegistry>();
}

void BindPanelFunctions(sol::table& dev, LuaBindingContext& ctx) {
  dev.set_function("register_panel", [&ctx](const std::string& id, const sol::object& optsOrFn) {
    auto* debugReg = GetDebugRegistry(ctx);
    if (!debugReg) return;

    DebugPanelDescriptor desc;
    desc.id = id;
    desc.category = "Game";
    desc.title = id;
    desc.isLuaOwned = true;
    desc.autoWindow = true;

    sol::protected_function drawFn;
    if (optsOrFn.is<sol::protected_function>()) {
      drawFn = optsOrFn.as<sol::protected_function>();
    } else if (optsOrFn.is<sol::table>()) {
      sol::table opts = optsOrFn.as<sol::table>();
      desc.title = opts.get_or("title", id);
      desc.category = opts.get_or("category", std::string("Game"));
      desc.shortcut = opts.get_or("shortcut", std::string{});
      desc.autoWindow = opts.get_or("auto_window", true);
      drawFn = opts.get<sol::protected_function>("draw");
    }

    if (drawFn.valid()) {
      desc.drawCallback = [drawFn](Registry&) {
        auto res = drawFn();
        if (!res.valid()) {
          const sol::error err = res;
          Logger::ErrorLua("dev.register_panel error in '" + std::string(err.what()) + "'");
        }
      };
    }

    debugReg->RegisterPanel(std::move(desc));
  });

  dev.set_function("unregister_panel", [&ctx](const std::string& id) -> bool {
    auto* debugReg = GetDebugRegistry(ctx);
    return debugReg != nullptr && debugReg->UnregisterPanel(id);
  });

  dev.set_function("is_panel_open", [&ctx](const std::string& id) -> bool {
    auto* debugReg = GetDebugRegistry(ctx);
    return debugReg != nullptr && debugReg->IsPanelOpen(id);
  });

  dev.set_function("set_panel_open", [&ctx](const std::string& id, bool open) {
    if (auto* debugReg = GetDebugRegistry(ctx)) {
      debugReg->SetPanelOpen(id, open);
    }
  });
}

void BindActionAndToggleFunctions(sol::table& dev, LuaBindingContext& ctx) {
  dev.set_function("register_action", [&ctx](const std::string& menuPath, const sol::protected_function& actionFn,
                                             const sol::optional<std::string>& shortcut) {
    auto* debugReg = GetDebugRegistry(ctx);
    if (!debugReg || !actionFn.valid()) return;

    debugReg->RegisterAction(
        menuPath,
        [actionFn](Registry&) {
          auto res = actionFn();
          if (!res.valid()) {
            const auto err = res.get<sol::error>();
            Logger::ErrorLua("dev.register_action error: " + std::string(err.what()));
          }
        },
        shortcut.value_or(""), /*isLuaOwned=*/true);
  });

  dev.set_function("register_toggle", [&ctx](const std::string& menuPath, const sol::protected_function& getter,
                                             const sol::protected_function& setter) {
    auto* debugReg = GetDebugRegistry(ctx);
    if (!debugReg || !getter.valid() || !setter.valid()) return;

    debugReg->RegisterToggle(
        menuPath,
        [getter]() -> bool {
          auto res = getter();
          return res.valid() && res.get_type() == sol::type::boolean && res.get<bool>();
        },
        [setter](bool enabled) {
          auto res = setter(enabled);
          if (!res.valid()) {
            const auto err = res.get<sol::error>();
            Logger::ErrorLua("dev.register_toggle error: " + std::string(err.what()));
          }
        },
        /*isLuaOwned=*/true);
  });
}

std::string FormatWatchValue(const sol::protected_function& valueFn) {
  auto res = valueFn();
  if (!res.valid()) return "<err>";
  if (res.return_count() == 0) return {};
  const sol::object obj = res.get<sol::object>();
  if (obj.is<std::string>()) return obj.as<std::string>();
  sol::state_view sv(obj.lua_state());
  auto tostring = sv["tostring"].get<sol::protected_function>();
  if (tostring.valid()) {
    auto strRes = tostring(obj);
    if (strRes.valid() && strRes.get_type() == sol::type::string) {
      return strRes.get<std::string>();
    }
  }
  return {};
}

void BindWatchFunctions(sol::table& dev, LuaBindingContext& ctx) {
  dev.set_function("watch", [&ctx](const std::string& label, const sol::protected_function& valueFn) {
    auto* debugReg = GetDebugRegistry(ctx);
    if (!debugReg || !valueFn.valid()) return;

    debugReg->RegisterWatch(
        label, [valueFn]() -> std::string { return FormatWatchValue(valueFn); },
        /*isLuaOwned=*/true);
  });

  dev.set_function("unwatch", [&ctx](const std::string& label) -> bool {
    auto* debugReg = GetDebugRegistry(ctx);
    return debugReg != nullptr && debugReg->UnregisterWatch(label);
  });
}

void BindEntitySelectionFunctions(sol::table& dev, LuaBindingContext& ctx) {
  dev.set_function("select_entity", [&ctx](sol::optional<Entity> entity) {
    if (auto* debugReg = GetDebugRegistry(ctx)) {
      debugReg->SelectEntity(entity.value_or(Entity{}));
    }
  });

  dev.set_function("get_selected_entity", [&ctx]() -> sol::optional<Entity> {
    auto* debugReg = GetDebugRegistry(ctx);
    if (debugReg == nullptr) return sol::nullopt;
    const Entity ent = debugReg->GetSelectedEntity();
    auto* reg = ctx.GetRegistry();
    if (reg != nullptr && reg->IsAlive(ent)) return ent;
    return sol::nullopt;
  });

  dev.set_function("clear", [&ctx]() {
    if (auto* debugReg = GetDebugRegistry(ctx)) {
      debugReg->ClearLuaOwned();
    }
  });
}

#ifndef OCTARINE_SHIPPED
UIPreviewer* GetOrCreateUIPreviewer(LuaBindingContext& ctx) {
  auto* reg = ctx.GetRegistry();
  if (reg == nullptr) return nullptr;
  auto* previewer = reg->TryGet<UIPreviewer>();
  if (previewer == nullptr) {
    previewer = &reg->Set<UIPreviewer>(UIPreviewer(reg));
  } else {
    previewer->SetRegistry(reg);
  }
  return previewer;
}

sol::table MakeAuditIssueTable(sol::state& lua, const UILayoutIssue& issue) {
  sol::table row = lua.create_table();
  row.set("code", issue.code);
  row.set("severity", issue.severity);
  row.set("entity", issue.entity);
  row.set("label", issue.label);
  row.set("message", issue.message);
  sol::table rect = lua.create_table();
  rect.set("left", issue.rect.left);
  rect.set("top", issue.rect.top);
  rect.set("right", issue.rect.right);
  rect.set("bottom", issue.rect.bottom);
  row.set("rect", rect);
  return row;
}

sol::table MakeCatalogViewTable(sol::state& lua, const PreviewCatalogEntry& view) {
  sol::table row = lua.create_table();
  row.set("id", view.id);
  row.set("path", view.displayPath);
  return row;
}

void BindUIPreviewMountFunctions(sol::table& dev, LuaBindingContext& ctx) {
  dev.set_function("preview_ui", [&ctx](const std::string& pathOrId, const sol::optional<sol::table>& opts) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr && previewer->MountPreview(pathOrId, opts);
  });

  dev.set_function("preview_inline_ui", [&ctx](const std::string& xml, const sol::optional<std::string>& oss) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr && previewer->MountInlineXml(xml, oss.value_or(""));
  });

  dev.set_function("unmount_preview_ui", [&ctx]() {
    if (auto* reg = ctx.GetRegistry()) {
      if (auto* previewer = reg->TryGet<UIPreviewer>()) {
        previewer->UnmountPreview();
      }
    }
  });

  dev.set_function("reload_preview_ui", [&ctx]() -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr && previewer->ReloadActivePreview();
  });
}

void BindUIPreviewAuditFunctions(sol::table& dev, sol::state& lua, LuaBindingContext& ctx) {
  dev.set_function("audit_ui", [&lua, &ctx]() -> sol::table {
    sol::table arr = lua.create_table();
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    if (previewer == nullptr) return arr;
    const auto issues = previewer->AuditLayout();
    for (std::size_t i = 0; i < issues.size(); ++i) {
      arr.set(i + 1, MakeAuditIssueTable(lua, issues[i]));
    }
    return arr;
  });

  dev.set_function("audit_ui_json", [&ctx]() -> std::string {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr ? previewer->AuditLayoutJson() : "{}";
  });

  dev.set_function("list_ui_views", [&lua, &ctx]() -> sol::table {
    sol::table arr = lua.create_table();
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    if (previewer == nullptr) return arr;
    previewer->RefreshCatalogViews();
    const auto& views = previewer->CatalogViews();
    for (std::size_t i = 0; i < views.size(); ++i) {
      arr.set(i + 1, MakeCatalogViewTable(lua, views[i]));
    }
    return arr;
  });
}

void BindUIPreviewFunctions(sol::table& dev, sol::state& lua, LuaBindingContext& ctx) {
  BindUIPreviewMountFunctions(dev, ctx);
  BindUIPreviewAuditFunctions(dev, lua, ctx);
}
#endif

}  // namespace

void LuaModuleBinding<DevModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  sol::table dev = lua.create_named_table("dev");
  BindPanelFunctions(dev, ctx);
  BindActionAndToggleFunctions(dev, ctx);
  BindWatchFunctions(dev, ctx);
  BindEntitySelectionFunctions(dev, ctx);
#ifndef OCTARINE_SHIPPED
  BindUIPreviewFunctions(dev, lua, ctx);
#endif
}
