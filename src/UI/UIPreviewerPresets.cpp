#include "UI/UIPreviewer.h"

#ifndef OCTARINE_SHIPPED

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ECS/Registry.h"
#include "Game/EngineContext.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "Lua/LuaTableSerializer.h"
#include "Renderer/RenderQueue.h"
#include "Systems/UILayoutMetrics.h"
#include "UI/UIPreviewerInternal.h"
#include "UI/UIRuntime.h"

namespace {
namespace fs = std::filesystem;
namespace detail = octarine::ui_preview_detail;

std::string NormalizePresetLookupKey(const std::string& raw) {
  return (!raw.empty() && raw.front() == '@') ? raw.substr(1) : raw;
}

bool TryParseStressVariant(std::string_view kind, detail::MockSeedVariant& outVariant, std::string& outCanonicalTag) {
  const std::string_view stripped = (!kind.empty() && kind.front() == '@') ? kind.substr(1) : kind;
  if (stripped == "default") {
    outVariant = detail::MockSeedVariant::Compact;
    outCanonicalTag = "@default";
    return true;
  }
  if (stripped == "empty") {
    outVariant = detail::MockSeedVariant::Empty;
    outCanonicalTag = "@empty";
    return true;
  }
  if (stripped == "overflow") {
    outVariant = detail::MockSeedVariant::Overflow;
    outCanonicalTag = "@overflow";
    return true;
  }
  return false;
}

std::vector<std::string> ExtractSortedPresetKeys(const sol::table& presetsTbl) {
  std::vector<std::string> keys;
  bool hasDefault = false;
  for (const auto& [k, v] : presetsTbl) {
    if (!k.is<std::string>() || !v.is<sol::table>()) continue;
    const std::string name = k.as<std::string>();
    if (name == "default") {
      hasDefault = true;
    } else {
      keys.push_back(name);
    }
  }
  std::ranges::sort(keys);
  if (hasDefault) {
    keys.insert(keys.begin(), "default");
  }
  return keys;
}
}  // namespace

namespace octarine::ui_preview_detail {
sol::table BuildLiveDocSnapshot(sol::state& lua, const UIPreviewer& previewer, const sol::table& ctx) {
  sol::table docTbl = lua.create_table();
  if (const auto params = previewer.CollectMockParams(); !params.empty()) {
    sol::table paramsTbl = lua.create_table();
    for (const auto& p : params) {
      paramsTbl[p.name] = p.currentValue;
    }
    docTbl.set("params", paramsTbl);
  }
  const sol::object ctxClean = ctx.valid() ? UnwrapObservableDataDeep(lua, ctx) : sol::object(sol::lua_nil);
  docTbl.set("context", ctxClean.is<sol::table>() ? ctxClean.as<sol::table>() : lua.create_table());
  return docTbl;
}
}  // namespace octarine::ui_preview_detail

namespace {
std::string SanitizePresetFilenameSlug(const std::string& preset) {
  std::string_view sv = preset;
  if (!sv.empty() && sv.front() == '@') sv.remove_prefix(1);
  std::string out;
  out.reserve(sv.size());
  for (const char c : sv) {
    const auto uc = static_cast<unsigned char>(c);
    out.push_back((std::isalnum(uc) != 0 || c == '_' || c == '-') ? c : '_');
  }
  return out.empty() ? "default" : out;
}

std::vector<std::string> SplitCsvTrimmed(const std::string& csv) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= csv.size()) {
    const std::size_t comma = csv.find(',', start);
    const std::size_t len = (comma == std::string::npos) ? (csv.size() - start) : (comma - start);
    std::string token = csv.substr(start, len);
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())) != 0) token.erase(token.begin());
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())) != 0) token.pop_back();
    if (!token.empty()) out.push_back(std::move(token));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

bool TryParseSingleResolution(const std::string& token, int& outW, int& outH) {
  const std::size_t sep = token.find_first_of("xX");
  if (sep == std::string::npos || sep == 0 || sep + 1 >= token.size()) return false;
  auto parsePosInt = [](std::string_view sv, int& val) -> bool {
    int n = 0;
    for (const char c : sv) {
      if (std::isdigit(static_cast<unsigned char>(c)) == 0) return false;
      if (n > 100000) return false;
      n = n * 10 + (c - '0');
    }
    if (n <= 0 || n > 8192) return false;
    val = n;
    return true;
  };
  return parsePosInt(std::string_view(token).substr(0, sep), outW) &&
         parsePosInt(std::string_view(token).substr(sep + 1), outH);
}

std::vector<std::string> ResolveMatrixPresetList(const UIPreviewer& previewer,
                                                 const std::vector<std::string>& requested) {
  auto authored = previewer.ListMockPresets();
  const bool hasFlatCustomMock = authored.empty() && (previewer.GetMockSourceMode() == MockSourceMode::ExplicitLua ||
                                                      previewer.GetMockSourceMode() == MockSourceMode::SidecarPreview);

  if (requested.empty() || (requested.size() == 1 && requested[0] == "auto")) {
    if (!authored.empty()) return authored;
    if (hasFlatCustomMock) return {"default", "@empty", "@overflow"};
    return {"@default", "@empty", "@overflow"};
  }
  if (requested.size() == 1 && requested[0] == "all") {
    if (!authored.empty()) return authored;
    return {hasFlatCustomMock ? "default" : "@default"};
  }
  return requested;
}

struct LiveMatrixSnapshot {
  int simW{0};
  int simH{0};
  std::string preset{};
  MockSourceMode mode{MockSourceMode::None};
  std::string mockPath{};
  sol::table rawCtx{};
  sol::table rawMockRoot{};
  sol::table rawCtrl{};
  std::vector<MockParamEntry> params{};
  bool hadCtx{false};
};

LiveMatrixSnapshot CaptureLiveMatrixSnapshot(const UIPreviewer& previewer, const Registry* registry) {
  LiveMatrixSnapshot snap;
  snap.simW = previewer.SimulatedWidth();
  snap.simH = previewer.SimulatedHeight();
  snap.preset = previewer.ActiveMockPreset();
  snap.mode = previewer.GetMockSourceMode();
  snap.mockPath = previewer.ActiveMockPath();
  snap.params = previewer.CollectMockParams();

  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (detail::TryGetActiveViewContext(registry, luaPtr, viewTbl, ctx)) {
    const sol::object unwrapped = detail::UnwrapObservableDataDeep(*luaPtr, ctx);
    if (unwrapped.is<sol::table>()) {
      snap.rawCtx = unwrapped.as<sol::table>();
      snap.hadCtx = true;
    }
    const sol::object rawMockObj =
        detail::GetPinnedLuaRegistryObject(luaPtr->lua_state(), "__octarine_preview_raw_mock");
    if (rawMockObj.is<sol::table>()) {
      snap.rawMockRoot = rawMockObj.as<sol::table>();
    }
    const sol::object rawCtrlObj =
        detail::GetPinnedLuaRegistryObject(luaPtr->lua_state(), "__octarine_preview_controller");
    if (rawCtrlObj.is<sol::table>()) {
      snap.rawCtrl = rawCtrlObj.as<sol::table>();
    }
  }
  return snap;
}

sol::optional<sol::table> BuildInlineMockRestoreOpts(const Registry* registry, const LiveMatrixSnapshot& snap) {
  auto* uiRuntime = registry != nullptr ? registry->TryGet<UIRuntime>() : nullptr;
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return sol::nullopt;
  sol::state& lua = *uiRuntime->GetLua();
  sol::table opts = lua.create_table();
  opts.set("context", snap.hadCtx ? snap.rawCtx : lua.create_table());
  if (snap.rawCtrl.valid()) opts.set("controller", snap.rawCtrl);
  if (!snap.params.empty()) {
    sol::table pTbl = lua.create_table();
    for (const auto& p : snap.params) pTbl[p.name] = p.currentValue;
    opts.set("params", pTbl);
  }
  return opts;
}

void ApplySnapshotContextAndParams(UIPreviewer& previewer, Registry* registry, const LiveMatrixSnapshot& snap) {
  for (const auto& p : snap.params) {
    if (p.isOverridden) static_cast<void>(previewer.SetMockParam(p.name, p.currentValue));
  }
  if (!snap.hadCtx) return;
  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (detail::TryGetActiveViewContext(registry, luaPtr, viewTbl, ctx)) {
    if (auto* uiRuntime = registry != nullptr ? registry->TryGet<UIRuntime>() : nullptr) {
      sol::table obs = uiRuntime->CreateObservable(snap.rawCtx);
      const sol::protected_function setCtx = viewTbl.get<sol::protected_function>("set_context");
      if (setCtx.valid()) setCtx(viewTbl, obs);
    }
  }
}

void RestoreMatrixMockBaseline(UIPreviewer& previewer, const Registry* registry, const LiveMatrixSnapshot& snap) {
  if (!snap.preset.empty() && !snap.preset.starts_with('@')) {
    static_cast<void>(previewer.SelectMockPreset(snap.preset));
    return;
  }
  if (!snap.mockPath.empty()) {
    static_cast<void>(previewer.MountPreviewWithMock(previewer.ActiveSource(), snap.mockPath, snap.preset));
    return;
  }
  if (snap.mode == MockSourceMode::ExplicitLua) {
    static_cast<void>(previewer.MountPreview(previewer.ActiveSource(), BuildInlineMockRestoreOpts(registry, snap)));
  } else if (snap.mode == MockSourceMode::AutoSeeded) {
    static_cast<void>(previewer.ResetToAutoSeeded());
  }
  if (snap.preset.starts_with('@')) {
    static_cast<void>(previewer.ApplyStressPreset(snap.preset));
  }
}

void RestoreLiveMatrixSnapshot(UIPreviewer& previewer, Registry* registry, const LiveMatrixSnapshot& snap) {
  RestoreMatrixMockBaseline(previewer, registry, snap);
  ApplySnapshotContextAndParams(previewer, registry, snap);

  if (snap.simW > 0 && snap.simH > 0) {
    previewer.SetSimulatedViewport(snap.simW, snap.simH);
  } else {
    previewer.ClearSimulatedViewport();
  }
}

std::string FormatMatrixReportJson(const UIPreviewer& previewer, const PreviewMatrixResult& result,
                                   const std::vector<std::pair<int, int>>& resolutions,
                                   const std::vector<std::string>& presets) {
  std::size_t totalIssues = 0;
  for (const auto& c : result.cells) totalIssues += c.issueCount;

  std::vector<std::string> resStrings;
  resStrings.reserve(resolutions.size());
  for (const auto& [w, h] : resolutions) {
    resStrings.push_back(std::to_string(w) + "x" + std::to_string(h));
  }

  std::ostringstream ss;
  ss << "{\n";
  ss << R"(  "source": ")" << detail::EscapeJsonString(previewer.ActiveSource()) << "\",\n";
  ss << R"(  "mock_path": ")" << detail::EscapeJsonString(previewer.ActiveMockPath()) << "\",\n";
  ss << R"(  "out_dir": ")" << detail::EscapeJsonString(result.outDir) << "\",\n";
  ss << R"(  "report_path": ")" << detail::EscapeJsonString(result.reportPath) << "\",\n";
  ss << R"(  "cell_count": )" << result.cells.size() << ",\n";
  ss << R"(  "total_issues": )" << totalIssues << ",\n";
  ss << R"(  "resolutions": )";
  detail::AppendJsonStringList(ss, resStrings);
  ss << ",\n" << R"(  "presets": )";
  detail::AppendJsonStringList(ss, presets);
  ss << ",\n" << R"(  "cells": [)";

  for (std::size_t i = 0; i < result.cells.size(); ++i) {
    const auto& cell = result.cells[i];
    if (i > 0) ss << ",";
    ss << "\n    {\n";
    ss << R"(      "preset": ")" << detail::EscapeJsonString(cell.preset) << "\",\n";
    ss << R"(      "width": )" << cell.width << ",\n";
    ss << R"(      "height": )" << cell.height << ",\n";
    ss << R"(      "ui_scale": )" << cell.uiScale << ",\n";
    ss << R"(      "capture_path": ")" << detail::EscapeJsonString(cell.capturePath) << "\",\n";
    ss << R"(      "capture_ok": )" << (cell.captureOk ? "true" : "false") << ",\n";
    ss << R"(      "issue_count": )" << cell.issueCount << ",\n";
    ss << R"(      "issues": )";
    detail::AppendJsonIssuesArray(ss, cell.issues, "      ");
    ss << "\n    }";
  }
  ss << (result.cells.empty() ? "]\n" : "\n  ]\n") << "}\n";
  return ss.str();
}

}  // namespace

void UIPreviewer::SetSimulatedViewport(const int width, const int height) {
  if (width <= 0 || height <= 0) {
    ClearSimulatedViewport();
    return;
  }
  if (registry_ != nullptr) {
    if (auto* cfg = registry_->TryGet<GameConfig>()) {
      if (!has_saved_unsimulated_size_) {
        saved_unsimulated_w_ = cfg->windowWidth;
        saved_unsimulated_h_ = cfg->windowHeight;
        has_saved_unsimulated_size_ = true;
      }
      cfg->windowWidth = width;
      cfg->windowHeight = height;
    }
    if (auto* uiRuntime = registry_->TryGet<UIRuntime>(); uiRuntime != nullptr && uiRuntime->GetLua() != nullptr) {
      (*uiRuntime->GetLua())["game_window_width"] = width;
      (*uiRuntime->GetLua())["game_window_height"] = height;
    }
  }
  sim_viewport_w_ = width;
  sim_viewport_h_ = height;
}

void UIPreviewer::RestoreSimulatedViewportIfOwned() {
  sim_viewport_w_ = 0;
  sim_viewport_h_ = 0;
  if (!has_saved_unsimulated_size_) return;
  if (registry_ != nullptr) {
    if (auto* cfg = registry_->TryGet<GameConfig>()) {
      cfg->windowWidth = saved_unsimulated_w_;
      cfg->windowHeight = saved_unsimulated_h_;
    }
    if (auto* uiRuntime = registry_->TryGet<UIRuntime>(); uiRuntime != nullptr && uiRuntime->GetLua() != nullptr) {
      (*uiRuntime->GetLua())["game_window_width"] = saved_unsimulated_w_;
      (*uiRuntime->GetLua())["game_window_height"] = saved_unsimulated_h_;
    }
  }
  has_saved_unsimulated_size_ = false;
}

void UIPreviewer::ClearSimulatedViewport() { RestoreSimulatedViewportIfOwned(); }

std::string UIPreviewer::ViewportInfoJson() const {
  const auto* cfg = registry_ != nullptr ? registry_->TryGet<GameConfig>() : nullptr;
  const int curW = HasSimulatedViewport() ? sim_viewport_w_ : (cfg != nullptr ? cfg->windowWidth : 0);
  const int curH = HasSimulatedViewport() ? sim_viewport_h_ : (cfg != nullptr ? cfg->windowHeight : 0);
  const float uiScale = cfg != nullptr ? octarine::ui::ResolveUiScale(*cfg) : 1.0f;
  std::ostringstream ss;
  ss << "{ \"width\": " << curW << ", \"height\": " << curH << ", \"ui_scale\": " << uiScale
     << ", \"simulated\": " << (HasSimulatedViewport() ? "true" : "false")
     << ", \"safe_areas\": " << (show_safe_area_guides_ ? "true" : "false")
     << ", \"integer_zoom\": " << (integer_zoom_sim_ ? "true" : "false") << " }\n";
  return ss.str();
}

std::vector<std::string> UIPreviewer::ListMockPresets() const {
  if (registry_ == nullptr) return {};
  const auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return {};
  const sol::object rawObj =
      detail::GetPinnedLuaRegistryObject(uiRuntime->GetLua()->lua_state(), "__octarine_preview_raw_mock");
  if (!rawObj.is<sol::table>()) return {};
  const sol::object presetsObj = rawObj.as<sol::table>().get<sol::object>("presets");
  if (!presetsObj.is<sol::table>()) return {};
  return ExtractSortedPresetKeys(presetsObj.as<sol::table>());
}

std::string UIPreviewer::DefaultMockPreset() const {
  if (registry_ == nullptr) return {};
  const auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return {};
  const sol::object rawObj =
      detail::GetPinnedLuaRegistryObject(uiRuntime->GetLua()->lua_state(), "__octarine_preview_raw_mock");
  if (!rawObj.is<sol::table>()) return {};
  const sol::table rawRoot = rawObj.as<sol::table>();
  if (!rawRoot.get<sol::object>("presets").is<sol::table>()) return {};
  std::string defKey;
  static_cast<void>(detail::ResolveEffectivePresetTable(rawRoot, "", defKey));
  return defKey;
}

bool UIPreviewer::ApplyStressPreset(const std::string& kind) {
  if (!IsActive() || registry_ == nullptr) return false;
  detail::MockSeedVariant variant = detail::MockSeedVariant::Compact;
  std::string canonicalTag;
  if (!TryParseStressVariant(kind, variant, canonicalTag)) return false;

  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (!detail::TryGetActiveViewContext(registry_, luaPtr, viewTbl, ctx)) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr) return false;

  sol::table rawSeed = luaPtr->create_table();
  detail::SeedFieldsIntoTableWithVariant(*luaPtr, registry_, rawSeed, active_contract_, variant,
                                         /*overwriteExisting=*/true);
  sol::table obs = uiRuntime->CreateObservable(rawSeed);
  const sol::protected_function setCtx = viewTbl.get<sol::protected_function>("set_context");
  if (!setCtx.valid()) return false;
  setCtx(viewTbl, obs);

  active_mock_preset_ = canonicalTag;
  mock_modified_ = true;
  last_mock_error_.clear();
  return true;
}

bool UIPreviewer::SelectMockPreset(const std::string& presetName) {
  if (!IsActive() || registry_ == nullptr || presetName.empty()) return false;
  if (presetName.front() == '@') {
    return ApplyStressPreset(presetName);
  }
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;
  sol::state& lua = *uiRuntime->GetLua();

  const sol::object rawObj = detail::GetPinnedLuaRegistryObject(lua.lua_state(), "__octarine_preview_raw_mock");
  if (!rawObj.is<sol::table>()) return false;
  const sol::table rawRoot = rawObj.as<sol::table>();
  const sol::object presetsObj = rawRoot.get<sol::object>("presets");
  if (!presetsObj.is<sol::table>()) return false;
  const sol::table presetsTbl = presetsObj.as<sol::table>();
  const sol::object targetObj = presetsTbl.get<sol::object>(presetName);
  if (!targetObj.is<sol::table>()) return false;

  sol::table mockOpts = lua.create_table();
  sol::table rawCtrl;
  detail::ApplyMockTableFields(*uiRuntime, targetObj.as<sol::table>(), mockOpts, rawCtrl, rawRoot);
  return MountResolvedPreview(active_source_, mockOpts, inline_oss_, is_inline_, mock_source_mode_, active_mock_path_,
                              target_sidecar_path_, rawRoot, presetName);
}

namespace {
std::string ResolvePresetKeyForExport(const sol::table& rawRoot, const std::string& targetPresetKey,
                                      const std::string& activeMockPreset) {
  if (!targetPresetKey.empty()) return targetPresetKey;
  if (activeMockPreset.starts_with('@')) return activeMockPreset.substr(1);
  if (!activeMockPreset.empty()) return activeMockPreset;
  std::string key;
  static_cast<void>(detail::ResolveEffectivePresetTable(rawRoot, "", key));
  return key.empty() ? "default" : key;
}

sol::table BuildPrevDefaultFromFlatRoot(sol::state& lua, const sol::table& cleanRoot) {
  sol::table prevDefault = lua.create_table();
  if (const sol::object pObj = cleanRoot.get<sol::object>("params"); pObj.is<sol::table>()) {
    prevDefault.set("params", pObj);
  }
  if (const sol::object cObj = cleanRoot.get<sol::object>("context"); cObj.is<sol::table>()) {
    prevDefault.set("context", cObj);
  } else if (cleanRoot.get<sol::object>("params") == sol::lua_nil &&
             cleanRoot.get<sol::object>("styles") == sol::lua_nil) {
    prevDefault.set("context", cleanRoot);
  }
  return prevDefault;
}

sol::table PromoteDocToPresetsRoot(sol::state& lua, const sol::table& cleanRoot, const sol::table& docTbl,
                                   const std::string& targetPresetKey, const std::string& defaultPresetOverride) {
  sol::table promoted = lua.create_table();
  sol::table presetsTbl = lua.create_table();
  if (cleanRoot.valid()) {
    if (const sol::object stylesObj = cleanRoot.get<sol::object>("styles"); stylesObj.is<sol::table>()) {
      promoted.set("styles", stylesObj);
    }
    if (targetPresetKey != "default") {
      presetsTbl.set("default", BuildPrevDefaultFromFlatRoot(lua, cleanRoot));
    }
    promoted.set("default_preset", defaultPresetOverride.empty() ? std::string("default") : defaultPresetOverride);
  } else {
    promoted.set("default_preset", defaultPresetOverride.empty() ? targetPresetKey : defaultPresetOverride);
  }
  presetsTbl.set(targetPresetKey, docTbl);
  promoted.set("presets", presetsTbl);
  return promoted;
}

sol::table ResolveDocTableToSerialize(sol::state& lua, const sol::table& docTbl, const std::string& targetPresetKey,
                                      const std::string& defaultPresetOverride, const std::string& activePreset,
                                      bool promoteFlatIfNeeded, bool& outHasPresets) {
  outHasPresets = false;
  const sol::object rawMockObj = detail::GetPinnedLuaRegistryObject(lua.lua_state(), "__octarine_preview_raw_mock");
  sol::table cleanRoot;
  if (rawMockObj.is<sol::table>()) {
    const sol::table rawRoot = rawMockObj.as<sol::table>();
    if (const sol::object cleanObj = detail::UnwrapObservableDataDeep(lua, rawRoot); cleanObj.is<sol::table>()) {
      cleanRoot = cleanObj.as<sol::table>();
      detail::PruneEmptyControllerTable(cleanRoot);
      if (const sol::object presetsObj = cleanRoot.get<sol::object>("presets"); presetsObj.is<sol::table>()) {
        sol::table presetsTbl = presetsObj.as<sol::table>();
        presetsTbl.set(ResolvePresetKeyForExport(rawRoot, targetPresetKey, activePreset), docTbl);
        if (!defaultPresetOverride.empty()) cleanRoot.set("default_preset", defaultPresetOverride);
        outHasPresets = true;
        return cleanRoot;
      }
    }
  }
  if (promoteFlatIfNeeded && !targetPresetKey.empty()) {
    outHasPresets = true;
    return PromoteDocToPresetsRoot(lua, cleanRoot, docTbl, targetPresetKey, defaultPresetOverride);
  }
  return docTbl;
}

void SyncSavedPresetMetadata(Registry* registry, sol::state& lua, const std::string& dest, const std::string& key,
                             std::string& outActivePreset, bool& outHasController) {
  auto* uiRuntime = registry != nullptr ? registry->TryGet<UIRuntime>() : nullptr;
  if (uiRuntime == nullptr) {
    outActivePreset = key;
    outHasController = false;
    return;
  }
  sol::table nextOpts = lua.create_table();
  sol::table nextCtrl;
  sol::table nextRoot;
  std::string resolvedKey;
  bool hasCtrl = false;
  std::string loadErr;
  if (detail::LoadSandboxedMockScript(*uiRuntime, dest, key, nextOpts, nextCtrl, nextRoot, resolvedKey, hasCtrl,
                                      loadErr)) {
    detail::PinLuaRegistryObject(lua.lua_state(), "__octarine_preview_raw_mock", nextRoot);
    detail::PinLuaRegistryObject(lua.lua_state(), "__octarine_preview_controller", nextCtrl);
    outHasController = hasCtrl;
    outActivePreset = resolvedKey.empty() ? key : resolvedKey;
  }
}

bool RemovePresetEntryFromRoot(sol::state& lua, const std::string& presetName, const std::string& activePreset,
                               sol::table& outCleanRoot, std::string& outNextPreset, std::string& outErr) {
  const sol::object rawObj = detail::GetPinnedLuaRegistryObject(lua.lua_state(), "__octarine_preview_raw_mock");
  if (!rawObj.is<sol::table>()) return false;
  const sol::object cleanObj = detail::UnwrapObservableDataDeep(lua, rawObj.as<sol::table>());
  if (!cleanObj.is<sol::table>()) return false;

  outCleanRoot = cleanObj.as<sol::table>();
  detail::PruneEmptyControllerTable(outCleanRoot);
  const sol::object presetsObj = outCleanRoot.get<sol::object>("presets");
  if (!presetsObj.is<sol::table>()) return false;

  sol::table presetsTbl = presetsObj.as<sol::table>();
  if (!presetsTbl.get<sol::object>(presetName).is<sol::table>()) return false;
  if (ExtractSortedPresetKeys(presetsTbl).size() <= 1) {
    outErr = "Cannot delete the only remaining preset";
    return false;
  }

  presetsTbl.set(presetName, sol::lua_nil);
  const auto remaining = ExtractSortedPresetKeys(presetsTbl);
  const sol::object curDefObj = outCleanRoot.get<sol::object>("default_preset");
  const std::string curDef = curDefObj.is<std::string>() ? curDefObj.as<std::string>() : std::string();
  outNextPreset = (!curDef.empty() && curDef != presetName) ? curDef : remaining.front();
  if (curDef == presetName || curDef.empty()) {
    outCleanRoot.set("default_preset", outNextPreset);
  }
  if (!activePreset.empty() && !activePreset.starts_with('@') && activePreset != presetName) {
    outNextPreset = activePreset;
  }
  return true;
}

bool ActivateMatrixPreset(UIPreviewer& previewer, Registry* registry, const LiveMatrixSnapshot& snap,
                          const std::string& preset, const std::size_t pIdx, const bool hasPresetsTbl) {
  if (preset != "default" || hasPresetsTbl) {
    return previewer.SelectMockPreset(preset);
  }
  if (pIdx > 0) {
    if (!snap.mockPath.empty()) {
      static_cast<void>(previewer.MountPreviewWithMock(previewer.ActiveSource(), snap.mockPath, ""));
    }
    ApplySnapshotContextAndParams(previewer, registry, snap);
  }
  return true;
}

PreviewMatrixCellResult RunSingleMatrixCell(UIPreviewer& previewer, Registry* registry, EngineContext* engCtx,
                                            const GameConfig* cfg, const UIPreviewer::CaptureCallback& capCb,
                                            const std::string& outDir, const std::string& preset,
                                            const std::string& slug, const bool presetOk, const int w, const int h) {
  PreviewMatrixCellResult cell;
  cell.preset = preset;
  cell.width = w;
  cell.height = h;
  cell.capturePath =
      (fs::path(outDir) / (slug + "_" + std::to_string(w) + "x" + std::to_string(h) + ".png")).generic_string();

  if (!presetOk) {
    cell.uiScale = cfg != nullptr ? octarine::ui::ResolveUiScale(*cfg) : 1.0f;
    cell.captureOk = false;
    cell.issues.push_back({
        .code = "missing_preset",
        .severity = "error",
        .label = preset,
        .message = "Requested preview preset '" + preset + "' was not found.",
    });
    cell.issueCount = cell.issues.size();
    return cell;
  }

  previewer.SetSimulatedViewport(w, h);
  if (engCtx != nullptr) {
    engCtx->sceneRenderScale = 1.0f;
    engCtx->sceneRenderScaleX = 1.0f;
    engCtx->sceneRenderScaleY = 1.0f;
  }
  previewer.RenderWhenPaused(*registry);
  previewer.RenderWhenPaused(*registry);

  cell.uiScale = cfg != nullptr ? octarine::ui::ResolveUiScale(*cfg) : 1.0f;
  cell.captureOk = static_cast<bool>(capCb) && capCb(cell.capturePath);
  cell.issues = previewer.AuditLayout();
  cell.issueCount = cell.issues.size();
  return cell;
}
}  // namespace

std::string UIPreviewer::ExportMockLuaScriptForDoc(const sol::table& docTbl, const std::string& targetPresetKey,
                                                   const std::string& defaultPresetOverride,
                                                   const bool promoteFlatIfNeeded, bool* outHasPresets) const {
  if (registry_ == nullptr) return "";
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return "";
  sol::state& lua = *uiRuntime->GetLua();

  bool hasPresets = false;
  sol::table tblToSerialize = ResolveDocTableToSerialize(lua, docTbl, targetPresetKey, defaultPresetOverride,
                                                         active_mock_preset_, promoteFlatIfNeeded, hasPresets);
  detail::PruneEmptyControllerTable(tblToSerialize);
  if (outHasPresets != nullptr) *outHasPresets = hasPresets;

  std::string body;
  std::string err;
  if (!lua_table_serializer::Serialize(tblToSerialize, body, &err)) {
    Logger::Error("[ui-preview] ExportMockLuaScript serialize error: " + err);
    return "";
  }
  std::ostringstream ss;
  if (!hasPresets) detail::AppendCallbackCommentHeader(ss, active_contract_);
  ss << "return " << body << "\n";
  return ss.str();
}

bool UIPreviewer::SaveMockPreset(const std::string& presetName, const bool setAsDefault) {
  if (!IsActive() || is_inline_ || registry_ == nullptr) return false;
  const std::string key = NormalizePresetLookupKey(presetName);
  if (key.empty()) return false;

  const std::string dest = !active_mock_path_.empty() ? active_mock_path_ : target_sidecar_path_;
  if (dest.empty()) {
    last_mock_error_ = "No disk path available for .preview.lua sidecar";
    return false;
  }

  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (!detail::TryGetActiveViewContext(registry_, luaPtr, viewTbl, ctx)) return false;

  const sol::table docTbl = detail::BuildLiveDocSnapshot(*luaPtr, *this, ctx);
  const std::string scriptText =
      ExportMockLuaScriptForDoc(docTbl, key, setAsDefault ? key : "", /*promoteFlatIfNeeded=*/true);
  if (scriptText.empty()) {
    last_mock_error_ = "Failed to serialize preset to Lua";
    return false;
  }

  std::string writeErr;
  if (!detail::TryWriteTextFile(dest, scriptText, writeErr)) {
    last_mock_error_ = writeErr;
    return false;
  }

  active_mock_path_ = dest;
  detail::RecordMockMtime(dest, last_mock_write_time_, has_mock_write_time_);
  mock_source_mode_ = detail::AreSameDiskPath(dest, target_sidecar_path_) ? MockSourceMode::SidecarPreview
                                                                          : MockSourceMode::ExplicitLua;
  SyncSavedPresetMetadata(registry_, *luaPtr, dest, key, active_mock_preset_, has_live_mock_controller_);
  mock_modified_ = false;
  last_mock_error_.clear();
  return true;
}

bool UIPreviewer::DeleteMockPreset(const std::string& presetName) {
  if (!IsActive() || is_inline_ || registry_ == nullptr || presetName.empty()) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;
  sol::state& lua = *uiRuntime->GetLua();

  sol::table cleanRoot;
  std::string nextPreset;
  std::string removeErr;
  if (!RemovePresetEntryFromRoot(lua, presetName, active_mock_preset_, cleanRoot, nextPreset, removeErr)) {
    if (!removeErr.empty()) last_mock_error_ = removeErr;
    return false;
  }

  const std::string dest = !active_mock_path_.empty() ? active_mock_path_ : target_sidecar_path_;
  if (dest.empty()) return false;

  std::string body;
  std::string serErr;
  if (!lua_table_serializer::Serialize(cleanRoot, body, &serErr)) {
    last_mock_error_ = serErr;
    return false;
  }
  std::string writeErr;
  if (!detail::TryWriteTextFile(dest, "return " + body + "\n", writeErr)) {
    last_mock_error_ = writeErr;
    return false;
  }

  detail::RecordMockMtime(dest, last_mock_write_time_, has_mock_write_time_);
  sol::table mockOpts = lua.create_table();
  sol::table rawCtrl;
  sol::table rawRoot;
  std::string err;
  std::string loadedKey;
  bool hasCtrl = false;
  if (!detail::LoadSandboxedMockScript(*uiRuntime, dest, nextPreset, mockOpts, rawCtrl, rawRoot, loadedKey, hasCtrl,
                                       err)) {
    last_mock_error_ = err;
    return false;
  }
  return MountResolvedPreview(active_source_, mockOpts, "", false, mock_source_mode_, dest, target_sidecar_path_,
                              rawRoot, loadedKey);
}

std::string UIPreviewer::ListPresetsJson() const {
  const auto authored = ListMockPresets();
  const std::string defPreset = DefaultMockPreset();
  std::ostringstream ss;
  ss << "{\n";
  ss << R"(  "active_preset": ")" << detail::EscapeJsonString(active_mock_preset_) << "\",\n";
  ss << R"(  "default_preset": ")" << detail::EscapeJsonString(defPreset) << "\",\n";
  ss << R"(  "has_controller": )" << (has_live_mock_controller_ ? "true" : "false") << ",\n";
  ss << R"(  "presets": )";
  detail::AppendJsonStringList(ss, authored);
  ss << ",\n" << R"(  "stress_presets": ["@default", "@empty", "@overflow"])" << "\n}\n";
  return ss.str();
}

std::vector<std::pair<int, int>> UIPreviewer::ParseResolutionsCsv(const std::string& csv) {
  std::vector<std::pair<int, int>> out;
  for (const std::string& token : SplitCsvTrimmed(csv)) {
    int w = 0;
    int h = 0;
    if (TryParseSingleResolution(token, w, h)) {
      out.emplace_back(w, h);
    }
  }
  return out;
}

PreviewMatrixResult UIPreviewer::RunPreviewMatrix(const PreviewMatrixOptions& opts) {
  PreviewMatrixResult result;
  if (!IsActive() || registry_ == nullptr || opts.outDir.empty()) return result;

  std::vector<std::pair<int, int>> resolutions = opts.resolutions;
  if (resolutions.empty()) resolutions = {{1920, 1080}, {1280, 800}, {800, 600}};
  const std::vector<std::string> presets = ResolveMatrixPresetList(*this, opts.presets);
  if (presets.empty()) return result;

  std::error_code ec;
  fs::create_directories(fs::path(opts.outDir), ec);
  if (ec) {
    Logger::Error("[ui-preview] failed creating matrix directory '" + opts.outDir + "': " + ec.message());
    return result;
  }

  const LiveMatrixSnapshot snap = CaptureLiveMatrixSnapshot(*this, registry_);
  auto* engCtx = registry_->TryGet<EngineContext>();
  const float prevScale = engCtx != nullptr ? engCtx->sceneRenderScale : 1.0f;
  const float prevScaleX = engCtx != nullptr ? engCtx->sceneRenderScaleX : 1.0f;
  const float prevScaleY = engCtx != nullptr ? engCtx->sceneRenderScaleY : 1.0f;

  const auto* cfg = registry_->TryGet<GameConfig>();
  result.outDir = opts.outDir;

  const bool hasPresetsTbl = snap.rawMockRoot.valid() && snap.rawMockRoot.get<sol::object>("presets").is<sol::table>();
  for (std::size_t pIdx = 0; pIdx < presets.size(); ++pIdx) {
    const std::string& preset = presets[pIdx];
    const bool presetOk = ActivateMatrixPreset(*this, registry_, snap, preset, pIdx, hasPresetsTbl);
    const std::string slug = SanitizePresetFilenameSlug(preset);
    for (const auto& [w, h] : resolutions) {
      result.cells.push_back(RunSingleMatrixCell(*this, registry_, engCtx, cfg, capture_callback_, opts.outDir, preset,
                                                 slug, presetOk, w, h));
    }
  }

  RestoreLiveMatrixSnapshot(*this, registry_, snap);
  if (engCtx != nullptr) {
    engCtx->sceneRenderScale = prevScale;
    engCtx->sceneRenderScaleX = prevScaleX;
    engCtx->sceneRenderScaleY = prevScaleY;
  }
  if (auto* rq = registry_->TryGet<RenderQueue>()) rq->Clear();

  result.reportPath = (fs::path(opts.outDir) / "matrix_report.json").generic_string();
  result.reportJson = FormatMatrixReportJson(*this, result, resolutions, presets);

  std::string writeErr;
  result.ok = detail::TryWriteTextFile(result.reportPath, result.reportJson, writeErr);
  if (!result.ok) Logger::Error("[ui-preview] failed writing matrix report: " + writeErr);
  return result;
}

std::string UIPreviewer::RunPreviewMatrixJson(const std::string& outDir, const std::string& resolutionsCsv,
                                              const std::string& presetsCsv) {
  PreviewMatrixOptions opts;
  opts.outDir = outDir;
  if (!resolutionsCsv.empty()) {
    opts.resolutions = ParseResolutionsCsv(resolutionsCsv);
    if (opts.resolutions.empty()) {
      Logger::Error("[ui-preview] invalid --ui-resolutions specification: '" + resolutionsCsv + "'");
      return "{ \"error\": \"No valid WxH resolutions parsed\", \"cell_count\": 0, \"cells\": [] }\n";
    }
  }
  if (!presetsCsv.empty()) {
    opts.presets = SplitCsvTrimmed(presetsCsv);
  }
  const PreviewMatrixResult res = RunPreviewMatrix(opts);
  return res.reportJson.empty() ? "{}\n" : res.reportJson;
}

#endif  // !OCTARINE_SHIPPED
