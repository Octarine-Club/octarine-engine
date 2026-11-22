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
#include "UI/UIPreviewerMockSource.h"
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

bool CanPreserveControllerFromSource(UIRuntime* uiRuntime, const std::string& sourcePath) {
  if (uiRuntime == nullptr || sourcePath.empty()) return false;
  std::string err;
  const std::string srcText = ReadFileOrAssetText(uiRuntime, sourcePath, err);
  if (srcText.empty()) return false;
  MockControllerSnippets snippets;
  return ExtractMockControllerSnippets(srcText, snippets) && snippets.HasAnyController();
}

bool TryPreserveControllerOnSave(UIRuntime& uiRuntime, const std::string& serializedBody, const std::string& sourcePath,
                                 const std::string& requestedPreset, std::string& outScriptText) {
  if (sourcePath.empty() || uiRuntime.GetLua() == nullptr) return false;
  std::string readErr;
  const std::string srcText = ReadFileOrAssetText(&uiRuntime, sourcePath, readErr);
  MockControllerSnippets snippets;
  if (srcText.empty() || !ExtractMockControllerSnippets(srcText, snippets) || !snippets.HasAnyController()) {
    return false;
  }
  const std::string candidate = InjectMockControllerSnippets(serializedBody, snippets);
  sol::table opts = uiRuntime.GetLua()->create_table();
  sol::table ctrl;
  sol::table root;
  std::string key;
  bool hasCtrl = false;
  std::string valErr;
  if (!LoadSandboxedMockScriptText(uiRuntime, candidate, sourcePath, requestedPreset, opts, ctrl, root, key, hasCtrl,
                                   valErr) ||
      !hasCtrl) {
    return false;
  }
  outScriptText = candidate;
  return true;
}

bool BuildSafeMockScriptForSave(UIRuntime& uiRuntime, const std::string& serializedBody, const bool hasPresets,
                                const OxmlContract& contract, const std::string& sourcePath,
                                const std::string& requestedPreset, const bool hasLiveController, const bool force,
                                std::string& outScriptText, std::string& outError) {
  outScriptText.clear();
  outError.clear();

  if (hasLiveController &&
      TryPreserveControllerOnSave(uiRuntime, serializedBody, sourcePath, requestedPreset, outScriptText)) {
    return true;
  }

  if (hasLiveController && !force) {
    outError =
        "Mock script contains Lua controller functions that cannot be preserved automatically; pass force=true to "
        "overwrite.";
    return false;
  }

  std::ostringstream ss;
  if (!hasPresets) AppendCallbackCommentHeader(ss, contract);
  ss << "return " << serializedBody << "\n";
  outScriptText = ss.str();
  return true;
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
  ss << R"(  "html_report_path": ")" << detail::EscapeJsonString(result.htmlReportPath) << "\",\n";
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

std::string EscapeHtmlText(std::string_view sv) {
  std::string out;
  out.reserve(sv.size());
  for (const char c : sv) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#39;";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

void CountCellSeverities(const std::vector<UILayoutIssue>& issues, std::size_t& outErrors, std::size_t& outWarnings) {
  outErrors = 0;
  outWarnings = 0;
  for (const auto& issue : issues) {
    if (issue.severity == "error") {
      ++outErrors;
    } else {
      ++outWarnings;
    }
  }
}

void AppendMatrixCellIssueListHtml(std::ostringstream& ss, const std::vector<UILayoutIssue>& issues) {
  if (issues.empty()) return;
  ss << R"(      <details class="issues"><summary>)" << issues.size() << " finding(s)</summary><ul>\n";
  for (const auto& issue : issues) {
    const char* cls = (issue.severity == "error") ? "sev-error" : "sev-warn";
    ss << "        <li><span class=\"" << cls << "\">[" << EscapeHtmlText(issue.severity) << "]</span> <code>"
       << EscapeHtmlText(issue.code) << "</code> - <strong>" << EscapeHtmlText(issue.label)
       << "</strong>: " << EscapeHtmlText(issue.message) << "</li>\n";
  }
  ss << "      </ul></details>\n";
}

void AppendMatrixCellCardHtml(std::ostringstream& ss, const PreviewMatrixCellResult& cell) {
  std::size_t errors = 0;
  std::size_t warnings = 0;
  CountCellSeverities(cell.issues, errors, warnings);

  const std::string relCapture = fs::path(cell.capturePath).filename().generic_string();
  const std::string escapedFile = EscapeHtmlText(relCapture);
  const std::string escapedPreset = EscapeHtmlText(cell.preset);

  std::string badgeClass = "badge-pass";
  std::string badgeText = "PASS";
  if (!cell.captureOk) {
    badgeClass = "badge-fail";
    badgeText = "CAPTURE FAILED";
  } else if (errors > 0) {
    badgeClass = "badge-fail";
    badgeText = std::to_string(errors) + " ERROR" + (warnings > 0 ? ", " + std::to_string(warnings) + " WARN" : "");
  } else if (warnings > 0) {
    badgeClass = "badge-warn";
    badgeText = std::to_string(warnings) + " WARN";
  }

  ss << "    <article class=\"card\">\n";
  ss << "      <div class=\"card-head\"><div><strong>" << escapedPreset << "</strong> <span class=\"meta\">"
     << cell.width << "x" << cell.height << " (scale " << std::fixed << std::setprecision(2) << cell.uiScale
     << "x)</span></div><span class=\"badge " << badgeClass << "\">" << badgeText << "</span></div>\n";
  if (cell.captureOk && !relCapture.empty()) {
    ss << R"(      <a class="shot-link" href=")" << escapedFile << R"(" target="_blank" rel="noopener"><img src=")"
       << escapedFile << R"(" loading="lazy" alt=")" << escapedPreset << " " << cell.width << "x" << cell.height
       << "\" /></a>\n";
  } else {
    ss << "      <div class=\"missing\">Capture unavailable (" << escapedFile << ")</div>\n";
  }
  AppendMatrixCellIssueListHtml(ss, cell.issues);
  ss << "    </article>\n";
}

std::string FormatMatrixReportHtml(const UIPreviewer& previewer, const PreviewMatrixResult& result,
                                   const std::vector<std::pair<int, int>>& resolutions) {
  std::size_t totalErrors = 0;
  std::size_t totalWarnings = 0;
  for (const auto& cell : result.cells) {
    std::size_t e = 0;
    std::size_t w = 0;
    CountCellSeverities(cell.issues, e, w);
    totalErrors += e;
    totalWarnings += w;
  }

  const std::size_t colCount = std::max<std::size_t>(1, resolutions.size());
  std::ostringstream ss;
  ss << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\" />\n"
     << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
     << "<title>Octarine UI Matrix - " << EscapeHtmlText(previewer.ActiveSource()) << "</title>\n"
     << "<style>\n"
     << "  :root { color-scheme: dark; --bg: #0f131a; --card: #171c26; --border: #2b3344; --text: #e6edf5;"
     << " --muted: #9aa7bd; --pass: #2ea043; --warn: #d29922; --fail: #f85149; }\n"
     << "  * { box-sizing: border-box; }\n"
     << "  body { margin: 0; padding: 20px; font-family: system-ui, -apple-system, sans-serif;"
     << " background: var(--bg); color: var(--text); }\n"
     << "  header { display: flex; flex-wrap: wrap; justify-content: space-between; align-items: baseline;"
     << " gap: 12px; margin-bottom: 18px; padding-bottom: 12px; border-bottom: 1px solid var(--border); }\n"
     << "  h1 { margin: 0; font-size: 18px; }\n"
     << "  .summary { display: flex; flex-wrap: wrap; gap: 12px; font-size: 13px; color: var(--muted); }\n"
     << "  .summary a { color: #79c0ff; text-decoration: none; }\n"
     << "  .grid { display: grid; gap: 14px; overflow-x: auto; }\n"
     << "  .card { background: var(--card); border: 1px solid var(--border); border-radius: 8px; overflow: hidden;"
     << " display: flex; flex-direction: column; }\n"
     << "  .card-head { display: flex; justify-content: space-between; align-items: center; gap: 8px;"
     << " padding: 10px 12px; border-bottom: 1px solid var(--border); font-size: 13px; }\n"
     << "  .meta { color: var(--muted); font-weight: 400; }\n"
     << "  .badge { font-size: 11px; font-weight: 700; padding: 2px 7px; border-radius: 999px; color: #fff;"
     << " white-space: nowrap; }\n"
     << "  .badge-pass { background: var(--pass); } .badge-warn { background: var(--warn); color: #111; }"
     << " .badge-fail { background: var(--fail); }\n"
     << "  .shot-link { display: block; background: #090c10; }\n"
     << "  .shot-link img { width: 100%; height: auto; display: block; object-fit: contain; }\n"
     << "  .missing { padding: 36px 12px; text-align: center; color: var(--muted); background: #090c10;"
     << " font-size: 12px; }\n"
     << "  .issues { padding: 8px 12px; font-size: 12px; border-top: 1px solid var(--border); }\n"
     << "  .issues summary { cursor: pointer; color: #f2cc60; }\n"
     << "  .issues ul { margin: 8px 0 0; padding-left: 18px; }\n"
     << "  .issues li { margin-bottom: 4px; color: var(--muted); }\n"
     << "  .sev-error { color: #ff7b72; font-weight: 600; } .sev-warn { color: #f2cc60; font-weight: 600; }\n"
     << "</style>\n</head>\n<body>\n";

  ss << "<header>\n  <div><h1>Octarine UI Preview Matrix: <code>" << EscapeHtmlText(previewer.ActiveSource())
     << "</code></h1></div>\n  <div class=\"summary\"><span>Cells: <strong>" << result.cells.size()
     << "</strong></span><span>Errors: <strong class=\"sev-error\">" << totalErrors
     << "</strong></span><span>Warnings: <strong class=\"sev-warn\">" << totalWarnings
     << "</strong></span><a href=\"matrix_report.json\">matrix_report.json</a></div>\n</header>\n";

  ss << R"(<main class="grid" style="grid-template-columns: repeat()" << colCount << ", minmax(280px, 1fr));\">\n";
  for (const auto& cell : result.cells) {
    AppendMatrixCellCardHtml(ss, cell);
  }
  ss << "</main>\n</body>\n</html>\n";
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
                                                   const bool promoteFlatIfNeeded, bool* outHasPresets,
                                                   std::string* outSerializedBody) const {
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
  if (outSerializedBody != nullptr) *outSerializedBody = body;
  std::ostringstream ss;
  if (!hasPresets) detail::AppendCallbackCommentHeader(ss, active_contract_);
  ss << "return " << body << "\n";
  return ss.str();
}

bool UIPreviewer::SaveMockPreset(const std::string& presetName, const bool setAsDefault, const bool force) {
  if (!IsActive() || is_inline_ || registry_ == nullptr) return false;
  const std::string key = NormalizePresetLookupKey(presetName);
  if (key.empty()) return false;

  const std::string dest = !active_mock_path_.empty() ? active_mock_path_ : target_sidecar_path_;
  if (dest.empty()) {
    last_mock_error_ = "No disk path available for .preview.lua sidecar";
    return false;
  }

  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (uiRuntime == nullptr || !detail::TryGetActiveViewContext(registry_, luaPtr, viewTbl, ctx)) return false;

  bool hasPresets = false;
  std::string serializedBody;
  const sol::table docTbl = detail::BuildLiveDocSnapshot(*luaPtr, *this, ctx);
  if (ExportMockLuaScriptForDoc(docTbl, key, setAsDefault ? key : "", /*promoteFlatIfNeeded=*/true, &hasPresets,
                                &serializedBody)
          .empty()) {
    last_mock_error_ = "Failed to serialize preset to Lua";
    return false;
  }

  const std::string& sourcePath = !active_mock_path_.empty() ? active_mock_path_ : dest;
  std::string scriptText;
  std::string safeErr;
  if (!detail::BuildSafeMockScriptForSave(*uiRuntime, serializedBody, hasPresets, active_contract_, sourcePath, key,
                                          has_live_mock_controller_, force, scriptText, safeErr)) {
    last_mock_error_ = safeErr;
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

bool UIPreviewer::DeleteMockPreset(const std::string& presetName, const bool force) {
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
  const std::string& sourcePath = !active_mock_path_.empty() ? active_mock_path_ : dest;
  std::string scriptText;
  std::string safeErr;
  if (!detail::BuildSafeMockScriptForSave(*uiRuntime, body, /*hasPresets=*/true, active_contract_, sourcePath,
                                          nextPreset, has_live_mock_controller_, force, scriptText, safeErr)) {
    last_mock_error_ = safeErr;
    return false;
  }

  std::string writeErr;
  if (!detail::TryWriteTextFile(dest, scriptText, writeErr)) {
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

namespace {
void WriteMatrixReports(const UIPreviewer& previewer, const std::string& outDir,
                        const std::vector<std::pair<int, int>>& resolutions, const std::vector<std::string>& presets,
                        PreviewMatrixResult& result) {
  result.reportPath = (fs::path(outDir) / "matrix_report.json").generic_string();
  result.htmlReportPath = (fs::path(outDir) / "index.html").generic_string();
  result.reportJson = FormatMatrixReportJson(previewer, result, resolutions, presets);

  std::string writeErr;
  const bool jsonOk = detail::TryWriteTextFile(result.reportPath, result.reportJson, writeErr);
  if (!jsonOk) Logger::Error("[ui-preview] failed writing matrix report: " + writeErr);

  std::string htmlErr;
  const std::string htmlText = FormatMatrixReportHtml(previewer, result, resolutions);
  const bool htmlOk = detail::TryWriteTextFile(result.htmlReportPath, htmlText, htmlErr);
  if (!htmlOk) Logger::Error("[ui-preview] failed writing matrix HTML contact sheet: " + htmlErr);

  result.ok = jsonOk && htmlOk;
}
}  // namespace

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

  WriteMatrixReports(*this, opts.outDir, resolutions, presets, result);
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
