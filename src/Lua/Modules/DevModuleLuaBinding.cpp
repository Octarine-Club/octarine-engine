#include "Lua/Modules/DevModuleLuaBinding.h"

#include <sol/sol.hpp>
#include <sstream>
#include <string>

#include "Debug/DebugMenuRegistry.h"
#include "ECS/Registry.h"
#include "General/Logger.h"
#include "Lua/LuaBindingContext.h"
#ifndef OCTARINE_SHIPPED
#include "UI/UIPreviewer.h"
#include "UI/UIRuntime.h"
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
  if (!previewer->HasCaptureCallback()) {
    previewer->SetCaptureCallback([&ctx](const std::string& path) { return ctx.CaptureFrameToFile(path); });
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

void AppendContractParamsToLua(sol::state& lua, sol::table& out, const std::vector<MockParamEntry>& params) {
  sol::table tbl = lua.create_table();
  for (std::size_t i = 0; i < params.size(); ++i) {
    sol::table row = lua.create_table();
    row.set("name", params[i].name);
    row.set("default", params[i].defaultValue);
    row.set("current", params[i].currentValue);
    row.set("overridden", params[i].isOverridden);
    tbl.set(i + 1, row);
  }
  out.set("params", tbl);
}

void AppendContractVarsToLua(sol::state& lua, sol::table& out, const OxmlContract& contract,
                             const std::vector<MockFieldEntry>& fields, const bool staticUnmounted = false) {
  sol::table tbl = lua.create_table();
  for (std::size_t i = 0; i < contract.fields.size(); ++i) {
    const auto& f = contract.fields[i];
    const bool isUnset = staticUnmounted ? true : UIPreviewer::IsContractFieldUnset(f, fields);
    sol::table row = lua.create_table();
    row.set("path", f.path);
    row.set("type", ContractFieldTypeName(f.inferredType));
    row.set("conditional_only", f.conditionalOnly);
    row.set("two_way", f.isTwoWay);
    row.set("unset", isUnset);
    tbl.set(i + 1, row);
  }
  out.set("vars", tbl);
}

void AppendContractCallbacksToLua(sol::state& lua, sol::table& out, const std::vector<MockCallbackEntry>& cbs) {
  sol::table tbl = lua.create_table();
  for (std::size_t i = 0; i < cbs.size(); ++i) {
    sol::table row = lua.create_table();
    row.set("name", cbs[i].name);
    row.set("attr", cbs[i].sourceAttr);
    row.set("raw", cbs[i].rawExpr);
    row.set("bound", cbs[i].hasControllerHandler);
    tbl.set(i + 1, row);
  }
  out.set("callbacks", tbl);
}

sol::table InspectStaticOxmlContractToLua(sol::state& lua, UIRuntime* uiRuntime, const std::string& pathOrId) {
  sol::table out = lua.create_table();
  if (uiRuntime == nullptr) return out;
  const auto text = (!pathOrId.empty() && pathOrId.front() == '<') ? std::optional<std::string>(pathOrId)
                                                                   : uiRuntime->ReadTextAsset(pathOrId);
  if (!text.has_value()) return out;
  const OxmlParseResult parsed = OxmlParser::Parse(*text);
  if (!parsed.ok) return out;

  auto resolveUse = [uiRuntime](const std::string& src) -> std::optional<OxmlDocument> {
    const auto uTxt =
        (!src.empty() && src.front() == '<') ? std::optional<std::string>(src) : uiRuntime->ReadTextAsset(src);
    if (!uTxt.has_value()) return std::nullopt;
    const OxmlParseResult uParsed = OxmlParser::Parse(*uTxt);
    return uParsed.ok ? std::optional<OxmlDocument>(uParsed.document) : std::nullopt;
  };
  const OxmlContract contract = InspectOxmlContract(parsed.document, {}, resolveUse);
  std::vector<MockParamEntry> params;
  params.reserve(contract.params.size());
  for (const auto& p : contract.params) {
    params.push_back({p.name, p.defaultValue, p.defaultValue, false});
  }
  std::vector<MockCallbackEntry> cbs;
  cbs.reserve(contract.callbacks.size());
  for (const auto& cb : contract.callbacks) {
    cbs.push_back({cb.name, cb.sourceAttr, cb.rawExpr, false});
  }
  out.set("active", false);
  out.set("source", pathOrId);
  AppendContractParamsToLua(lua, out, params);
  AppendContractVarsToLua(lua, out, contract, {}, /*staticUnmounted=*/true);
  AppendContractCallbacksToLua(lua, out, cbs);
  out.set("refs", contract.refs);
  out.set("slots", contract.slots);
  return out;
}

sol::table InspectActiveContractToLua(sol::state& lua, LuaBindingContext& ctx) {
  sol::table out = lua.create_table();
  auto* previewer = GetOrCreateUIPreviewer(ctx);
  if (previewer == nullptr) return out;
  out.set("active", previewer->IsActive());
  out.set("mock_path", previewer->ActiveMockPath());
  out.set("mock_modified", previewer->IsMockModified());
  out.set("last_mock_error", previewer->LastMockError());
  AppendContractParamsToLua(lua, out, previewer->CollectMockParams());
  AppendContractVarsToLua(lua, out, previewer->ActiveContract(), previewer->CollectMockFields());
  AppendContractCallbacksToLua(lua, out, previewer->CollectMockCallbacks());
  out.set("refs", previewer->ActiveContract().refs);
  out.set("slots", previewer->ActiveContract().slots);
  return out;
}

bool SetUiMockParamFromLua(LuaBindingContext& ctx, const std::string& name, const sol::object& value) {
  auto* previewer = GetOrCreateUIPreviewer(ctx);
  if (previewer == nullptr) return false;
  std::string strVal;
  if (value.is<std::string>()) {
    strVal = value.as<std::string>();
  } else if (value.is<bool>()) {
    strVal = value.as<bool>() ? "true" : "false";
  } else if (value.is<double>()) {
    std::ostringstream oss;
    oss << value.as<double>();
    strVal = oss.str();
  } else {
    return false;
  }
  return previewer->SetMockParam(name, strVal);
}

void BindUIPreviewContractFunctions(sol::table& dev, sol::state& lua, LuaBindingContext& ctx) {
  dev.set_function("inspect_ui_contract", [&lua, &ctx](const sol::optional<std::string>& pathOrId) -> sol::table {
    if (pathOrId.has_value() && !pathOrId->empty()) {
      auto* reg = ctx.GetRegistry();
      auto* uiRuntime = reg != nullptr ? reg->TryGet<UIRuntime>() : nullptr;
      return InspectStaticOxmlContractToLua(lua, uiRuntime, *pathOrId);
    }
    return InspectActiveContractToLua(lua, ctx);
  });

  dev.set_function("inspect_ui_contract_json", [&ctx](const sol::optional<std::string>& pathOrId) -> std::string {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr ? previewer->InspectContractJson(pathOrId.value_or("")) : "{}";
  });
}

void BindUIPreviewMockFunctions(sol::table& dev, sol::state& lua, LuaBindingContext& ctx) {
  BindUIPreviewContractFunctions(dev, lua, ctx);

  dev.set_function("set_ui_mock_field", [&ctx](const std::string& path, const sol::object& value) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr && previewer->SetMockFieldObject(path, value);
  });

  dev.set_function("resize_ui_mock_array", [&ctx](const std::string& path, const int newLen) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr && previewer->ResizeMockArray(path, newLen);
  });

  dev.set_function("set_ui_param", [&ctx](const std::string& name, const sol::object& value) -> bool {
    return SetUiMockParamFromLua(ctx, name, value);
  });

  dev.set_function("load_ui_mock", [&ctx](const std::string& path) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr && previewer->LoadMockScript(path);
  });

  dev.set_function(
      "save_ui_mock", [&ctx](const sol::optional<std::string>& path, const sol::optional<bool>& force) -> bool {
        auto* previewer = GetOrCreateUIPreviewer(ctx);
        return previewer != nullptr && previewer->SaveMockSidecar(path.value_or(""), force.value_or(false));
      });

  dev.set_function("export_ui_mock_lua", [&ctx]() -> std::string {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr ? previewer->ExportMockLuaScript() : "";
  });

  dev.set_function("seed_ui_mock", [&ctx](const sol::optional<std::string>& path) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    if (previewer == nullptr) return false;
    return path.has_value() ? previewer->SeedSingleContractField(*path) : previewer->SeedMissingContractFields();
  });
}

void BindUIPreviewViewportFunctions(sol::table& dev, LuaBindingContext& ctx) {
  dev.set_function("set_ui_viewport", [&ctx](const int width, const int height) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    if (previewer == nullptr) return false;
    previewer->SetSimulatedViewport(width, height);
    return true;
  });

  dev.set_function("get_ui_viewport_json", [&ctx]() -> std::string {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr ? previewer->ViewportInfoJson() : "{}";
  });

  dev.set_function("set_ui_safe_area_guides", [&ctx](const bool enabled) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    if (previewer == nullptr) return false;
    previewer->SetShowSafeAreaGuides(enabled);
    return true;
  });

  dev.set_function("set_ui_integer_zoom", [&ctx](const bool enabled) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    if (previewer == nullptr) return false;
    previewer->SetIntegerZoomSimulation(enabled);
    return true;
  });
}

void BindUIPreviewPresetFunctions(sol::table& dev, LuaBindingContext& ctx) {
  dev.set_function("list_ui_presets_json", [&ctx]() -> std::string {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr ? previewer->ListPresetsJson() : "{}";
  });

  dev.set_function("select_ui_preset", [&ctx](const std::string& name) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr && previewer->SelectMockPreset(name);
  });

  dev.set_function(
      "save_ui_preset",
      [&ctx](const std::string& name, const sol::optional<bool>& setDefault, const sol::optional<bool>& force) -> bool {
        auto* previewer = GetOrCreateUIPreviewer(ctx);
        return previewer != nullptr &&
               previewer->SaveMockPreset(name, setDefault.value_or(false), force.value_or(false));
      });

  dev.set_function("delete_ui_preset", [&ctx](const std::string& name, const sol::optional<bool>& force) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr && previewer->DeleteMockPreset(name, force.value_or(false));
  });

  dev.set_function("apply_ui_stress_preset", [&ctx](const std::string& kind) -> bool {
    auto* previewer = GetOrCreateUIPreviewer(ctx);
    return previewer != nullptr && previewer->ApplyStressPreset(kind);
  });

  dev.set_function("run_ui_matrix_json",
                   [&ctx](const std::string& outDir, const sol::optional<std::string>& resolutionsCsv,
                          const sol::optional<std::string>& presetsCsv) -> std::string {
                     auto* previewer = GetOrCreateUIPreviewer(ctx);
                     if (previewer == nullptr) return "{}";
                     return previewer->RunPreviewMatrixJson(outDir, resolutionsCsv.value_or(""),
                                                            presetsCsv.value_or(""));
                   });
}

void BindUIPreviewFunctions(sol::table& dev, sol::state& lua, LuaBindingContext& ctx) {
  BindUIPreviewMountFunctions(dev, ctx);
  BindUIPreviewAuditFunctions(dev, lua, ctx);
  BindUIPreviewMockFunctions(dev, lua, ctx);
  BindUIPreviewViewportFunctions(dev, ctx);
  BindUIPreviewPresetFunctions(dev, ctx);
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
