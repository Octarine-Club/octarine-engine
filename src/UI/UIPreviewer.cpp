#include "UI/UIPreviewer.h"

#ifndef OCTARINE_SHIPPED

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>

#include "AssetManager/AssetCatalog.h"
#include "AssetManager/AssetManager.h"
#include "Components/CameraComponents.h"
#include "Components/NameComponent.h"
#include "Components/SpriteComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "Components/TextLabelComponent.h"
#include "Components/UIAnchorComponent.h"
#include "Components/UIButtonComponent.h"
#include "Components/UICanvasComponent.h"
#include "Components/UIClipComponent.h"
#include "Components/UIFlexComponent.h"
#include "Components/UIFocusComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/UIScrollComponent.h"
#include "Components/UIStyleStateComponent.h"
#include "ECS/Context.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Game/EngineContext.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "Lua/LuaTableSerializer.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/SpriteRenderCache.h"
#include "Systems/RenderTextSystem.h"
#include "Systems/RenderUIPrimitiveSystem.h"
#include "Systems/RenderUISpriteSystem.h"
#include "Systems/TransformSystem.h"
#include "Systems/UILayoutMetrics.h"
#include "Systems/UILayoutSystem.h"
#include "UI/UIPreviewerInternal.h"
#include "UI/UIRuntime.h"

namespace octarine::ui_preview_detail {
namespace {
namespace fs = std::filesystem;
bool ShouldCopyDataKey(const sol::object& k) {
  if (k.is<std::string>()) return !IsObservableInternalKey(k.as<std::string>());
  return k.is<double>() || k.is<bool>();
}
}  // namespace

std::string EscapeJsonString(std::string_view input) {
  std::ostringstream ss;
  for (const char c : input) {
    switch (c) {
      case '"':
        ss << "\\\"";
        break;
      case '\\':
        ss << "\\\\";
        break;
      case '\n':
        ss << "\\n";
        break;
      case '\r':
        ss << "\\r";
        break;
      case '\t':
        ss << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20U) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(c)));
          ss << buf;
        } else {
          ss << c;
        }
        break;
    }
  }
  return ss.str();
}

const char* MockSourceModeName(const MockSourceMode mode) {
  switch (mode) {
    case MockSourceMode::None:
      return "none";
    case MockSourceMode::ExplicitLua:
      return "explicit_lua";
    case MockSourceMode::SidecarPreview:
      return "sidecar_preview";
    case MockSourceMode::AutoSeeded:
      return "auto_seeded";
  }
  return "none";
}

bool IsObservableInternalKey(std::string_view key) {
  return key == "__is_observable" || key == "__raw" || key == "__listeners" || key == "__subscribe" ||
         key == "__forward_sinks";
}

sol::table UnwrapObservableShallow(const sol::table& tbl) {
  if (!tbl.valid()) return tbl;
  const sol::object isObs = tbl.get<sol::object>("__is_observable");
  if (isObs.is<bool>() && isObs.as<bool>()) {
    const sol::object raw = tbl.get<sol::object>("__raw");
    if (raw.is<sol::table>()) {
      return raw.as<sol::table>();
    }
  }
  return tbl;
}

// Recursively copies data tables while stripping observable proxies and non-serializable types.
// NOLINTNEXTLINE(misc-no-recursion)
sol::object UnwrapObservableDataDeepImpl(sol::state& lua, const sol::object& val, const int depth,
                                         std::unordered_set<const void*>& visited) {
  if (!val.valid() || val == sol::lua_nil || depth > 24) return sol::lua_nil;
  if (val.is<bool>() || val.is<double>() || val.is<std::string>()) return val;
  if (!val.is<sol::table>()) return sol::lua_nil;

  const sol::table raw = UnwrapObservableShallow(val.as<sol::table>());
  if (!visited.insert(raw.pointer()).second) return sol::lua_nil;

  sol::table copy = lua.create_table();
  for (const auto& [k, v] : raw) {
    if (!ShouldCopyDataKey(k)) continue;
    if (const sol::object cleanVal = UnwrapObservableDataDeepImpl(lua, v, depth + 1, visited);
        cleanVal != sol::lua_nil) {
      copy[k] = cleanVal;
    }
  }
  visited.erase(raw.pointer());
  return copy;
}

sol::object UnwrapObservableDataDeep(sol::state& lua, const sol::object& val) {
  std::unordered_set<const void*> visited;
  return UnwrapObservableDataDeepImpl(lua, val, 0, visited);
}

// Bounded check for any Lua function handlers inside controller tables (depth <= 2).
// NOLINTNEXTLINE(misc-no-recursion)
bool ContainsFunctionValue(const sol::table& tbl, const int depth) {
  if (!tbl.valid() || depth > 2) return false;
  const sol::table raw = UnwrapObservableShallow(tbl);
  for (const auto& [k, v] : raw) {
    static_cast<void>(k);
    if (v.is<sol::protected_function>()) return true;
    if (depth < 2 && v.is<sol::table>() && ContainsFunctionValue(v.as<sol::table>(), depth + 1)) return true;
  }
  return false;
}

bool HasAnyControllerFunction(const sol::table& rawRoot, const sol::table& activeCtrl) {
  if (ContainsFunctionValue(activeCtrl)) return true;
  if (!rawRoot.valid()) return false;
  if (const sol::object rootCtrl = rawRoot.get<sol::object>("controller");
      rootCtrl.is<sol::table>() && ContainsFunctionValue(rootCtrl.as<sol::table>())) {
    return true;
  }
  if (const sol::object presetsObj = rawRoot.get<sol::object>("presets"); presetsObj.is<sol::table>()) {
    for (const auto& [k, v] : presetsObj.as<sol::table>()) {
      static_cast<void>(k);
      if (!v.is<sol::table>()) continue;
      if (const sol::object subCtrl = v.as<sol::table>().get<sol::object>("controller");
          subCtrl.is<sol::table>() && ContainsFunctionValue(subCtrl.as<sol::table>())) {
        return true;
      }
    }
  }
  return false;
}

void PruneEmptyControllerTable(sol::table& tbl) {
  if (!tbl.valid()) return;
  auto pruneOne = [](sol::table& t) {
    const sol::object c = t.get<sol::object>("controller");
    if (c.is<sol::table>() && c.as<sol::table>().empty()) {
      t.set("controller", sol::lua_nil);
    }
  };
  pruneOne(tbl);
  if (const sol::object presetsObj = tbl.get<sol::object>("presets"); presetsObj.is<sol::table>()) {
    for (const auto& [k, v] : presetsObj.as<sol::table>()) {
      static_cast<void>(k);
      if (v.is<sol::table>()) {
        sol::table sub = v.as<sol::table>();
        pruneOne(sub);
      }
    }
  }
}

void AppendJsonStringList(std::ostringstream& ss, const std::vector<std::string>& items) {
  ss << "[";
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) ss << ", ";
    ss << "\"" << EscapeJsonString(items[i]) << "\"";
  }
  ss << "]";
}

void AppendJsonIssuesArray(std::ostringstream& ss, const std::vector<UILayoutIssue>& issues,
                           const std::string_view indent) {
  ss << "[";
  for (std::size_t i = 0; i < issues.size(); ++i) {
    const auto& it = issues[i];
    if (i > 0) ss << ",";
    ss << "\n"
       << indent << "  {\"code\": \"" << EscapeJsonString(it.code) << R"(", "severity": ")"
       << EscapeJsonString(it.severity) << R"(", "entity": )" << it.entity.GetId() << R"(, "label": ")"
       << EscapeJsonString(it.label) << R"(", "message": ")" << EscapeJsonString(it.message)
       << R"(", "rect": {"left": )" << it.rect.left << R"(, "top": )" << it.rect.top << R"(, "right": )"
       << it.rect.right << R"(, "bottom": )" << it.rect.bottom << "}}";
  }
  if (issues.empty()) {
    ss << "]";
  } else {
    ss << "\n" << indent << "]";
  }
}

}  // namespace octarine::ui_preview_detail

using octarine::ui_preview_detail::AppendCallbackCommentHeader;
using octarine::ui_preview_detail::AppendJsonIssuesArray;
using octarine::ui_preview_detail::AppendJsonStringList;
using octarine::ui_preview_detail::ApplyMockTableFields;
using octarine::ui_preview_detail::AreSameDiskPath;
using octarine::ui_preview_detail::BuildDefaultScalarValue;
using octarine::ui_preview_detail::BuildSeededArrayForPattern;
using octarine::ui_preview_detail::EscapeJsonString;
using octarine::ui_preview_detail::GetPinnedLuaRegistryObject;
using octarine::ui_preview_detail::HasAnyControllerFunction;
using octarine::ui_preview_detail::IsObservableInternalKey;
using octarine::ui_preview_detail::LoadSandboxedMockScript;
using octarine::ui_preview_detail::MockSeedVariant;
using octarine::ui_preview_detail::MockSourceModeName;
using octarine::ui_preview_detail::PinLuaRegistryObject;
using octarine::ui_preview_detail::ReadFileOrAssetText;
using octarine::ui_preview_detail::RecordMockMtime;
using octarine::ui_preview_detail::ResolveEffectivePresetTable;
using octarine::ui_preview_detail::ResolveMockDiskPath;
using octarine::ui_preview_detail::SeedFieldsIntoTableWithVariant;
using octarine::ui_preview_detail::TryGetActiveViewContext;
using octarine::ui_preview_detail::TryWriteTextFile;
using octarine::ui_preview_detail::UnwrapObservableDataDeep;
using octarine::ui_preview_detail::UnwrapObservableShallow;

namespace {
namespace fs = std::filesystem;

constexpr std::size_t kMaxCallbackEvents = 128;
constexpr int kMaxPreviewCanvasLayer = 60000;
constexpr int kLayerMargin = 100;
constexpr int kMinBackdropHeadroom = 10;

std::string MakeDisplayPath(const std::string& fullPath, const std::string& basePath) {
  if (fullPath.empty()) return {};
  if (basePath.empty()) return fullPath;
  std::error_code ec;
  const fs::path rel = fs::relative(fs::path(fullPath), fs::path(basePath), ec);
  if (!ec && !rel.empty() && rel.native().rfind("..", 0) != 0) {
    return rel.generic_string();
  }
  return fullPath;
}

std::string DescribeUIEntity(const Registry& registry, const Entity entity) {
  std::string tag = "<Node>";
  if (registry.HasComponent<UICanvasComponent>(entity)) {
    tag = "<Canvas>";
  } else if (registry.HasComponent<UIScrollComponent>(entity)) {
    tag = "<ScrollView>";
  } else if (registry.HasComponent<UIButtonComponent>(entity)) {
    tag = "<Button>";
  } else if (registry.HasComponent<TextLabelComponent>(entity)) {
    tag = "<Label>";
  } else if (registry.HasComponent<SpriteComponent>(entity)) {
    tag = "<Image>";
  } else if (registry.HasComponent<UIFlexComponent>(entity)) {
    tag = "<Container>";
  }
  if (registry.HasComponent<NameComponent>(entity)) {
    const std::string& name = registry.GetComponent<NameComponent>(entity).name;
    if (!name.empty()) {
      return tag + " #" + name + " (id=" + std::to_string(entity.GetId()) + ")";
    }
  }
  return tag + " (id=" + std::to_string(entity.GetId()) + ")";
}

bool HasClipAncestor(const Registry& registry, const Entity entity) {
  std::optional<Entity> cur = registry.GetParent(entity);
  while (cur.has_value() && registry.IsAlive(*cur)) {
    if (registry.HasComponent<UIClipComponent>(*cur) || registry.HasComponent<UIScrollComponent>(*cur)) {
      return true;
    }
    cur = registry.GetParent(*cur);
  }
  return false;
}

bool IsEntityVisualOrInteractive(const Registry& registry, const Entity entity) {
  if (registry.HasComponent<UIButtonComponent>(entity) || registry.HasComponent<UIScrollComponent>(entity) ||
      registry.HasComponent<SpriteComponent>(entity)) {
    return true;
  }
  if (registry.HasComponent<TextLabelComponent>(entity) &&
      !registry.GetComponent<TextLabelComponent>(entity).text.empty()) {
    return true;
  }
  if (registry.HasComponent<SquarePrimitiveComponent>(entity)) {
    const auto& sq = registry.GetComponent<SquarePrimitiveComponent>(entity);
    return sq.color.a > 0 || sq.borderThickness > 0.0f || sq.trackColor.a > 0;
  }
  return false;
}

void CheckEntityZeroSize(const Registry& registry, const Entity entity, const UIRectComponent& rect,
                         std::vector<UILayoutIssue>& out) {
  if ((rect.Width() > 0.0f && rect.Height() > 0.0f) || !IsEntityVisualOrInteractive(registry, entity)) {
    return;
  }
  const std::string label = DescribeUIEntity(registry, entity);
  out.push_back({
      .code = "zero_size_visible",
      .severity = "warning",
      .entity = entity,
      .label = label,
      .message = label + " is visible/interactive but resolved to zero size (" +
                 std::to_string(static_cast<int>(rect.Width())) + "x" +
                 std::to_string(static_cast<int>(rect.Height())) + "px).",
      .rect = rect,
  });
}

void CheckEntityViewportOverflow(const Registry& registry, const Entity entity, const UIRectComponent& rect,
                                 const float viewW, const float viewH, std::vector<UILayoutIssue>& out) {
  if (viewW <= 0.0f || viewH <= 0.0f || rect.clipId != 0 || HasClipAncestor(registry, entity)) {
    return;
  }
  static constexpr float kTolerance = 1.0f;
  const bool overflows = (rect.left < -kTolerance) || (rect.top < -kTolerance) || (rect.right > viewW + kTolerance) ||
                         (rect.bottom > viewH + kTolerance);
  if (!overflows) return;

  const std::string label = DescribeUIEntity(registry, entity);
  std::ostringstream msg;
  msg << label << " bounds [" << static_cast<int>(rect.left) << ", " << static_cast<int>(rect.top) << ", "
      << static_cast<int>(rect.right) << ", " << static_cast<int>(rect.bottom) << "] exceed viewport ("
      << static_cast<int>(viewW) << "x" << static_cast<int>(viewH) << ").";
  out.push_back({
      .code = "viewport_overflow",
      .severity = "warning",
      .entity = entity,
      .label = label,
      .message = msg.str(),
      .rect = rect,
  });
}

void CheckEntityTextOverflow(Registry& registry, const Entity entity, const UIRectComponent& rect, const float uiScale,
                             std::vector<UILayoutIssue>& out) {
  if (rect.Width() <= 0.0f || rect.Height() <= 0.0f || !registry.HasComponent<TextLabelComponent>(entity)) {
    return;
  }
  const auto& lbl = registry.GetComponent<TextLabelComponent>(entity);
  if (lbl.text.empty()) return;

  const int wrapOverride =
      (lbl.wrapToRect && lbl.wrapWidth <= 0) ? std::max(1, static_cast<int>(std::floor(rect.Width()))) : 0;
  const glm::vec2 measured = octarine::ui::MeasureTextLabel(&registry, lbl, wrapOverride, uiScale);
  static constexpr float kTolerance = 2.0f;
  if (measured.x <= rect.Width() + kTolerance && measured.y <= rect.Height() + kTolerance) {
    return;
  }

  const std::string label = DescribeUIEntity(registry, entity);
  std::ostringstream msg;
  msg << label << " text (" << static_cast<int>(std::ceil(measured.x)) << "x" << static_cast<int>(std::ceil(measured.y))
      << "px) exceeds rect (" << static_cast<int>(rect.Width()) << "x" << static_cast<int>(rect.Height()) << "px).";
  out.push_back({
      .code = "text_overflow",
      .severity = "warning",
      .entity = entity,
      .label = label,
      .message = msg.str(),
      .rect = rect,
  });
}

void CheckEntityFontLegibility(const Registry& registry, const Entity entity, const UIRectComponent& rect,
                               const float uiScale, std::vector<UILayoutIssue>& out) {
  if (!registry.HasComponent<TextLabelComponent>(entity)) return;
  const auto& lbl = registry.GetComponent<TextLabelComponent>(entity);
  if (lbl.text.empty()) return;

  float authoredPx = octarine::ui::ParseFontPixelSize(lbl.fontId);
  if (const auto* am = registry.TryGet<AssetManager>()) {
    if (TTF_Font* font = am->GetFont(lbl.fontId)) {
      const float ttfSz = TTF_GetFontSize(font);
      if (ttfSz > 0.0f) authoredPx = ttfSz;
    }
  }
  const float effectivePx = authoredPx * uiScale;
  static constexpr float kMinReadableFontPx = 10.0f;
  if (effectivePx >= kMinReadableFontPx) return;

  const std::string label = DescribeUIEntity(registry, entity);
  std::ostringstream msg;
  msg.setf(std::ios::fixed);
  msg.precision(1);
  msg << label << " effective text height " << effectivePx << "px (" << authoredPx << "px @ ";
  msg.precision(2);
  msg << uiScale << "x scale) is below 10.0px legibility threshold.";
  out.push_back({
      .code = "font_too_small",
      .severity = "warning",
      .entity = entity,
      .label = label,
      .message = msg.str(),
      .rect = rect,
  });
}

void CheckEntityFocusFeedback(const Registry& registry, const Entity entity, const UIRectComponent& rect,
                              std::vector<UILayoutIssue>& out) {
  if (!registry.HasComponent<UIFocusComponent>(entity)) return;
  const auto& foc = registry.GetComponent<UIFocusComponent>(entity);
  if (!foc.isFocusable || foc.isDisabled || foc.isInert) return;

  bool hasFocusStyle = false;
  if (registry.HasComponent<UIStyleStateComponent>(entity)) {
    const auto& fp = registry.GetComponent<UIStyleStateComponent>(entity).focus;
    hasFocusStyle = fp.HasBg() || fp.HasBorder() || fp.HasBorderWidth() || fp.HasText() || fp.HasOpacity();
  }
  if (hasFocusStyle) return;

  const std::string label = DescribeUIEntity(registry, entity);
  out.push_back({
      .code = "missing_focus_feedback",
      .severity = "warning",
      .entity = entity,
      .label = label,
      .message = label + " is focusable but defines no :focus style override.",
      .rect = rect,
  });
}

bool RectsOverlapSignificant(const UIRectComponent& a, const UIRectComponent& b) {
  const float interW = std::min(a.right, b.right) - std::max(a.left, b.left);
  const float interH = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
  static constexpr float kMinOverlapPx = 1.0f;
  return interW > kMinOverlapPx && interH > kMinOverlapPx;
}

struct FlowChild {
  Entity entity{};
  UIRectComponent rect{};
};

std::vector<FlowChild> CollectFlowChildren(const Registry& registry, const Entity parent) {
  std::vector<FlowChild> children;
  registry.ForEachChild(parent, [&](const Entity child) {
    if (!registry.IsAlive(child) || !registry.HasComponent<UIRectComponent>(child) ||
        registry.HasComponent<UIAnchorComponent>(child)) {
      return;
    }
    const auto& r = registry.GetComponent<UIRectComponent>(child);
    if (r.visible && r.Width() > 0.0f && r.Height() > 0.0f) {
      children.push_back({child, r});
    }
  });
  return children;
}

void CheckParentSiblingOverlaps(const Registry& registry, const Entity parent, std::vector<UILayoutIssue>& out) {
  if (!registry.HasComponent<UIFlexComponent>(parent)) return;
  const std::vector<FlowChild> children = CollectFlowChildren(registry, parent);

  for (std::size_t i = 0; i < children.size(); ++i) {
    for (std::size_t j = i + 1; j < children.size(); ++j) {
      if (children[i].rect.layer != children[j].rect.layer) continue;
      if (!RectsOverlapSignificant(children[i].rect, children[j].rect)) continue;
      const std::string labelA = DescribeUIEntity(registry, children[i].entity);
      const std::string labelB = DescribeUIEntity(registry, children[j].entity);
      out.push_back({
          .code = "sibling_overlap",
          .severity = "warning",
          .entity = children[j].entity,
          .label = labelB,
          .message = labelA + " and " + labelB + " overlap on the same layer inside flex container.",
          .rect = children[j].rect,
      });
    }
  }
}

void AuditSubtreeIterative(Registry& registry, const Entity root, const float viewW, const float viewH,
                           const float uiScale, std::vector<UILayoutIssue>& out) {
  if (!registry.IsAlive(root)) return;
  const bool rootFixed =
      !registry.HasComponent<UICanvasComponent>(root) || registry.GetComponent<UICanvasComponent>(root).isFixed;

  std::vector<Entity> stack{root};
  while (!stack.empty()) {
    const Entity cur = stack.back();
    stack.pop_back();
    if (!registry.IsAlive(cur)) continue;

    if (registry.HasComponent<UIRectComponent>(cur)) {
      const auto& rect = registry.GetComponent<UIRectComponent>(cur);
      if (!rect.visible) continue;  // Hidden container subtrees do not lay out children or draw.
      CheckEntityZeroSize(registry, cur, rect, out);
      if (rootFixed) {
        CheckEntityViewportOverflow(registry, cur, rect, viewW, viewH, out);
      }
      CheckEntityTextOverflow(registry, cur, rect, uiScale, out);
      CheckEntityFontLegibility(registry, cur, rect, uiScale, out);
      CheckEntityFocusFeedback(registry, cur, rect, out);
      CheckParentSiblingOverlaps(registry, cur, out);
    }

    registry.ForEachChild(cur, [&stack](const Entity child) { stack.push_back(child); });
  }
}

std::string MatchCatalogEntryForPreview(const std::vector<PreviewCatalogEntry>& views, const std::string& input) {
  for (const auto& entry : views) {
    if (entry.id == input || entry.fullPath == input || entry.displayPath == input) {
      return entry.fullPath.empty() ? entry.id : entry.fullPath;
    }
  }
  return input;
}

Entity ReadRootEntityField(const sol::table& viewTable, const Entity fallback = {}) {
  if (!viewTable.valid()) return fallback;
  const sol::object obj = viewTable.get<sol::object>("root");
  if (obj.valid() && obj.is<Entity>()) {
    return obj.as<Entity>();
  }
  return fallback;
}

void PinPreviewTableInLuaRegistry(lua_State* L, const sol::table& viewTable) {
  PinLuaRegistryObject(L, "__octarine_preview_pin", viewTable);
}

std::string FormatFallbackArg(const sol::object& arg) {
  if (arg.is<std::string>()) return "\"" + arg.as<std::string>() + "\"";
  if (arg.is<bool>()) return arg.as<bool>() ? "true" : "false";
  if (arg.is<double>()) {
    std::ostringstream oss;
    oss << arg.as<double>();
    return oss.str();
  }
  return {};
}

sol::object LookupUserControllerMember(const sol::table& backing, const std::string& key) {
  if (!backing.valid()) return sol::lua_nil;
  const sol::object existing = backing.get<sol::object>(key);
  return (existing.valid() && existing != sol::lua_nil) ? existing : sol::lua_nil;
}

struct PathSeg {
  std::string key{};
  bool isIndex{false};
  int indexVal{0};
};

bool TryParsePositiveInt(std::string_view sv, int& out) {
  if (sv.empty()) return false;
  int v = 0;
  for (const char c : sv) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) return false;
    v = v * 10 + (c - '0');
  }
  out = v;
  return v > 0;
}

PathSeg MakePathSeg(std::string_view seg) {
  int idx = 0;
  if (TryParsePositiveInt(seg, idx)) {
    return {std::string(seg), true, idx};
  }
  return {std::string(seg), false, 0};
}

std::vector<PathSeg> ParsePathSegments(std::string_view path) {
  std::vector<PathSeg> out;
  std::size_t i = 0;
  while (i < path.size()) {
    if (path[i] == '.') {
      ++i;
      continue;
    }
    if (path[i] == '[') {
      const std::size_t close = path.find(']', i + 1);
      if (close == std::string_view::npos) break;
      int idx = 0;
      const std::string_view inner = path.substr(i + 1, close - i - 1);
      if (TryParsePositiveInt(inner, idx)) out.push_back({std::string(inner), true, idx});
      i = close + 1;
      continue;
    }
    std::size_t end = i;
    while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
    out.push_back(MakePathSeg(path.substr(i, end - i)));
    i = end;
  }
  return out;
}

sol::object GetSegmentValue(const sol::table& tbl, const PathSeg& seg) {
  if (!tbl.valid()) return sol::lua_nil;
  return seg.isIndex ? tbl.get<sol::object>(seg.indexVal) : tbl.get<sol::object>(seg.key);
}

void SetSegmentValue(sol::table& tbl, const PathSeg& seg, const sol::object& val) {
  if (!tbl.valid()) return;
  if (seg.isIndex) {
    tbl[seg.indexVal] = val;
  } else {
    tbl[seg.key] = val;
  }
}

sol::object GetUnwrappedPathValue(const sol::table& root, std::string_view path) {
  if (!root.valid() || path.empty()) return sol::lua_nil;
  const std::vector<PathSeg> segs = ParsePathSegments(path);
  sol::object cur = root;
  for (const auto& seg : segs) {
    if (!cur.is<sol::table>()) return sol::lua_nil;
    const sol::table unwrapped = UnwrapObservableShallow(cur.as<sol::table>());
    cur = GetSegmentValue(unwrapped, seg);
    if (!cur.valid() || cur == sol::lua_nil) return sol::lua_nil;
  }
  return cur;
}

bool SetLivePathValue(sol::state& lua, sol::table& root, std::string_view path, const sol::object& val) {
  const std::vector<PathSeg> segs = ParsePathSegments(path);
  if (!root.valid() || segs.empty()) return false;
  sol::table cur = root;
  for (std::size_t i = 0; i + 1 < segs.size(); ++i) {
    sol::object nextObj = GetSegmentValue(cur, segs[i]);
    if (!nextObj.is<sol::table>()) {
      sol::table created = lua.create_table();
      SetSegmentValue(cur, segs[i], created);
      nextObj = GetSegmentValue(cur, segs[i]);
      if (!nextObj.is<sol::table>()) return false;
    }
    cur = nextObj.as<sol::table>();
  }
  SetSegmentValue(cur, segs.back(), val);
  return true;
}

std::string FormatTitleWords(std::string_view tail) {
  std::string out;
  bool capNext = true;
  int words = 0;
  for (const char c : tail) {
    if (c == '_' || c == '-') {
      if (out.empty()) continue;
      if (++words >= 2 || out.size() >= 12) break;
      out.push_back(' ');
      capNext = true;
      continue;
    }
    const auto uc = static_cast<unsigned char>(c);
    out.push_back(static_cast<char>(capNext ? std::toupper(uc) : std::tolower(uc)));
    capNext = false;
    if (out.size() >= 16) break;
  }
  return out;
}

std::string MakeShortTitleCaseLabel(std::string_view path, const int arrayIdx = 0) {
  const std::size_t dot = path.rfind('.');
  const std::string_view tail = (dot == std::string_view::npos) ? path : path.substr(dot + 1);
  std::string out = FormatTitleWords(tail);
  if (out.empty()) out = "Value";
  if (arrayIdx > 0) {
    out += " " + std::to_string(arrayIdx);
  }
  return out;
}

std::string FindFirstCatalogTextureId(const Registry* reg) {
  if (reg == nullptr) return "";
  const auto* am = reg->TryGet<AssetManager>();
  if (am == nullptr) return "";
  std::string best;
  for (const auto& [id, entry] : am->GetCatalog().Entries()) {
    if (entry.type == AssetType::Texture && (best.empty() || id < best)) {
      best = id;
    }
  }
  return best;
}

std::string MakeOverflowLabel(std::string_view path, const int arrayIdx = 0) {
  return "Overflow Stress " + MakeShortTitleCaseLabel(path, arrayIdx) +
         " - Extended Localization String Verification Sample";
}

double ComputeDefaultSliderValue(const ContractField& f) {
  const double minV = static_cast<double>(f.minVal);
  const double maxV = static_cast<double>(f.maxVal);
  double mid = 0.5 * (minV + maxV);
  if (f.step > 0.0f) {
    const double stepD = static_cast<double>(f.step);
    mid = minV + std::round((mid - minV) / stepD) * stepD;
    mid = std::clamp(mid, std::min(minV, maxV), std::max(minV, maxV));
  }
  return mid;
}

sol::object BuildDefaultScalarEmpty(sol::state& lua, const Registry* reg, const ContractField& f) {
  if (f.sampleHint == "color") return sol::make_object(lua, "#4A5568");
  if (f.sampleHint == "sprite") return sol::make_object(lua, FindFirstCatalogTextureId(reg));
  if (f.sampleHint == "radio") {
    return sol::make_object(lua, f.radioOptions.empty() ? std::string() : f.radioOptions.front());
  }
  if (f.inferredType == ContractFieldType::Number) {
    if (f.sampleHint == "divided_den") return sol::make_object(lua, 100.0);
    if (f.sampleHint == "slider" || f.hasSliderRange) return sol::make_object(lua, static_cast<double>(f.minVal));
    return sol::make_object(lua, 0.0);
  }
  if (f.inferredType == ContractFieldType::Boolean) return sol::make_object(lua, false);
  return sol::make_object(lua, "");
}

sol::object BuildDefaultScalarOverflow(sol::state& lua, const Registry* reg, const ContractField& f,
                                       const int arrayIdx) {
  if (f.sampleHint == "color") return sol::make_object(lua, "#FF00FF");
  if (f.sampleHint == "sprite") return sol::make_object(lua, FindFirstCatalogTextureId(reg));
  if (f.sampleHint == "radio") {
    return sol::make_object(lua, f.radioOptions.empty() ? std::string("Overflow Option") : f.radioOptions.back());
  }
  if (f.inferredType == ContractFieldType::Number) {
    if (f.sampleHint == "slider" || f.hasSliderRange) return sol::make_object(lua, static_cast<double>(f.maxVal));
    if (f.sampleHint == "progress") return sol::make_object(lua, 1.0);
    if (f.sampleHint == "divided_num" || f.sampleHint == "divided_den") return sol::make_object(lua, 99999.0);
    return sol::make_object(lua, 999999.0);
  }
  if (f.inferredType == ContractFieldType::Boolean) return sol::make_object(lua, f.sampleHint != "disabled");
  return sol::make_object(lua, MakeOverflowLabel(f.path, arrayIdx));
}

sol::object BuildDefaultScalarCompact(sol::state& lua, const Registry* reg, const ContractField& f,
                                      const int arrayIdx) {
  if (f.sampleHint == "color") return sol::make_object(lua, "#4A5568");
  if (f.sampleHint == "sprite") return sol::make_object(lua, FindFirstCatalogTextureId(reg));
  if (f.sampleHint == "radio") {
    const std::string opt = f.radioOptions.empty() ? "Option 1" : f.radioOptions.front();
    return sol::make_object(lua, opt);
  }
  if (f.inferredType == ContractFieldType::Number) {
    if (f.sampleHint == "slider" || f.hasSliderRange) {
      return sol::make_object(lua, ComputeDefaultSliderValue(f));
    }
    if (f.sampleHint == "divided_den") return sol::make_object(lua, 100.0);
    if (f.sampleHint == "divided_num") return sol::make_object(lua, 75.0);
    if (f.sampleHint == "progress") return sol::make_object(lua, 0.65);
    return sol::make_object(lua, 10.0);
  }
  if (f.inferredType == ContractFieldType::Boolean) {
    return sol::make_object(lua, f.sampleHint != "disabled");
  }
  return sol::make_object(lua, MakeShortTitleCaseLabel(f.path, arrayIdx));
}

int ArrayCountForVariant(const MockSeedVariant variant, const int depth) {
  if (variant == MockSeedVariant::Empty) return 0;
  if (variant == MockSeedVariant::Overflow) return depth == 0 ? 10 : 4;
  return 3;
}

sol::object BuildDefaultArrayElementWithVariant(sol::state& lua, const Registry* reg, const OxmlContract& contract,
                                                const std::string& arrayPath, int itemIdx, int depth,
                                                MockSeedVariant variant);

// Recursively seeds array item tables including nested repeater sub-arrays (bounded by depth <= 4).
// NOLINTNEXTLINE(misc-no-recursion)
sol::table BuildSeededArrayWithVariant(sol::state& lua, const Registry* reg, const OxmlContract& contract,
                                       const std::string& arrayPath, const int count, const int depth,
                                       const MockSeedVariant variant) {
  sol::table arr = lua.create_table();
  for (int idx = 1; idx <= count; ++idx) {
    arr[idx] = BuildDefaultArrayElementWithVariant(lua, reg, contract, arrayPath, idx, depth, variant);
  }
  return arr;
}

// Builds default table item from contract subfields; nested repeaters bounded by depth <= 4.
// NOLINTNEXTLINE(misc-no-recursion)
sol::table BuildArrayItemTableWithVariant(sol::state& lua, const Registry* reg, const OxmlContract& contract,
                                          const std::string& arrayPrefix, const int itemIdx, const int depth,
                                          const MockSeedVariant variant) {
  const std::string needle = arrayPrefix + "[].";
  sol::table itemTbl = lua.create_table();
  for (const auto& f : contract.fields) {
    if (!f.path.starts_with(needle)) continue;
    const std::string sub = f.path.substr(needle.size());
    if (sub.find("[]") != std::string::npos) continue;
    if (f.inferredType == ContractFieldType::Array && depth < 4) {
      const int subCount = ArrayCountForVariant(variant, depth + 1);
      SetLivePathValue(
          lua, itemTbl, sub,
          BuildSeededArrayWithVariant(lua, reg, contract, arrayPrefix + "[]." + sub, subCount, depth + 1, variant));
    } else if (f.inferredType != ContractFieldType::Table) {
      SetLivePathValue(lua, itemTbl, sub, BuildDefaultScalarValue(lua, reg, f, itemIdx, variant));
    }
  }
  return itemTbl;
}

// Mutually recursive with BuildSeededArrayWithVariant; bounded by depth <= 4.
// NOLINTNEXTLINE(misc-no-recursion)
sol::object BuildDefaultArrayElementWithVariant(sol::state& lua, const Registry* reg, const OxmlContract& contract,
                                                const std::string& arrayPath, const int itemIdx, const int depth,
                                                const MockSeedVariant variant) {
  const std::string needle = arrayPath + "[].";
  for (const auto& f : contract.fields) {
    if (f.path.starts_with(needle)) {
      return BuildArrayItemTableWithVariant(lua, reg, contract, arrayPath, itemIdx, depth, variant);
    }
  }
  const std::string barePath = arrayPath + "[]";
  for (const auto& f : contract.fields) {
    if (f.path == barePath) {
      return BuildDefaultScalarValue(lua, reg, f, itemIdx, variant);
    }
  }
  return (variant == MockSeedVariant::Overflow) ? sol::make_object(lua, MakeOverflowLabel(arrayPath, itemIdx))
                                                : sol::make_object(lua, MakeShortTitleCaseLabel(arrayPath, itemIdx));
}

sol::object BuildDefaultArrayElement(sol::state& lua, const Registry* reg, const OxmlContract& contract,
                                     const std::string& arrayPath, const int itemIdx, const int depth = 0) {
  return BuildDefaultArrayElementWithVariant(lua, reg, contract, arrayPath, itemIdx, depth, MockSeedVariant::Compact);
}

struct NestedSeedWorkItem {
  sol::table tbl{};
  std::string remPattern{};
  std::string arrPatternPrefix{};
  int lastIdx{1};
  int depth{0};
};

void ApplyNestedSubfieldLeaf(sol::state& lua, const Registry* reg, const OxmlContract& contract, const ContractField& f,
                             const MockSeedVariant variant, const bool overwriteExisting, NestedSeedWorkItem& cur) {
  if (!overwriteExisting && GetUnwrappedPathValue(cur.tbl, cur.remPattern) != sol::lua_nil) return;
  if (f.inferredType == ContractFieldType::Array && cur.depth < 4) {
    const std::string fullPat =
        cur.arrPatternPrefix.empty() ? cur.remPattern : (cur.arrPatternPrefix + "[]." + cur.remPattern);
    SetLivePathValue(lua, cur.tbl, cur.remPattern,
                     BuildSeededArrayWithVariant(lua, reg, contract, fullPat,
                                                 ArrayCountForVariant(variant, cur.depth + 1), cur.depth + 1, variant));
    return;
  }
  if (f.inferredType != ContractFieldType::Table) {
    SetLivePathValue(lua, cur.tbl, cur.remPattern, BuildDefaultScalarValue(lua, reg, f, cur.lastIdx, variant));
  }
}

void EnqueueNestedArrayChildren(const NestedSeedWorkItem& cur, const std::size_t splitPos,
                                std::vector<NestedSeedWorkItem>& work) {
  const std::string arrSeg = cur.remPattern.substr(0, splitPos);
  const std::string subRem = cur.remPattern.substr(splitPos + 3);
  const std::string nextPat = cur.arrPatternPrefix.empty() ? arrSeg : (cur.arrPatternPrefix + "[]." + arrSeg);
  const sol::object arrObj = GetUnwrappedPathValue(cur.tbl, arrSeg);
  if (!arrObj.is<sol::table>()) return;
  const sol::table unwrapped = UnwrapObservableShallow(arrObj.as<sol::table>());
  for (std::size_t idx = 1; idx <= unwrapped.size(); ++idx) {
    const sol::object elemObj = unwrapped.get<sol::object>(idx);
    if (elemObj.is<sol::table>()) {
      work.push_back({elemObj.as<sol::table>(), subRem, nextPat, static_cast<int>(idx), cur.depth + 1});
    }
  }
}

void SeedNestedArraySubfield(sol::state& lua, const Registry* reg, const OxmlContract& contract, sol::table& rootCtx,
                             const ContractField& f, const MockSeedVariant variant, const bool overwriteExisting) {
  if (f.path.find("[].") == std::string::npos) return;
  std::vector<NestedSeedWorkItem> work{{rootCtx, f.path, "", 1, 0}};
  while (!work.empty()) {
    NestedSeedWorkItem cur = std::move(work.back());
    work.pop_back();
    const std::size_t pos = cur.remPattern.find("[].");
    if (pos == std::string::npos) {
      ApplyNestedSubfieldLeaf(lua, reg, contract, f, variant, overwriteExisting, cur);
    } else {
      EnqueueNestedArrayChildren(cur, pos, work);
    }
  }
}

void SeedAutoFieldsIntoTable(sol::state& lua, const Registry* reg, sol::table& targetCtx,
                             const OxmlContract& contract) {
  SeedFieldsIntoTableWithVariant(lua, reg, targetCtx, contract, MockSeedVariant::Compact, /*overwriteExisting=*/false);
}

std::string ComputeSidecarPathForView(const UIRuntime* uiRuntime, const std::string& resolvedSource,
                                      const bool isInline) {
  if (isInline || resolvedSource.empty() || uiRuntime == nullptr) return {};
  std::string diskPath;
  static_cast<void>(uiRuntime->ReadTextAsset(resolvedSource, &diskPath));
  const std::string& candidate = !diskPath.empty() ? diskPath : resolvedSource;
  if (candidate.empty() || candidate.front() == '<') return {};
  std::error_code ec;
  if (!fs::exists(candidate, ec) && diskPath.empty()) return {};
  fs::path p(candidate);
  p.replace_extension(".preview.lua");
  return p.generic_string();
}

sol::environment CreateSandboxedMockEnv(sol::state& lua) {
  sol::environment env(lua, sol::create);
  static constexpr const char* kSafeGlobals[] = {
      "math", "string", "table", "pairs", "ipairs", "next", "tonumber", "tostring", "type", "unpack", "select",
  };
  for (const char* name : kSafeGlobals) {
    const sol::object obj = lua.get<sol::object>(name);
    if (obj.valid() && obj != sol::lua_nil) {
      env[name] = obj;
    }
  }
  return env;
}

void MergeOptionalParams(sol::state& lua, const sol::table& rootFallback, const sol::table& tbl, sol::table& outOpts) {
  const sol::object rootParams = rootFallback.valid() ? rootFallback["params"] : sol::object(sol::lua_nil);
  const sol::object subParams = tbl["params"];
  if (!rootParams.is<sol::table>() && !subParams.is<sol::table>()) return;
  sol::table merged = lua.create_table();
  if (rootParams.is<sol::table>()) {
    for (const auto& [k, v] : rootParams.as<sol::table>()) merged[k] = v;
  }
  if (subParams.is<sol::table>()) {
    for (const auto& [k, v] : subParams.as<sol::table>()) merged[k] = v;
  }
  outOpts.set("params", merged);
}
}  // namespace

namespace octarine::ui_preview_detail {

std::string ReadFileOrAssetText(UIRuntime* uiRuntime, const std::string& pathOrId, std::string& outErr) {
  if (uiRuntime != nullptr) {
    if (const auto loaded = uiRuntime->ReadTextAsset(pathOrId); loaded.has_value()) {
      return *loaded;
    }
  }
  outErr = "failed to open '" + pathOrId + "'";
  return {};
}

std::string ResolveMockDiskPath(const Registry* registry, const UIRuntime* uiRuntime, const std::string& mockPath) {
  if (mockPath.empty()) return {};
  std::error_code ec;
  if (fs::exists(mockPath, ec)) return mockPath;
  if (registry != nullptr) {
    if (const auto* am = registry->TryGet<AssetManager>()) {
      const fs::path candidate = fs::path(am->GetBasePath()) / mockPath;
      if (fs::exists(candidate, ec)) return candidate.generic_string();
    }
  }
  if (uiRuntime != nullptr) {
    std::string diskPath;
    if (uiRuntime->ReadTextAsset(mockPath, &diskPath).has_value() && !diskPath.empty()) {
      return diskPath;
    }
  }
  return mockPath;
}

void PinLuaRegistryObject(lua_State* L, const char* registryKey, const sol::object& obj) {
  if (L == nullptr) return;
  if (obj.valid() && obj != sol::lua_nil) {
    obj.push(L);
  } else {
    lua_pushnil(L);
  }
  lua_setfield(L, LUA_REGISTRYINDEX, registryKey);
}

sol::object GetPinnedLuaRegistryObject(lua_State* L, const char* registryKey) {
  if (L == nullptr) return sol::lua_nil;
  lua_getfield(L, LUA_REGISTRYINDEX, registryKey);
  sol::object obj(L, -1);
  lua_pop(L, 1);
  return (obj.valid() && obj != sol::lua_nil) ? obj : sol::object(sol::lua_nil);
}

sol::object BuildDefaultScalarValue(sol::state& lua, const Registry* reg, const ContractField& f, const int arrayIdx,
                                    const MockSeedVariant variant) {
  if (variant == MockSeedVariant::Empty) return BuildDefaultScalarEmpty(lua, reg, f);
  if (variant == MockSeedVariant::Overflow) return BuildDefaultScalarOverflow(lua, reg, f, arrayIdx);
  return BuildDefaultScalarCompact(lua, reg, f, arrayIdx);
}

sol::table BuildSeededArrayForPattern(sol::state& lua, const Registry* reg, const OxmlContract& contract,
                                      const std::string& arrayPath, const int count, const int depth,
                                      const MockSeedVariant variant) {
  return BuildSeededArrayWithVariant(lua, reg, contract, arrayPath, count, depth, variant);
}

void SeedFieldsIntoTableWithVariant(sol::state& lua, const Registry* reg, sol::table& targetCtx,
                                    const OxmlContract& contract, const MockSeedVariant variant,
                                    const bool overwriteExisting) {
  const int topArrCount = ArrayCountForVariant(variant, 0);
  for (const auto& f : contract.fields) {
    if (f.path.find("[]") != std::string::npos) continue;
    if (!overwriteExisting && GetUnwrappedPathValue(targetCtx, f.path) != sol::lua_nil) continue;

    if (f.inferredType == ContractFieldType::Array) {
      SetLivePathValue(lua, targetCtx, f.path,
                       BuildSeededArrayWithVariant(lua, reg, contract, f.path, topArrCount, 0, variant));
    } else if (f.inferredType != ContractFieldType::Table) {
      SetLivePathValue(lua, targetCtx, f.path, BuildDefaultScalarValue(lua, reg, f, 0, variant));
    }
  }
  for (const auto& f : contract.fields) {
    if (f.path.find("[].") != std::string::npos) {
      SeedNestedArraySubfield(lua, reg, contract, targetCtx, f, variant, overwriteExisting);
    }
  }
}

std::string PickSortedFirstPresetKey(const sol::table& presets) {
  std::vector<std::string> sortedKeys;
  for (const auto& [k, v] : presets) {
    if (k.is<std::string>() && v.is<sol::table>()) sortedKeys.push_back(k.as<std::string>());
  }
  std::ranges::sort(sortedKeys);
  return sortedKeys.empty() ? std::string() : sortedKeys.front();
}

sol::table ResolveEffectivePresetTable(const sol::table& rootTbl, const std::string& requestedPreset,
                                       std::string& outPresetKey) {
  const sol::object presetsObj = rootTbl.get<sol::object>("presets");
  if (!presetsObj.is<sol::table>()) return rootTbl;
  const sol::table presets = presetsObj.as<sol::table>();
  const sol::object defObj = rootTbl.get<sol::object>("default_preset");
  const std::string defName =
      (defObj.is<std::string>() && !defObj.as<std::string>().empty()) ? defObj.as<std::string>() : "default";
  std::string presetName = requestedPreset.empty() ? defName : requestedPreset;

  sol::object chosen = presets.get<sol::object>(presetName);
  if (!chosen.is<sol::table>() && presetName != "default") {
    presetName = "default";
    chosen = presets.get<sol::object>(presetName);
  }
  if (!chosen.is<sol::table>()) {
    if (const std::string firstKey = PickSortedFirstPresetKey(presets); !firstKey.empty()) {
      presetName = firstKey;
      chosen = presets.get<sol::object>(presetName);
    }
  }
  if (!chosen.is<sol::table>()) return rootTbl;
  outPresetKey = presetName;
  return chosen.as<sol::table>();
}

void ApplyMockTableFields(UIRuntime& uiRuntime, const sol::table& tbl, sol::table& outOpts, sol::table& outRawCtrl,
                          const sol::table& rootFallback) {
  const sol::object ctxField = tbl["context"];
  const sol::object subCtrl = tbl["controller"];
  const sol::object rootCtrl = rootFallback.valid() ? rootFallback["controller"] : sol::object(sol::lua_nil);
  const sol::object ctrlField = subCtrl.is<sol::table>() ? subCtrl : rootCtrl;
  const sol::object paramsField = tbl["params"];
  const sol::object subStyles = tbl["styles"];
  const sol::object rootStyles = rootFallback.valid() ? rootFallback["styles"] : sol::object(sol::lua_nil);
  const sol::object stylesField = subStyles.is<sol::table>() ? subStyles : rootStyles;

  const bool structured = ctxField.is<sol::table>() || ctrlField.is<sol::table>() || paramsField.is<sol::table>() ||
                          stylesField.is<sol::table>() ||
                          (rootFallback.valid() && rootFallback["presets"].is<sol::table>());
  if (!structured) {
    outOpts.set("context", uiRuntime.CreateObservable(tbl));
    return;
  }
  if (ctxField.is<sol::table>()) outOpts.set("context", uiRuntime.CreateObservable(ctxField.as<sol::table>()));
  if (ctrlField.is<sol::table>()) {
    outOpts.set("controller", ctrlField);
    outRawCtrl = ctrlField.as<sol::table>();
  }
  if (uiRuntime.GetLua() != nullptr) {
    MergeOptionalParams(*uiRuntime.GetLua(), rootFallback, tbl, outOpts);
  }
  if (stylesField.is<sol::table>()) outOpts.set("styles", stylesField);
}

bool LoadSandboxedMockScriptText(UIRuntime& uiRuntime, const std::string& scriptText, const std::string& chunkLabel,
                                 const std::string& requestedPreset, sol::table& outOpts, sol::table& outRawCtrl,
                                 sol::table& outRawRoot, std::string& outPresetKey, bool& outHasController,
                                 std::string& outError) {
  sol::state* luaPtr = uiRuntime.GetLua();
  if (luaPtr == nullptr) {
    outError = "Lua state unavailable";
    return false;
  }
  sol::state& lua = *luaPtr;

  sol::load_result chunk = lua.load(scriptText, "@" + chunkLabel);
  if (!chunk.valid()) {
    const sol::error err = chunk;
    outError = err.what();
    return false;
  }

  sol::protected_function fn = chunk.get<sol::protected_function>();
  sol::environment env = CreateSandboxedMockEnv(lua);
  sol::set_environment(env, fn);
  const sol::protected_function_result res = fn();
  if (!res.valid()) {
    const sol::error err = res;
    outError = err.what();
    return false;
  }
  if (res.return_count() == 0 || !res[0].is<sol::table>()) {
    outError = "Mock script '" + chunkLabel + "' must return a table";
    return false;
  }

  outRawRoot = res[0].as<sol::table>();
  outPresetKey.clear();
  const sol::table effective = ResolveEffectivePresetTable(outRawRoot, requestedPreset, outPresetKey);
  ApplyMockTableFields(uiRuntime, effective, outOpts, outRawCtrl, outRawRoot);
  outHasController = HasAnyControllerFunction(outRawRoot, outRawCtrl);
  return true;
}

bool LoadSandboxedMockScript(UIRuntime& uiRuntime, const std::string& resolvedPath, const std::string& requestedPreset,
                             sol::table& outOpts, sol::table& outRawCtrl, sol::table& outRawRoot,
                             std::string& outPresetKey, bool& outHasController, std::string& outError) {
  const std::string scriptText = ReadFileOrAssetText(&uiRuntime, resolvedPath, outError);
  if (scriptText.empty() && !outError.empty()) {
    return false;
  }
  return LoadSandboxedMockScriptText(uiRuntime, scriptText, resolvedPath, requestedPreset, outOpts, outRawCtrl,
                                     outRawRoot, outPresetKey, outHasController, outError);
}

}  // namespace octarine::ui_preview_detail

namespace {

MockFieldKind InferLiveFieldKind(const sol::object& obj) {
  if (obj.is<bool>()) return MockFieldKind::Boolean;
  if (obj.is<double>()) return MockFieldKind::Number;
  if (obj.is<sol::table>()) {
    const sol::table raw = UnwrapObservableShallow(obj.as<sol::table>());
    const bool hasArrayItems = raw.size() > 0;
    if (hasArrayItems) return MockFieldKind::Array;
    bool anyStringKey = false;
    for (const auto& [k, _] : raw) {
      if (k.is<std::string>() && !IsObservableInternalKey(k.as<std::string>())) {
        anyStringKey = true;
        break;
      }
    }
    return anyStringKey ? MockFieldKind::Table : MockFieldKind::Array;
  }
  return MockFieldKind::String;
}

MockFieldKind ContractTypeToMockKind(const ContractFieldType t) {
  switch (t) {
    case ContractFieldType::Boolean:
      return MockFieldKind::Boolean;
    case ContractFieldType::Number:
      return MockFieldKind::Number;
    case ContractFieldType::Array:
      return MockFieldKind::Array;
    case ContractFieldType::Table:
      return MockFieldKind::Table;
    case ContractFieldType::Unknown:
    case ContractFieldType::String:
      return MockFieldKind::String;
  }
  return MockFieldKind::String;
}

void PopulateEntryFromLiveObject(MockFieldEntry& entry, const sol::object& val) {
  entry.isUnset = (!val.valid() || val == sol::lua_nil);
  if (entry.isUnset) return;

  entry.kind = InferLiveFieldKind(val);
  if (entry.kind == MockFieldKind::Boolean) {
    entry.boolValue = val.as<bool>();
  } else if (entry.kind == MockFieldKind::Number) {
    entry.numberValue = val.as<double>();
  } else if (entry.kind == MockFieldKind::String) {
    entry.stringValue = val.is<std::string>() ? val.as<std::string>() : "";
  } else if (entry.kind == MockFieldKind::Array) {
    const sol::table raw = UnwrapObservableShallow(val.as<sol::table>());
    entry.arrayLength = static_cast<int>(raw.size());
  }
}

void CollectSortedLiveKeys(const sol::table& raw, std::vector<std::pair<int, sol::object>>& intKeys,
                           std::vector<std::string>& strKeys) {
  for (const auto& [k, _] : raw) {
    if (k.is<double>()) {
      const double d = k.as<double>();
      if (d >= 1.0 && std::floor(d) == d) {
        intKeys.emplace_back(static_cast<int>(d), k);
      }
    } else if (k.is<std::string>() && !IsObservableInternalKey(k.as<std::string>())) {
      strKeys.push_back(k.as<std::string>());
    }
  }
  std::ranges::sort(intKeys, [](const auto& a, const auto& b) { return a.first < b.first; });
  std::ranges::sort(strKeys);
}

void AppendExtraLiveFields(const sol::table& tbl, const std::string& prefix, std::unordered_set<std::string>& seenPaths,
                           const bool isAutoMode, std::vector<MockFieldEntry>& out, const int depth = 0);

// Mutually recursive with AppendExtraLiveFields; depth-bounded at <= 8.
// NOLINTNEXTLINE(misc-no-recursion)
void ProcessExtraLiveEntry(const sol::object& val, const std::string& fullPath,
                           std::unordered_set<std::string>& seenPaths, const bool isAutoMode,
                           std::vector<MockFieldEntry>& out, const int depth, const bool recurseArray) {
  if (!val.valid() || val == sol::lua_nil || val.is<sol::protected_function>()) return;
  const MockFieldKind k = InferLiveFieldKind(val);
  if (k == MockFieldKind::Table) {
    AppendExtraLiveFields(val.as<sol::table>(), fullPath, seenPaths, isAutoMode, out, depth + 1);
    return;
  }
  if (seenPaths.insert(fullPath).second) {
    MockFieldEntry e;
    e.path = fullPath;
    e.isAutoSeeded = isAutoMode;
    PopulateEntryFromLiveObject(e, val);
    out.push_back(std::move(e));
  }
  if (recurseArray && k == MockFieldKind::Array) {
    AppendExtraLiveFields(val.as<sol::table>(), fullPath, seenPaths, isAutoMode, out, depth + 1);
  }
}

// Walks live Lua tables deterministically to append any extra fields not in active_contract_.
// NOLINTNEXTLINE(misc-no-recursion)
void AppendExtraLiveFields(const sol::table& tbl, const std::string& prefix, std::unordered_set<std::string>& seenPaths,
                           const bool isAutoMode, std::vector<MockFieldEntry>& out, const int depth) {
  if (!tbl.valid() || depth > 8) return;
  const sol::table raw = UnwrapObservableShallow(tbl);
  std::vector<std::pair<int, sol::object>> intKeys;
  std::vector<std::string> strKeys;
  CollectSortedLiveKeys(raw, intKeys, strKeys);

  for (const auto& keyStr : strKeys) {
    const std::string fullPath = prefix.empty() ? keyStr : (prefix + "." + keyStr);
    ProcessExtraLiveEntry(raw.get<sol::object>(keyStr), fullPath, seenPaths, isAutoMode, out, depth, true);
  }
  for (const auto& [idx, kObj] : intKeys) {
    const std::string elemPath = prefix + "[" + std::to_string(idx) + "]";
    ProcessExtraLiveEntry(raw.get<sol::object>(kObj), elemPath, seenPaths, isAutoMode, out, depth, true);
  }
}

sol::object InvokeUserCtrlFn(const sol::protected_function& userFn, const sol::table& backing,
                             const sol::variadic_args& args) {
  std::vector<sol::object> forwardArgs;
  forwardArgs.reserve(args.size());
  for (const auto& arg : args) {
    forwardArgs.push_back(sol::object(arg));
  }
  const sol::protected_function_result res =
      forwardArgs.empty() ? userFn(backing) : userFn(backing, sol::as_args(forwardArgs));
  if (!res.valid()) {
    const sol::error err = res;
    Logger::Error(std::string("[ui-preview] controller error: ") + err.what());
    return sol::lua_nil;
  }
  return res.return_count() > 0 ? res[0] : sol::object(sol::lua_nil);
}

}  // namespace

namespace octarine::ui_preview_detail {
bool TryWriteTextFile(const std::string& path, const std::string& contents, std::string& outError) {
  std::error_code ec;
  const fs::path p(path);
  if (p.has_parent_path()) {
    fs::create_directories(p.parent_path(), ec);
  }
  std::ofstream out(p, std::ios::out | std::ios::trunc | std::ios::binary);
  if (!out.is_open()) {
    outError = "Failed to open '" + path + "' for writing";
    return false;
  }
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  out.close();
  if (!out) {
    outError = "Failed writing to '" + path + "'";
    return false;
  }
  return true;
}
}  // namespace octarine::ui_preview_detail

struct UIPreviewer::PausedRenderSystems {
  Registry* boundRegistry{nullptr};
  TransformSystem transform{};
  UILayoutSystem uiLayout{};
  RenderUIPrimitiveSystem renderPrimitives{};
  RenderUISpriteSystem renderSprites{};
  RenderTextSystem renderText{};
  std::unique_ptr<ComponentQuery<UIRectComponent, SpriteComponent>> spriteQuery{};
  std::unique_ptr<ComponentQuery<TextLabelComponent>> textQuery{};
  std::unique_ptr<ComponentQuery<UICanvasComponent>> canvasQuery{};

  void EnsureQueries(Registry& reg) {
    if (boundRegistry == &reg) return;
    boundRegistry = &reg;
    spriteQuery = reg.CreateQuery<UIRectComponent, SpriteComponent>();
    textQuery = reg.CreateQuery<TextLabelComponent>();
    canvasQuery = reg.CreateQuery<UICanvasComponent>();
  }

  void Run(Registry& reg) {
    EnsureQueries(reg);
    ContextFacade bulkCtx(&reg, 0.0F, nullptr, &Internal::BulkGetComponent);
    transform(bulkCtx);
    uiLayout(bulkCtx);
    renderPrimitives(bulkCtx);

    if (reg.TryGet<AssetManager>() != nullptr && reg.TryGet<RenderQueue>() != nullptr &&
        reg.TryGet<SpriteRenderCache>() != nullptr) {
      spriteQuery->Update();
      renderSprites.Prepare(&reg);
      spriteQuery->ForEach(renderSprites);
    }

    if (reg.TryGet<AssetManager>() != nullptr && reg.TryGet<EngineContext>() != nullptr &&
        reg.TryGet<RenderQueue>() != nullptr && reg.TryGet<CameraComponent>() != nullptr &&
        reg.TryGet<GameConfig>() != nullptr) {
      textQuery->Update();
      renderText.Prepare(&reg);
      textQuery->ForEach(renderText);
    }
  }
};

UIPreviewer::UIPreviewer(Registry* registry)
    : registry_(registry),
      paused_systems_(std::make_unique<PausedRenderSystems>()),
      self_handle_(std::make_shared<UIPreviewer*>(this)) {}

UIPreviewer::~UIPreviewer() {
  if (self_handle_) *self_handle_ = nullptr;
}

UIPreviewer::UIPreviewer(UIPreviewer&& other) noexcept
    : registry_(other.registry_),
      paused_systems_(std::move(other.paused_systems_)),
      self_handle_(std::move(other.self_handle_)),
      catalog_views_(std::move(other.catalog_views_)),
      catalog_styles_(std::move(other.catalog_styles_)),
      active_root_(other.active_root_),
      active_view_id_(other.active_view_id_),
      orig_canvas_base_layer_(other.orig_canvas_base_layer_),
      captured_orig_base_layer_(other.captured_orig_base_layer_),
      backdrop_layer_(other.backdrop_layer_),
      active_source_(std::move(other.active_source_)),
      active_mock_path_(std::move(other.active_mock_path_)),
      inline_xml_(std::move(other.inline_xml_)),
      inline_oss_(std::move(other.inline_oss_)),
      is_inline_(other.is_inline_),
      backdrop_mode_(other.backdrop_mode_),
      isolated_(other.isolated_),
      prev_paused_(other.prev_paused_),
      has_saved_pause_state_(other.has_saved_pause_state_),
      muted_view_states_(std::move(other.muted_view_states_)),
      callback_events_(std::move(other.callback_events_)),
      pending_capture_path_(std::move(other.pending_capture_path_)),
      last_capture_status_(std::move(other.last_capture_status_)),
      mock_source_mode_(other.mock_source_mode_),
      target_sidecar_path_(std::move(other.target_sidecar_path_)),
      last_mock_error_(std::move(other.last_mock_error_)),
      mock_modified_(other.mock_modified_),
      last_mock_write_time_(other.last_mock_write_time_),
      has_mock_write_time_(other.has_mock_write_time_),
      mock_hot_reload_accum_(other.mock_hot_reload_accum_),
      last_seen_reload_count_(other.last_seen_reload_count_),
      callback_seq_(other.callback_seq_),
      active_contract_(std::move(other.active_contract_)),
      sim_viewport_w_(other.sim_viewport_w_),
      sim_viewport_h_(other.sim_viewport_h_),
      saved_unsimulated_w_(other.saved_unsimulated_w_),
      saved_unsimulated_h_(other.saved_unsimulated_h_),
      has_saved_unsimulated_size_(other.has_saved_unsimulated_size_),
      show_safe_area_guides_(other.show_safe_area_guides_),
      integer_zoom_sim_(other.integer_zoom_sim_),
      active_mock_preset_(std::move(other.active_mock_preset_)),
      has_live_mock_controller_(other.has_live_mock_controller_),
      capture_callback_(std::move(other.capture_callback_)) {
  if (self_handle_) *self_handle_ = this;
  other.active_view_id_ = 0;
}

UIPreviewer& UIPreviewer::operator=(UIPreviewer&& other) noexcept {
  if (this == &other) return *this;
  if (self_handle_) *self_handle_ = nullptr;
  registry_ = other.registry_;
  paused_systems_ = std::move(other.paused_systems_);
  self_handle_ = std::move(other.self_handle_);
  if (self_handle_) *self_handle_ = this;
  catalog_views_ = std::move(other.catalog_views_);
  catalog_styles_ = std::move(other.catalog_styles_);
  active_root_ = other.active_root_;
  active_view_id_ = other.active_view_id_;
  orig_canvas_base_layer_ = other.orig_canvas_base_layer_;
  captured_orig_base_layer_ = other.captured_orig_base_layer_;
  backdrop_layer_ = other.backdrop_layer_;
  active_source_ = std::move(other.active_source_);
  active_mock_path_ = std::move(other.active_mock_path_);
  inline_xml_ = std::move(other.inline_xml_);
  inline_oss_ = std::move(other.inline_oss_);
  is_inline_ = other.is_inline_;
  backdrop_mode_ = other.backdrop_mode_;
  isolated_ = other.isolated_;
  prev_paused_ = other.prev_paused_;
  has_saved_pause_state_ = other.has_saved_pause_state_;
  muted_view_states_ = std::move(other.muted_view_states_);
  callback_events_ = std::move(other.callback_events_);
  pending_capture_path_ = std::move(other.pending_capture_path_);
  last_capture_status_ = std::move(other.last_capture_status_);
  mock_source_mode_ = other.mock_source_mode_;
  target_sidecar_path_ = std::move(other.target_sidecar_path_);
  last_mock_error_ = std::move(other.last_mock_error_);
  mock_modified_ = other.mock_modified_;
  last_mock_write_time_ = other.last_mock_write_time_;
  has_mock_write_time_ = other.has_mock_write_time_;
  mock_hot_reload_accum_ = other.mock_hot_reload_accum_;
  last_seen_reload_count_ = other.last_seen_reload_count_;
  callback_seq_ = other.callback_seq_;
  active_contract_ = std::move(other.active_contract_);
  sim_viewport_w_ = other.sim_viewport_w_;
  sim_viewport_h_ = other.sim_viewport_h_;
  saved_unsimulated_w_ = other.saved_unsimulated_w_;
  saved_unsimulated_h_ = other.saved_unsimulated_h_;
  has_saved_unsimulated_size_ = other.has_saved_unsimulated_size_;
  show_safe_area_guides_ = other.show_safe_area_guides_;
  integer_zoom_sim_ = other.integer_zoom_sim_;
  active_mock_preset_ = std::move(other.active_mock_preset_);
  has_live_mock_controller_ = other.has_live_mock_controller_;
  capture_callback_ = std::move(other.capture_callback_);
  other.active_view_id_ = 0;
  return *this;
}

void UIPreviewer::RefreshCatalogViews() {
  catalog_views_.clear();
  catalog_styles_.clear();
  if (registry_ == nullptr) return;
  const auto* am = registry_->TryGet<AssetManager>();
  if (am == nullptr) return;

  const std::string basePath = am->GetBasePath();
  for (const auto& [id, entry] : am->GetCatalog().Entries()) {
    if (entry.type == AssetType::UIView) {
      catalog_views_.push_back({id, entry.fullPath, MakeDisplayPath(entry.fullPath, basePath)});
    } else if (entry.type == AssetType::UIStyle) {
      catalog_styles_.push_back({id, entry.fullPath, MakeDisplayPath(entry.fullPath, basePath)});
    }
  }
  const auto byDisplay = [](const PreviewCatalogEntry& a, const PreviewCatalogEntry& b) {
    return a.displayPath < b.displayPath;
  };
  std::ranges::sort(catalog_views_, byDisplay);
  std::ranges::sort(catalog_styles_, byDisplay);
}

namespace {
void ApplyAutoSeedContextToMountedView(UIRuntime& uiRuntime, const Registry* registry, sol::state& lua,
                                       const sol::table& mountedTable, const OxmlContract& contract) {
  sol::table rawSeed = lua.create_table();
  SeedAutoFieldsIntoTable(lua, registry, rawSeed, contract);
  sol::table seededObs = uiRuntime.CreateObservable(rawSeed);
  const sol::protected_function setCtx = mountedTable.get<sol::protected_function>("set_context");
  if (setCtx.valid()) {
    setCtx(mountedTable, seededObs);
  }
}

sol::object MakeWrappedControllerFn(sol::state& lua, const std::shared_ptr<UIPreviewer*>& handle,
                                    const std::string& key, const sol::table& backing,
                                    const sol::protected_function& userFn) {
  return sol::make_object(lua,
                          [handle, key, backing, userFn](const sol::table&, sol::variadic_args args) -> sol::object {
                            const sol::object firstArg = args.size() > 0 ? sol::object(args[0]) : sol::lua_nil;
                            if (handle && *handle != nullptr) {
                              (*handle)->RecordCallbackEvent(key + "(" + FormatFallbackArg(firstArg) + ")");
                            }
                            return InvokeUserCtrlFn(userFn, backing, args);
                          });
}

sol::object MakeStubControllerFn(sol::state& lua, const std::shared_ptr<UIPreviewer*>& handle, const std::string& key) {
  return sol::make_object(lua, [handle, key](const sol::table&, const sol::object& arg) {
    const std::string msg = key + "(" + FormatFallbackArg(arg) + ")";
    if (handle && *handle != nullptr) {
      (*handle)->RecordCallbackEvent(msg);
    }
    Logger::Info("[ui-preview] callback triggered: " + msg);
  });
}
}  // namespace

namespace octarine::ui_preview_detail {
void RecordMockMtime(const std::string& mockPath, std::filesystem::file_time_type& outTime, bool& outHasTime) {
  std::error_code ec;
  if (!mockPath.empty() && fs::exists(mockPath, ec)) {
    outTime = fs::last_write_time(mockPath, ec);
    outHasTime = !ec;
  } else {
    outHasTime = false;
  }
}

bool AreSameDiskPath(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) return false;
  if (a == b) return true;
  std::error_code ecA;
  std::error_code ecB;
  const fs::path ca = fs::weakly_canonical(fs::path(a), ecA);
  const fs::path cb = fs::weakly_canonical(fs::path(b), ecB);
  if (!ecA && !ecB) return ca == cb;
  return fs::path(a).lexically_normal() == fs::path(b).lexically_normal();
}
}  // namespace octarine::ui_preview_detail

sol::table UIPreviewer::BuildFallbackController(const sol::optional<sol::table>& userController) {
  auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr;
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return sol::table{};
  sol::state& lua = *uiRuntime->GetLua();
  sol::table wrapper = lua.create_table();
  sol::table backing = userController.value_or(lua.create_table());
  sol::table meta = lua.create_table();
  const std::shared_ptr<UIPreviewer*> handle = self_handle_;

  meta.set_function("__index", [&lua, backing, handle](const sol::table&, const std::string& key) -> sol::object {
    if (const sol::object existing = LookupUserControllerMember(backing, key); existing != sol::lua_nil) {
      return existing.is<sol::protected_function>()
                 ? MakeWrappedControllerFn(lua, handle, key, backing, existing.as<sol::protected_function>())
                 : existing;
    }
    if (key == "on_mount" || key == "on_unmount") return sol::lua_nil;
    return MakeStubControllerFn(lua, handle, key);
  });

  wrapper[sol::metatable_key] = meta;
  return wrapper;
}

sol::optional<sol::table> UIPreviewer::PrepareMountOptions(const sol::optional<sol::table>& opts,
                                                           const std::string& inlineOss) {
  auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr;
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return std::nullopt;
  sol::state& lua = *uiRuntime->GetLua();

  sol::table out = lua.create_table();
  sol::optional<sol::table> userCtrl;
  if (opts.has_value()) {
    for (const auto& [k, v] : *opts) {
      out[k] = v;
    }
    const sol::object ctrlObj = (*opts)["controller"];
    if (ctrlObj.is<sol::table>()) {
      userCtrl = ctrlObj.as<sol::table>();
    }
  }
  out.set("controller", BuildFallbackController(userCtrl));

  if (!inlineOss.empty()) {
    sol::table styles = lua.create_table();
    styles.set(1, inlineOss);
    out.set("styles", styles);
  }
  return out;
}

bool UIPreviewer::LoadMockScriptOptions(const std::string& mockPath, sol::table& outOpts) {
  auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr;
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;
  sol::table rawCtrl;
  sol::table rawRoot;
  std::string err;
  std::string presetKey;
  bool hasCtrl = false;
  const std::string resolved = ResolveMockDiskPath(registry_, uiRuntime, mockPath);
  if (!LoadSandboxedMockScript(*uiRuntime, resolved, "", outOpts, rawCtrl, rawRoot, presetKey, hasCtrl, err)) {
    Logger::Error("[ui-preview] mock script error: " + err);
    return false;
  }
  return true;
}

namespace {
sol::object ExtractRawCtrlOpt(const sol::optional<sol::table>& opts) {
  if (!opts.has_value()) return sol::lua_nil;
  const sol::object ctrlObj = (*opts)["controller"];
  return ctrlObj.is<sol::table>() ? ctrlObj : sol::object(sol::lua_nil);
}
}  // namespace

bool UIPreviewer::MountResolvedPreview(std::string sourceIn, const sol::optional<sol::table>& opts,
                                       std::string inlineOssIn, const bool isInline, const MockSourceMode mockMode,
                                       std::string mockPathIn, std::string sidecarPathIn,
                                       const sol::optional<sol::table>& rawMockRoot, std::string presetName) {
  auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr;
  if (registry_ == nullptr || uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;

  const bool wasIsolated = isolated_;
  UnmountPreview(/*restoreIsolationState=*/false);

  const sol::object rawCtrlPin = ExtractRawCtrlOpt(opts);
  const sol::object rawMockPin = rawMockRoot.has_value() ? sol::object(*rawMockRoot) : sol::object(sol::lua_nil);

  const sol::optional<sol::table> mountOpts = PrepareMountOptions(opts, inlineOssIn);
  sol::state& lua = *uiRuntime->GetLua();
  const sol::object result = uiRuntime->Mount(sol::make_object(lua, sourceIn), mountOpts);
  if (!result.is<sol::table>()) {
    if (wasIsolated) RestoreBackgroundIsolation();
    return false;
  }

  const sol::table mountedTable = result.as<sol::table>();
  PinPreviewTableInLuaRegistry(lua.lua_state(), mountedTable);
  PinLuaRegistryObject(lua.lua_state(), "__octarine_preview_controller", rawCtrlPin);
  PinLuaRegistryObject(lua.lua_state(), "__octarine_preview_raw_mock", rawMockPin);

  active_root_ = ReadRootEntityField(mountedTable);
  active_view_id_ = uiRuntime->FindViewIdByRoot(active_root_).value_or(0);
  active_source_ = isInline ? "<inline>" : sourceIn;
  is_inline_ = isInline;
  inline_xml_ = isInline ? std::move(sourceIn) : "";
  inline_oss_ = isInline ? std::move(inlineOssIn) : "";
  captured_orig_base_layer_ = false;
  mock_source_mode_ = mockMode;
  active_mock_path_ = std::move(mockPathIn);
  target_sidecar_path_ = std::move(sidecarPathIn);
  active_mock_preset_ = std::move(presetName);
  const sol::table rawTbl = rawMockPin.is<sol::table>() ? rawMockPin.as<sol::table>() : sol::table{};
  const sol::table ctrlTbl = rawCtrlPin.is<sol::table>() ? rawCtrlPin.as<sol::table>() : sol::table{};
  has_live_mock_controller_ = HasAnyControllerFunction(rawTbl, ctrlTbl);
  mock_modified_ = false;
  last_mock_error_.clear();
  last_seen_reload_count_ = uiRuntime->GetDiagnostics().reloadCount;
  RecordMockMtime(active_mock_path_, last_mock_write_time_, has_mock_write_time_);

  RefreshContractState();
  if (mock_source_mode_ == MockSourceMode::AutoSeeded) {
    ApplyAutoSeedContextToMountedView(*uiRuntime, registry_, lua, mountedTable, active_contract_);
  }

  if (wasIsolated) {
    muted_view_states_ = uiRuntime->SetNonPreviewViewsInert(active_view_id_, true);
  }
  SyncPreviewLayerOrder();
  return true;
}

bool UIPreviewer::MountPreview(const std::string& pathOrId, const sol::optional<sol::table>& opts) {
  if (registry_ == nullptr) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;

  const std::string sourceCopy = pathOrId;
  std::string requestedPreset;
  bool hasExplicitData = false;
  if (opts.has_value()) {
    const sol::object pObj = opts->get<sol::object>("preset");
    if (pObj.is<std::string>()) requestedPreset = pObj.as<std::string>();
    hasExplicitData = (*opts)["context"].is<sol::table>() || (*opts)["controller"].is<sol::table>() ||
                      (*opts)["params"].is<sol::table>();
  }

  RefreshCatalogViews();
  const std::string resolvedSource = MatchCatalogEntryForPreview(catalog_views_, sourceCopy);
  const std::string sidecarPath = ComputeSidecarPathForView(uiRuntime, resolvedSource, /*isInline=*/false);

  if (hasExplicitData) {
    return MountResolvedPreview(resolvedSource, opts, "", false, MockSourceMode::ExplicitLua, "", sidecarPath,
                                sol::nullopt, requestedPreset);
  }

  std::error_code ec;
  if (!sidecarPath.empty() && fs::exists(sidecarPath, ec)) {
    return MountSidecarPreview(resolvedSource, sidecarPath, requestedPreset, opts);
  }

  const bool mounted =
      MountResolvedPreview(resolvedSource, opts, "", false, MockSourceMode::AutoSeeded, "", sidecarPath, sol::nullopt);
  if (mounted && !requestedPreset.empty()) {
    static_cast<void>(ApplyStressPreset(requestedPreset));
  }
  return mounted;
}

bool UIPreviewer::MountSidecarPreview(const std::string& resolvedSource, const std::string& sidecarPath,
                                      const std::string& requestedPreset, const sol::optional<sol::table>& opts) {
  auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr;
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;
  sol::state& lua = *uiRuntime->GetLua();
  sol::table mockOpts = lua.create_table();
  if (opts.has_value()) {
    for (const auto& [k, v] : *opts) mockOpts[k] = v;
  }
  sol::table rawCtrl;
  sol::table rawRoot;
  std::string resolvedPresetKey;
  bool hasCtrl = false;
  std::string sidecarErr;
  const std::string filePresetReq = (!requestedPreset.empty() && requestedPreset.front() == '@') ? "" : requestedPreset;
  if (!LoadSandboxedMockScript(*uiRuntime, sidecarPath, filePresetReq, mockOpts, rawCtrl, rawRoot, resolvedPresetKey,
                               hasCtrl, sidecarErr)) {
    Logger::Error("[ui-preview] sidecar error in '" + sidecarPath + "': " + sidecarErr);
    const bool ok = MountResolvedPreview(resolvedSource, opts, "", false, MockSourceMode::AutoSeeded, sidecarPath,
                                         sidecarPath, sol::nullopt);
    last_mock_error_ = sidecarErr;
    return ok;
  }
  const bool ok = MountResolvedPreview(resolvedSource, mockOpts, "", false, MockSourceMode::SidecarPreview, sidecarPath,
                                       sidecarPath, rawRoot, resolvedPresetKey);
  if (ok && !requestedPreset.empty() && requestedPreset.front() == '@') {
    static_cast<void>(ApplyStressPreset(requestedPreset));
  }
  return ok;
}

bool UIPreviewer::MountPreviewWithMock(const std::string& pathOrId, const std::string& mockScriptPath,
                                       const std::string& presetName) {
  if (registry_ == nullptr) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;
  if (mockScriptPath.empty()) {
    if (!presetName.empty()) {
      sol::table opts = uiRuntime->GetLua()->create_table();
      opts.set("preset", presetName);
      return MountPreview(pathOrId, opts);
    }
    return MountPreview(pathOrId);
  }

  const std::string sourceCopy = pathOrId;
  const std::string resolvedMock = ResolveMockDiskPath(registry_, uiRuntime, mockScriptPath);
  RefreshCatalogViews();
  const std::string resolvedSource = MatchCatalogEntryForPreview(catalog_views_, sourceCopy);
  const std::string sidecarPath = ComputeSidecarPathForView(uiRuntime, resolvedSource, /*isInline=*/false);

  sol::table mockOpts = uiRuntime->GetLua()->create_table();
  sol::table rawCtrl;
  sol::table rawRoot;
  std::string resolvedPresetKey;
  bool hasCtrl = false;
  std::string err;
  const std::string filePresetReq = (!presetName.empty() && presetName.front() == '@') ? "" : presetName;
  if (!LoadSandboxedMockScript(*uiRuntime, resolvedMock, filePresetReq, mockOpts, rawCtrl, rawRoot, resolvedPresetKey,
                               hasCtrl, err)) {
    last_mock_error_ = err;
    Logger::Error("[ui-preview] mock script error: " + err);
    return false;
  }
  const bool ok = MountResolvedPreview(resolvedSource, mockOpts, "", false, MockSourceMode::ExplicitLua, resolvedMock,
                                       sidecarPath, rawRoot, resolvedPresetKey);
  if (ok && !presetName.empty() && presetName.front() == '@') {
    static_cast<void>(ApplyStressPreset(presetName));
  }
  return ok;
}

bool UIPreviewer::MountInlineXml(const std::string& xmlSource, const std::string& ossSource) {
  if (registry_ == nullptr || xmlSource.empty()) return false;
  const std::string xmlCopy = xmlSource;
  const std::string ossCopy = ossSource;
  return MountResolvedPreview(xmlCopy, sol::nullopt, ossCopy, true, MockSourceMode::AutoSeeded, "", "", sol::nullopt);
}

bool UIPreviewer::ReloadActivePreview() {
  if (registry_ == nullptr || !IsActive()) return false;
  if (is_inline_) {
    return MountInlineXml(inline_xml_, inline_oss_);
  }
  if (!active_mock_path_.empty()) {
    const std::string src = active_source_;
    const std::string mock = active_mock_path_;
    const std::string reqPreset =
        (!active_mock_preset_.empty() && active_mock_preset_.front() != '@') ? active_mock_preset_ : "";
    const MockSourceMode nextMode =
        (mock == target_sidecar_path_) ? MockSourceMode::SidecarPreview : MockSourceMode::ExplicitLua;
    auto* uiRuntime = registry_->TryGet<UIRuntime>();
    if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;

    sol::table mockOpts = uiRuntime->GetLua()->create_table();
    sol::table rawCtrl;
    sol::table rawRoot;
    std::string resolvedKey;
    bool hasCtrl = false;
    std::string err;
    if (!LoadSandboxedMockScript(*uiRuntime, mock, reqPreset, mockOpts, rawCtrl, rawRoot, resolvedKey, hasCtrl, err)) {
      last_mock_error_ = err;
      Logger::Error("[ui-preview] mock reload error: " + err);
      return false;
    }
    return MountResolvedPreview(src, mockOpts, "", false, nextMode, mock, target_sidecar_path_, rawRoot, resolvedKey);
  }
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || active_view_id_ == 0) return false;

  captured_orig_base_layer_ = false;
  const bool ok = uiRuntime->ReloadMountedView(active_view_id_);
  SyncActivePreview();
  return ok;
}

void UIPreviewer::RestoreBackgroundIsolation() {
  if (registry_ != nullptr) {
    if (auto* uiRuntime = registry_->TryGet<UIRuntime>(); uiRuntime != nullptr && !muted_view_states_.empty()) {
      uiRuntime->RestoreViewsInert(muted_view_states_);
    }
    if (has_saved_pause_state_) {
      if (auto* cfg = registry_->TryGet<GameConfig>()) {
        cfg->GetEngineOptions().isPaused = prev_paused_;
      }
      has_saved_pause_state_ = false;
    }
  }
  muted_view_states_.clear();
}

void UIPreviewer::UnmountPreview(const bool restoreIsolationState) {
  if (registry_ != nullptr) {
    if (auto* uiRuntime = registry_->TryGet<UIRuntime>()) {
      if (active_view_id_ != 0) {
        uiRuntime->UnmountView(active_view_id_);
      }
      if (sol::state* lua = uiRuntime->GetLua(); lua != nullptr && lua->lua_state() != nullptr) {
        PinPreviewTableInLuaRegistry(lua->lua_state(), sol::lua_nil);
        PinLuaRegistryObject(lua->lua_state(), "__octarine_preview_controller", sol::lua_nil);
        PinLuaRegistryObject(lua->lua_state(), "__octarine_preview_raw_mock", sol::lua_nil);
      }
    }
  }
  active_root_ = Entity{};
  active_view_id_ = 0;
  captured_orig_base_layer_ = false;
  active_source_.clear();
  active_mock_path_.clear();
  target_sidecar_path_.clear();
  mock_source_mode_ = MockSourceMode::None;
  active_mock_preset_.clear();
  has_live_mock_controller_ = false;
  mock_modified_ = false;
  has_mock_write_time_ = false;
  last_mock_error_.clear();
  last_seen_reload_count_ = 0;
  active_contract_ = OxmlContract{};
  is_inline_ = false;
  inline_xml_.clear();
  inline_oss_.clear();

  if (restoreIsolationState) {
    isolated_ = false;
    RestoreBackgroundIsolation();
    RestoreSimulatedViewportIfOwned();
  } else if (registry_ != nullptr) {
    if (auto* uiRuntime = registry_->TryGet<UIRuntime>(); uiRuntime != nullptr && !muted_view_states_.empty()) {
      uiRuntime->RestoreViewsInert(muted_view_states_);
    }
    muted_view_states_.clear();
  }
}

bool UIPreviewer::IsActive() const {
  if (registry_ == nullptr || active_view_id_ == 0) return false;
  const auto* uiRuntime = registry_->TryGet<UIRuntime>();
  return uiRuntime != nullptr && uiRuntime->IsViewMounted(active_view_id_);
}

Entity UIPreviewer::ActiveRootEntity() const {
  if (registry_ == nullptr || active_view_id_ == 0) return {};
  const auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr) return active_root_;
  const Entity root = uiRuntime->GetViewRoot(active_view_id_);
  return registry_->IsAlive(root) ? root : active_root_;
}

void UIPreviewer::SyncPreviewLayerOrder() {
  if (registry_ == nullptr || !IsActive()) return;
  const Entity root = ActiveRootEntity();
  if (!registry_->IsAlive(root) || !registry_->HasComponent<UICanvasComponent>(root)) return;

  auto& canvas = registry_->GetComponent<UICanvasComponent>(root);
  if (!captured_orig_base_layer_) {
    orig_canvas_base_layer_ = canvas.baseLayer;
    captured_orig_base_layer_ = true;
  }

  if (!paused_systems_) {
    paused_systems_ = std::make_unique<PausedRenderSystems>();
  }
  paused_systems_->EnsureQueries(*registry_);
  auto& q = paused_systems_->canvasQuery;
  int maxOtherLayer = -1;
  q->Update();
  q->ForEach([&](const Entity ent, const UICanvasComponent& other) {
    if (ent.id != root.id) {
      maxOtherLayer = std::max(maxOtherLayer, other.baseLayer);
    }
  });

  const bool hasBackdrop = (backdrop_mode_ != PreviewBackdropMode::SceneOverlay);
  int minRequiredLayer = orig_canvas_base_layer_;
  if (maxOtherLayer >= 0) {
    minRequiredLayer = std::max(minRequiredLayer, maxOtherLayer + kLayerMargin);
  }
  if (hasBackdrop) {
    minRequiredLayer = std::max(minRequiredLayer, kMinBackdropHeadroom);
  }
  canvas.baseLayer = std::clamp(minRequiredLayer, 0, kMaxPreviewCanvasLayer);
  backdrop_layer_ = hasBackdrop ? std::max(0, canvas.baseLayer - 1) : 0;
}

void UIPreviewer::SyncActivePreview() {
  if (!IsActive()) {
    if (active_view_id_ != 0) {
      UnmountPreview(true);
    }
    return;
  }
  active_root_ = ActiveRootEntity();
  if (auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr) {
    const std::uint64_t reloads = uiRuntime->GetDiagnostics().reloadCount;
    if (reloads != last_seen_reload_count_) {
      last_seen_reload_count_ = reloads;
      RefreshContractState();
      if (mock_source_mode_ == MockSourceMode::AutoSeeded && uiRuntime->GetLua() != nullptr) {
        sol::state& lua = *uiRuntime->GetLua();
        const sol::object pinObj = GetPinnedLuaRegistryObject(lua.lua_state(), "__octarine_preview_pin");
        if (pinObj.is<sol::table>()) {
          const sol::table viewTbl = pinObj.as<sol::table>();
          const sol::protected_function getCtx = viewTbl.get<sol::protected_function>("get_context");
          const sol::object ctxObj = getCtx.valid() ? sol::object(getCtx(viewTbl)) : sol::lua_nil;
          if (ctxObj.is<sol::table>()) {
            sol::table ctx = ctxObj.as<sol::table>();
            SeedAutoFieldsIntoTable(lua, registry_, ctx, active_contract_);
            const sol::protected_function refreshFn = viewTbl.get<sol::protected_function>("refresh");
            if (refreshFn.valid()) refreshFn(viewTbl);
          }
        }
      }
    }
  }
  SyncPreviewLayerOrder();
}

void UIPreviewer::SetBackdropMode(const PreviewBackdropMode mode) {
  backdrop_mode_ = mode;
  SyncPreviewLayerOrder();
}

void UIPreviewer::SetIsolatedMode(const bool isolated, const bool managePauseState) {
  if (isolated_ == isolated) return;
  isolated_ = isolated;

  if (registry_ == nullptr) return;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();

  if (isolated_) {
    if (backdrop_mode_ == PreviewBackdropMode::SceneOverlay) {
      backdrop_mode_ = PreviewBackdropMode::DarkSlate;
    }
    if (managePauseState) {
      if (auto* cfg = registry_->TryGet<GameConfig>(); cfg != nullptr && !has_saved_pause_state_) {
        prev_paused_ = cfg->GetEngineOptions().isPaused;
        has_saved_pause_state_ = true;
        cfg->GetEngineOptions().isPaused = true;
      }
    }
    if (uiRuntime != nullptr) {
      muted_view_states_ = uiRuntime->SetNonPreviewViewsInert(active_view_id_, true);
    }
  } else {
    RestoreBackgroundIsolation();
  }
  SyncPreviewLayerOrder();
}

bool UIPreviewer::ShouldRenderWhilePaused() const {
  return IsActive() || backdrop_mode_ != PreviewBackdropMode::SceneOverlay;
}

void UIPreviewer::EmitBackdropIfNeeded(Registry& registry) {
  if (backdrop_mode_ == PreviewBackdropMode::SceneOverlay) return;
  auto* renderQueue = registry.TryGet<RenderQueue>();
  const auto* cfg = registry.TryGet<GameConfig>();
  if (renderQueue == nullptr || cfg == nullptr) return;

  const auto w = static_cast<float>(std::max(1, cfg->windowWidth));
  const auto h = static_cast<float>(std::max(1, cfg->windowHeight));
  const auto layer = static_cast<unsigned int>(std::max(0, backdrop_layer_));

  auto emitSolid = [&](const SDL_Color col, const float x, const float y, const float bw, const float bh,
                       const float depth) {
    auto& cmd = renderQueue->EmplaceSquare(layer, depth, nullptr, octarine::BlendMode::None, 0);
    cmd.destRect = {x, y, bw, bh};
    cmd.color = col;
    cmd.rotation = 0.0f;
    cmd.blendMode = SDL_BLENDMODE_NONE;
  };

  if (backdrop_mode_ == PreviewBackdropMode::DarkSlate) {
    emitSolid(SDL_Color{24, 26, 32, 255}, 0.0f, 0.0f, w, h, -1000.0f);
    return;
  }
  if (backdrop_mode_ == PreviewBackdropMode::LightGray) {
    emitSolid(SDL_Color{230, 232, 236, 255}, 0.0f, 0.0f, w, h, -1000.0f);
    return;
  }

  // Checkerboard: tile size guarantees <= ~150 quads even at 4K so small test queues never overflow.
  const float tile = std::max(32.0f, std::ceil(std::max(w, h) / 24.0f));
  emitSolid(SDL_Color{202, 204, 212, 255}, 0.0f, 0.0f, w, h, -1000.0f);
  const int cols = static_cast<int>(std::ceil(w / tile));
  const int rows = static_cast<int>(std::ceil(h / tile));
  const SDL_Color darkTile{162, 165, 174, 255};
  for (int row = 0; row < rows; ++row) {
    for (int col = (row % 2); col < cols; col += 2) {
      const float x = static_cast<float>(col) * tile;
      const float y = static_cast<float>(row) * tile;
      emitSolid(darkTile, x, y, std::min(tile, w - x), std::min(tile, h - y), -999.0f);
    }
  }
}

void UIPreviewer::RenderWhenPaused(Registry& registry) {
  SyncActivePreview();
  if (!ShouldRenderWhilePaused()) return;
  auto* renderQueue = registry.TryGet<RenderQueue>();
  if (renderQueue == nullptr) return;

  renderQueue->Clear();
  EmitBackdropIfNeeded(registry);
  if (!paused_systems_) {
    paused_systems_ = std::make_unique<PausedRenderSystems>();
  }
  paused_systems_->Run(registry);
}

namespace octarine::ui_preview_detail {
bool TryGetActiveViewContext(const Registry* registry, sol::state*& outLua, sol::table& outViewTbl,
                             sol::table& outCtxTbl) {
  if (registry == nullptr) return false;
  const auto* uiRuntime = registry->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;
  outLua = uiRuntime->GetLua();

  const sol::object pinObj = GetPinnedLuaRegistryObject(outLua->lua_state(), "__octarine_preview_pin");
  if (!pinObj.is<sol::table>()) return false;
  outViewTbl = pinObj.as<sol::table>();
  const sol::protected_function getCtx = outViewTbl.get<sol::protected_function>("get_context");
  if (!getCtx.valid()) return false;
  const sol::object ctxObj = getCtx(outViewTbl);
  if (ctxObj.is<sol::table>()) {
    outCtxTbl = ctxObj.as<sol::table>();
  }
  return true;
}
}  // namespace octarine::ui_preview_detail

namespace {

sol::table EnsureMutableContextTable(UIRuntime& uiRuntime, sol::state& lua, sol::table& viewTbl,
                                     sol::table existingCtx) {
  if (existingCtx.valid()) return existingCtx;
  sol::table obs = uiRuntime.CreateObservable(lua.create_table());
  const sol::protected_function setCtx = viewTbl.get<sol::protected_function>("set_context");
  if (setCtx.valid()) {
    setCtx(viewTbl, obs);
  }
  return obs;
}

void RefreshViewIfPlainContext(sol::table& viewTbl, const sol::table& ctx) {
  if (!ctx.valid() || ctx.get_or("__is_observable", false)) return;
  const sol::protected_function refreshFn = viewTbl.get<sol::protected_function>("refresh");
  if (refreshFn.valid()) refreshFn(viewTbl);
}

std::string NormalizeArrayContractPath(std::string_view path) {
  std::string out;
  out.reserve(path.size());
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '[') {
      const std::size_t close = path.find(']', i + 1);
      if (close != std::string_view::npos) {
        out += "[]";
        i = close;
        continue;
      }
    }
    out.push_back(path[i]);
  }
  return out;
}

const ContractField* FindContractFieldByPath(const OxmlContract& contract, std::string_view concretePath) {
  const std::string normalized = NormalizeArrayContractPath(concretePath);
  for (const auto& f : contract.fields) {
    if (f.path == concretePath || f.path == normalized) return &f;
  }
  return nullptr;
}

MockFieldEntry BuildMockEntryFromContract(const ContractField& f, const std::string& concretePath,
                                          const sol::object& liveVal, const bool isAutoMode) {
  MockFieldEntry e;
  e.path = concretePath;
  e.kind = ContractTypeToMockKind(f.inferredType);
  e.isConditionalOnly = f.conditionalOnly;
  e.isTwoWay = f.isTwoWay;
  e.isAutoSeeded = isAutoMode;
  e.sampleHint = f.sampleHint;
  e.sliderMin = f.minVal;
  e.sliderMax = f.maxVal;
  e.sliderStep = f.step;
  e.hasSliderRange = f.hasSliderRange;
  e.radioOptions = f.radioOptions;
  PopulateEntryFromLiveObject(e, liveVal);
  return e;
}

void EmitContractArrayItemEntries(const OxmlContract& contract, const ContractField& arrField, const sol::table& arrTbl,
                                  const std::size_t idx, const bool isAutoMode,
                                  std::unordered_set<std::string>& seenPaths, std::vector<MockFieldEntry>& out) {
  const sol::table unwrappedArr = UnwrapObservableShallow(arrTbl);
  const sol::object elemObj = unwrappedArr.get<sol::object>(idx);
  const std::string prefix = arrField.path + "[" + std::to_string(idx) + "]";
  const std::string subNeedle = arrField.path + "[].";
  bool hadSubfields = false;

  for (const auto& sf : contract.fields) {
    if (!sf.path.starts_with(subNeedle)) continue;
    const std::string subSuffix = sf.path.substr(subNeedle.size());
    if (subSuffix.find("[]") != std::string::npos) continue;
    hadSubfields = true;
    const std::string itemPath = prefix + "." + subSuffix;
    if (!seenPaths.insert(itemPath).second) continue;
    const sol::object subVal = elemObj.is<sol::table>() ? GetUnwrappedPathValue(elemObj.as<sol::table>(), subSuffix)
                                                        : sol::object(sol::lua_nil);
    out.push_back(BuildMockEntryFromContract(sf, itemPath, subVal, isAutoMode));
  }

  if (hadSubfields) return;
  if (const auto* bare = FindContractFieldByPath(contract, arrField.path + "[]")) {
    if (seenPaths.insert(prefix).second) {
      out.push_back(BuildMockEntryFromContract(*bare, prefix, elemObj, isAutoMode));
    }
  }
}

void PushContractAuditIssue(std::vector<UILayoutIssue>& out, const std::string& code, const Entity root,
                            const std::string& label, const std::string& message) {
  UILayoutIssue issue;
  issue.code = code;
  issue.severity = "warning";
  issue.entity = root;
  issue.label = label;
  issue.message = message;
  out.push_back(std::move(issue));
}

void CheckUnresolvedArraySubfield(const ContractField& f, const sol::table& ctx, const Entity root,
                                  std::vector<UILayoutIssue>& out) {
  struct Frame {
    std::string prefix;
    std::string remPattern;
  };
  std::vector<Frame> stack{{"", f.path}};
  while (!stack.empty()) {
    const Frame cur = std::move(stack.back());
    stack.pop_back();
    const std::size_t br = cur.remPattern.find("[].");
    if (br == std::string::npos) {
      const std::string concrete = cur.prefix + cur.remPattern;
      if (!ctx.valid() || GetUnwrappedPathValue(ctx, concrete) == sol::lua_nil) {
        PushContractAuditIssue(out, "unresolved_contract", root, concrete,
                               "Context binding '" + concrete + "' (" + ContractFieldTypeName(f.inferredType) +
                                   ") is nil in mock context.");
      }
      continue;
    }
    const std::string arrPath = cur.prefix + cur.remPattern.substr(0, br);
    const std::string after = cur.remPattern.substr(br + 3);
    const sol::object arrObj = ctx.valid() ? GetUnwrappedPathValue(ctx, arrPath) : sol::object(sol::lua_nil);
    if (!arrObj.is<sol::table>()) continue;
    const sol::table unwrappedArr = UnwrapObservableShallow(arrObj.as<sol::table>());
    const std::size_t n = unwrappedArr.size();
    for (std::size_t i = n; i >= 1; --i) {
      stack.push_back({arrPath + "[" + std::to_string(i) + "].", after});
    }
  }
}

void CheckUnresolvedContextFields(const OxmlContract& contract, const sol::table& ctx, const Entity root,
                                  std::vector<UILayoutIssue>& out) {
  for (const auto& f : contract.fields) {
    if (f.conditionalOnly) continue;
    if (f.path.find("[].") != std::string::npos) {
      CheckUnresolvedArraySubfield(f, ctx, root, out);
      continue;
    }
    if (f.path.find("[]") != std::string::npos) continue;
    if (ctx.valid() && GetUnwrappedPathValue(ctx, f.path) != sol::lua_nil) continue;
    PushContractAuditIssue(
        out, "unresolved_contract", root, f.path,
        "Context binding '" + f.path + "' (" + ContractFieldTypeName(f.inferredType) + ") is nil in mock context.");
  }
}

void CheckMissingControllerCallbacks(const OxmlContract& contract, lua_State* L, const Entity root,
                                     std::vector<UILayoutIssue>& out) {
  const sol::object rawCtrlObj = GetPinnedLuaRegistryObject(L, "__octarine_preview_controller");
  if (!rawCtrlObj.is<sol::table>()) return;
  const sol::table rawCtrl = rawCtrlObj.as<sol::table>();
  for (const auto& cb : contract.callbacks) {
    const sol::object fn = rawCtrl.get<sol::object>(cb.name);
    if (fn.is<sol::protected_function>()) continue;
    PushContractAuditIssue(
        out, "unresolved_contract", root, cb.name,
        "Callback '" + cb.name + "' (" + cb.sourceAttr + ") is missing from explicit controller table.");
  }
}

void AppendJsonContractSection(std::ostringstream& ss, const OxmlContract& contract,
                               const std::vector<MockParamEntry>& params,
                               const std::vector<MockCallbackEntry>& callbacks,
                               const std::vector<MockFieldEntry>& liveFields) {
  ss << "  \"contract\": {\n    \"params\": [";
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i > 0) ss << ", ";
    ss << R"({"name": ")" << EscapeJsonString(params[i].name) << R"(", "default": ")"
       << EscapeJsonString(params[i].defaultValue) << R"(", "current": ")" << EscapeJsonString(params[i].currentValue)
       << R"(", "overridden": )" << (params[i].isOverridden ? "true" : "false") << "}";
  }
  ss << "],\n    \"vars\": [";
  for (std::size_t i = 0; i < contract.fields.size(); ++i) {
    const auto& f = contract.fields[i];
    if (i > 0) ss << ", ";
    const bool isUnset = UIPreviewer::IsContractFieldUnset(f, liveFields);
    ss << R"({"path": ")" << EscapeJsonString(f.path) << R"(", "type": ")" << ContractFieldTypeName(f.inferredType)
       << R"(", "conditional_only": )" << (f.conditionalOnly ? "true" : "false") << R"(, "two_way": )"
       << (f.isTwoWay ? "true" : "false") << R"(, "unset": )" << (isUnset ? "true" : "false") << "}";
  }
  ss << "],\n    \"callbacks\": [";
  for (std::size_t i = 0; i < callbacks.size(); ++i) {
    if (i > 0) ss << ", ";
    ss << R"({"name": ")" << EscapeJsonString(callbacks[i].name) << R"(", "attr": ")"
       << EscapeJsonString(callbacks[i].sourceAttr) << R"(", "raw": ")" << EscapeJsonString(callbacks[i].rawExpr)
       << R"(", "bound": )" << (callbacks[i].hasControllerHandler ? "true" : "false") << "}";
  }
  ss << "],\n    \"refs\": ";
  AppendJsonStringList(ss, contract.refs);
  ss << ",\n    \"slots\": ";
  AppendJsonStringList(ss, contract.slots);
  ss << "\n  },\n";
}

}  // namespace

void UIPreviewer::ApplyAuditIssuesForContract(std::vector<UILayoutIssue>& out) const {
  if (!IsActive() || registry_ == nullptr) return;
  const Entity root = ActiveRootEntity();
  for (const auto& u : active_contract_.unsupportedAttrs) {
    PushContractAuditIssue(out, "unsupported_attribute", root, "<" + u.tag + "> line " + std::to_string(u.line),
                           "<" + u.tag + "> uses unsupported/typo attribute '" + u.attr + "'. " + u.suggestion);
  }

  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (!TryGetActiveViewContext(registry_, luaPtr, viewTbl, ctx)) return;

  if (mock_source_mode_ == MockSourceMode::ExplicitLua || mock_source_mode_ == MockSourceMode::SidecarPreview) {
    CheckUnresolvedContextFields(active_contract_, ctx, root, out);
  }
  CheckMissingControllerCallbacks(active_contract_, luaPtr->lua_state(), root, out);
}

std::vector<UILayoutIssue> UIPreviewer::AuditLayout() const {
  std::vector<UILayoutIssue> issues;
  if (registry_ == nullptr) return issues;

  const auto* cfg = registry_->TryGet<GameConfig>();
  const float viewW = cfg != nullptr ? static_cast<float>(cfg->windowWidth) : 0.0f;
  const float viewH = cfg != nullptr ? static_cast<float>(cfg->windowHeight) : 0.0f;
  const float uiScale = cfg != nullptr ? octarine::ui::ResolveUiScale(*cfg) : 1.0f;

  if (IsActive()) {
    const Entity root = ActiveRootEntity();
    if (registry_->IsAlive(root)) {
      AuditSubtreeIterative(*registry_, root, viewW, viewH, uiScale, issues);
      ApplyAuditIssuesForContract(issues);
      return issues;
    }
  }

  auto canvasQuery = registry_->CreateQuery<UICanvasComponent>();
  canvasQuery->Update();
  canvasQuery->ForEach([&](const Entity root, const UICanvasComponent&) {
    AuditSubtreeIterative(*registry_, root, viewW, viewH, uiScale, issues);
  });
  return issues;
}

std::string UIPreviewer::AuditLayoutJson() const {
  const auto issues = AuditLayout();
  const auto fields = CollectMockFields();
  const auto params = CollectMockParams();
  const auto callbacks = CollectMockCallbacks();
  const auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr;
  const auto* cfg = registry_ != nullptr ? registry_->TryGet<GameConfig>() : nullptr;
  const float uiScale = cfg != nullptr ? octarine::ui::ResolveUiScale(*cfg) : 1.0f;

  std::ostringstream ss;
  ss << "{\n";
  ss << R"(  "active": )" << (IsActive() ? "true" : "false") << ",\n";
  ss << R"(  "source": ")" << EscapeJsonString(active_source_) << "\",\n";
  ss << R"(  "active_preset": ")" << EscapeJsonString(active_mock_preset_) << "\",\n";
  ss << R"(  "mock_source": ")" << MockSourceModeName(mock_source_mode_) << "\",\n";
  ss << R"(  "mock_path": ")" << EscapeJsonString(active_mock_path_) << "\",\n";
  ss << R"(  "mock_modified": )" << (mock_modified_ ? "true" : "false") << ",\n";
  ss << R"(  "last_mock_error": ")" << EscapeJsonString(last_mock_error_) << "\",\n";
  ss << R"(  "viewport": { "width": )" << (cfg != nullptr ? cfg->windowWidth : 0) << R"(, "height": )"
     << (cfg != nullptr ? cfg->windowHeight : 0) << R"(, "ui_scale": )" << uiScale << R"(, "simulated": )"
     << (HasSimulatedViewport() ? "true" : "false") << " },\n";
  if (uiRuntime != nullptr) {
    const auto& diag = uiRuntime->GetDiagnostics();
    ss << R"(  "diagnostics": { "last_parse_ok": )" << (diag.lastParseOk ? "true" : "false") << R"(, "reload_count": )"
       << diag.reloadCount << R"(, "last_error": ")" << EscapeJsonString(diag.lastError) << R"(", "last_error_file": ")"
       << EscapeJsonString(diag.lastErrorFile) << R"(", "last_error_line": )" << diag.lastErrorLine << " },\n";
  }
  AppendJsonContractSection(ss, active_contract_, params, callbacks, fields);
  ss << R"(  "issue_count": )" << issues.size() << ",\n";
  ss << R"(  "issues": )";
  AppendJsonIssuesArray(ss, issues, "  ");
  ss << "\n}\n";
  return ss.str();
}

std::string UIPreviewer::InspectContractJson(const std::string& pathOrId) const {
  if (!pathOrId.empty()) {
    const auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr;
    if (uiRuntime == nullptr) return "{}\n";
    const auto txt =
        (pathOrId.front() == '<') ? std::optional<std::string>(pathOrId) : uiRuntime->ReadTextAsset(pathOrId);
    if (!txt.has_value()) return "{}\n";
    const OxmlParseResult parsed = OxmlParser::Parse(*txt);
    if (!parsed.ok) return "{}\n";
    auto resolveUse = [uiRuntime](const std::string& src) -> std::optional<OxmlDocument> {
      const auto uTxt =
          (!src.empty() && src.front() == '<') ? std::optional<std::string>(src) : uiRuntime->ReadTextAsset(src);
      if (!uTxt.has_value()) return std::nullopt;
      const OxmlParseResult uParsed = OxmlParser::Parse(*uTxt);
      return uParsed.ok ? std::optional<OxmlDocument>(uParsed.document) : std::nullopt;
    };
    const OxmlContract contract = InspectOxmlContract(parsed.document, {}, resolveUse);
    std::vector<MockParamEntry> params;
    for (const auto& p : contract.params) params.push_back({p.name, p.defaultValue, p.defaultValue, false});
    std::vector<MockCallbackEntry> cbs;
    for (const auto& cb : contract.callbacks) cbs.push_back({cb.name, cb.sourceAttr, cb.rawExpr, false});
    std::ostringstream ss;
    ss << "{\n";
    AppendJsonContractSection(ss, contract, params, cbs, {});
    ss << "  \"static\": true\n}\n";
    return ss.str();
  }
  const auto fields = CollectMockFields();
  const auto params = CollectMockParams();
  const auto callbacks = CollectMockCallbacks();
  std::ostringstream ss;
  ss << "{\n";
  AppendJsonContractSection(ss, active_contract_, params, callbacks, fields);
  ss << R"(  "active": )" << (IsActive() ? "true" : "false") << "\n}\n";
  return ss.str();
}

void UIPreviewer::RecordCallbackEvent(const std::string& eventText) {
  if (callback_events_.size() >= kMaxCallbackEvents) {
    callback_events_.erase(callback_events_.begin());
  }
  callback_events_.push_back("#" + std::to_string(++callback_seq_) + " " + eventText);
}

void UIPreviewer::RefreshContractState() {
  active_contract_ = OxmlContract{};
  if (registry_ == nullptr || active_view_id_ == 0) return;
  if (const auto* uiRuntime = registry_->TryGet<UIRuntime>()) {
    static_cast<void>(uiRuntime->InspectMountedViewContract(active_view_id_, active_contract_));
  }
}

std::vector<MockFieldEntry> UIPreviewer::CollectMockFields() const {
  std::vector<MockFieldEntry> out;
  if (!IsActive()) return out;

  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (!TryGetActiveViewContext(registry_, luaPtr, viewTbl, ctx)) return out;

  const bool isAuto = (mock_source_mode_ == MockSourceMode::AutoSeeded);
  std::unordered_set<std::string> seenPaths;

  for (const auto& f : active_contract_.fields) {
    if (f.path.find("[]") != std::string::npos) continue;
    const sol::object val = ctx.valid() ? GetUnwrappedPathValue(ctx, f.path) : sol::object(sol::lua_nil);
    if (seenPaths.insert(f.path).second) {
      out.push_back(BuildMockEntryFromContract(f, f.path, val, isAuto));
    }
    if (f.inferredType != ContractFieldType::Array || !val.is<sol::table>()) continue;
    const sol::table unwrappedArr = UnwrapObservableShallow(val.as<sol::table>());
    const std::size_t n = unwrappedArr.size();
    for (std::size_t idx = 1; idx <= n; ++idx) {
      EmitContractArrayItemEntries(active_contract_, f, unwrappedArr, idx, isAuto, seenPaths, out);
    }
  }

  if (ctx.valid()) {
    AppendExtraLiveFields(ctx, "", seenPaths, isAuto, out);
  }
  return out;
}

std::vector<MockParamEntry> UIPreviewer::CollectMockParams() const {
  std::vector<MockParamEntry> out;
  if (!IsActive() || registry_ == nullptr) return out;
  const auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr) return out;

  const sol::object paramsObj = uiRuntime->GetMountedViewParams(active_view_id_);
  const sol::table paramsTbl = paramsObj.is<sol::table>() ? paramsObj.as<sol::table>() : sol::table{};

  for (const auto& p : active_contract_.params) {
    std::string cur = p.defaultValue;
    if (paramsTbl.valid()) {
      const sol::object val = paramsTbl.get<sol::object>(p.name);
      if (val.is<std::string>()) {
        cur = val.as<std::string>();
      } else if (val.is<double>()) {
        std::ostringstream oss;
        oss << val.as<double>();
        cur = oss.str();
      } else if (val.is<bool>()) {
        cur = val.as<bool>() ? "true" : "false";
      }
    }
    out.push_back(MockParamEntry{
        .name = p.name,
        .defaultValue = p.defaultValue,
        .currentValue = cur,
        .isOverridden = (cur != p.defaultValue),
    });
  }
  return out;
}

std::vector<MockCallbackEntry> UIPreviewer::CollectMockCallbacks() const {
  std::vector<MockCallbackEntry> out;
  if (!IsActive() || registry_ == nullptr) return out;
  const auto* uiRuntime = registry_->TryGet<UIRuntime>();
  sol::table rawCtrl;
  if (uiRuntime != nullptr && uiRuntime->GetLua() != nullptr) {
    const sol::object obj =
        GetPinnedLuaRegistryObject(uiRuntime->GetLua()->lua_state(), "__octarine_preview_controller");
    if (obj.is<sol::table>()) rawCtrl = obj.as<sol::table>();
  }

  for (const auto& cb : active_contract_.callbacks) {
    bool bound = false;
    if (rawCtrl.valid()) {
      const sol::object fn = rawCtrl.get<sol::object>(cb.name);
      bound = fn.is<sol::protected_function>();
    }
    out.push_back(MockCallbackEntry{
        .name = cb.name,
        .sourceAttr = cb.sourceAttr,
        .rawExpr = cb.rawExpr,
        .hasControllerHandler = bound,
    });
  }
  return out;
}

bool UIPreviewer::SetMockFieldObject(const std::string& path, const sol::object& value) {
  if (!IsActive() || registry_ == nullptr || path.empty()) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr) return false;

  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (!TryGetActiveViewContext(registry_, luaPtr, viewTbl, ctx)) return false;

  sol::table mutableCtx = EnsureMutableContextTable(*uiRuntime, *luaPtr, viewTbl, ctx);
  if (!SetLivePathValue(*luaPtr, mutableCtx, path, value)) return false;
  RefreshViewIfPlainContext(viewTbl, mutableCtx);
  mock_modified_ = true;
  return true;
}

bool UIPreviewer::SetMockFieldString(const std::string& path, const std::string& value) {
  if (!IsActive() || registry_ == nullptr) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;
  return SetMockFieldObject(path, sol::make_object(*uiRuntime->GetLua(), value));
}

bool UIPreviewer::SetMockFieldNumber(const std::string& path, const double value) {
  if (!IsActive() || registry_ == nullptr) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;
  return SetMockFieldObject(path, sol::make_object(*uiRuntime->GetLua(), value));
}

bool UIPreviewer::SetMockFieldBool(const std::string& path, const bool value) {
  if (!IsActive() || registry_ == nullptr) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;
  return SetMockFieldObject(path, sol::make_object(*uiRuntime->GetLua(), value));
}

bool UIPreviewer::ResizeMockArray(const std::string& path, const int newLength) {
  if (!IsActive() || registry_ == nullptr || path.empty()) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr) return false;

  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (!TryGetActiveViewContext(registry_, luaPtr, viewTbl, ctx)) return false;

  sol::table mutableCtx = EnsureMutableContextTable(*uiRuntime, *luaPtr, viewTbl, ctx);
  const int targetLen = std::max(0, newLength);

  sol::table newArr = luaPtr->create_table();
  const sol::object existingObj = GetUnwrappedPathValue(mutableCtx, path);
  int copied = 0;
  if (existingObj.is<sol::table>()) {
    const sol::table oldRaw = UnwrapObservableShallow(existingObj.as<sol::table>());
    const int oldLen = static_cast<int>(oldRaw.size());
    copied = std::min(oldLen, targetLen);
    for (int i = 1; i <= copied; ++i) {
      const sol::object elem = oldRaw.get<sol::object>(i);
      newArr[i] = UnwrapObservableDataDeep(*luaPtr, elem);
    }
  }
  for (int i = copied + 1; i <= targetLen; ++i) {
    newArr[i] = BuildDefaultArrayElement(*luaPtr, registry_, active_contract_, path, i);
  }
  if (!SetLivePathValue(*luaPtr, mutableCtx, path, newArr)) return false;
  RefreshViewIfPlainContext(viewTbl, mutableCtx);
  mock_modified_ = true;
  return true;
}

bool UIPreviewer::IsContractFieldUnset(const ContractField& f, const std::vector<MockFieldEntry>& liveFields) {
  const std::size_t br = f.path.find("[]");
  if (br == std::string::npos) {
    for (const auto& mf : liveFields) {
      if (mf.path == f.path) return mf.isUnset;
    }
    return false;
  }
  const std::string arrRoot = f.path.substr(0, br);
  for (const auto& mf : liveFields) {
    if (mf.path == arrRoot && mf.isUnset) return true;
    if (NormalizeArrayContractPath(mf.path) == f.path && mf.isUnset) return true;
  }
  return false;
}

bool UIPreviewer::SeedSingleContractField(const std::string& path) {
  if (!IsActive() || registry_ == nullptr || path.empty()) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;
  const ContractField* f = FindContractFieldByPath(active_contract_, path);
  if (f == nullptr) return false;

  sol::state& lua = *uiRuntime->GetLua();
  if (f->inferredType == ContractFieldType::Array) {
    return SetMockFieldObject(path, BuildSeededArrayForPattern(lua, registry_, active_contract_, path, 3, 0));
  }
  return SetMockFieldObject(path, BuildDefaultScalarValue(lua, registry_, *f));
}

bool UIPreviewer::SeedMissingContractFields() {
  if (!IsActive() || registry_ == nullptr) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr) return false;

  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (!TryGetActiveViewContext(registry_, luaPtr, viewTbl, ctx)) return false;

  sol::table rawCopy = luaPtr->create_table();
  if (ctx.valid()) {
    const sol::object unwrapped = UnwrapObservableDataDeep(*luaPtr, ctx);
    if (unwrapped.is<sol::table>()) rawCopy = unwrapped.as<sol::table>();
  }
  SeedAutoFieldsIntoTable(*luaPtr, registry_, rawCopy, active_contract_);

  sol::table nextObs = uiRuntime->CreateObservable(rawCopy);
  const sol::protected_function setCtx = viewTbl.get<sol::protected_function>("set_context");
  if (setCtx.valid()) {
    setCtx(viewTbl, nextObs);
  }
  mock_modified_ = true;
  return true;
}

bool UIPreviewer::SetMockParam(const std::string& name, const std::string& value) {
  if (!IsActive() || registry_ == nullptr || name.empty()) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || !uiRuntime->SetMountedViewParam(active_view_id_, name, value)) return false;
  mock_modified_ = true;
  return true;
}

bool UIPreviewer::TriggerMockCallback(const std::string& name) {
  if (!IsActive() || registry_ == nullptr || name.empty()) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  return uiRuntime != nullptr && uiRuntime->InvokeMountedViewController(active_view_id_, name);
}

bool UIPreviewer::LoadMockScript(const std::string& mockPath) {
  if (!IsActive() || registry_ == nullptr || is_inline_ || mockPath.empty()) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;

  const std::string resolved = ResolveMockDiskPath(registry_, uiRuntime, mockPath);
  const std::string reqPreset =
      (!active_mock_preset_.empty() && active_mock_preset_.front() != '@') ? active_mock_preset_ : "";
  sol::table mockOpts = uiRuntime->GetLua()->create_table();
  sol::table rawCtrl;
  sol::table rawRoot;
  std::string resolvedKey;
  bool hasCtrl = false;
  std::string err;
  if (!LoadSandboxedMockScript(*uiRuntime, resolved, reqPreset, mockOpts, rawCtrl, rawRoot, resolvedKey, hasCtrl,
                               err)) {
    last_mock_error_ = err;
    Logger::Error("[ui-preview] mock script load error: " + err);
    return false;
  }

  const MockSourceMode nextMode =
      AreSameDiskPath(resolved, target_sidecar_path_) ? MockSourceMode::SidecarPreview : MockSourceMode::ExplicitLua;
  return MountResolvedPreview(active_source_, mockOpts, "", false, nextMode, resolved, target_sidecar_path_, rawRoot,
                              resolvedKey);
}

bool UIPreviewer::ResetToAutoSeeded() {
  if (!IsActive() || registry_ == nullptr) return false;
  if (is_inline_) {
    return MountInlineXml(inline_xml_, inline_oss_);
  }
  return MountResolvedPreview(active_source_, sol::nullopt, "", false, MockSourceMode::AutoSeeded, "",
                              target_sidecar_path_, sol::nullopt);
}

namespace octarine::ui_preview_detail {
void AppendCallbackCommentHeader(std::ostringstream& ss, const OxmlContract& contract) {
  if (contract.callbacks.empty()) return;
  ss << "-- Optional controller callbacks: ";
  for (std::size_t i = 0; i < contract.callbacks.size(); ++i) {
    if (i > 0) ss << ", ";
    ss << contract.callbacks[i].name;
  }
  ss << "\n";
}
}  // namespace octarine::ui_preview_detail

std::string UIPreviewer::ExportMockLuaScript() const {
  if (!IsActive() || registry_ == nullptr) return "";
  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (!TryGetActiveViewContext(registry_, luaPtr, viewTbl, ctx)) return "";

  const sol::table docTbl = octarine::ui_preview_detail::BuildLiveDocSnapshot(*luaPtr, *this, ctx);
  return ExportMockLuaScriptForDoc(docTbl, "", "", /*promoteFlatIfNeeded=*/false);
}

bool UIPreviewer::CanPreserveLiveControllerOnSave(const std::string& destPath) const {
  if (!has_live_mock_controller_) return true;
  auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr;
  const std::string& sourcePath =
      !active_mock_path_.empty() ? active_mock_path_ : (!destPath.empty() ? destPath : target_sidecar_path_);
  return octarine::ui_preview_detail::CanPreserveControllerFromSource(uiRuntime, sourcePath);
}

bool UIPreviewer::SaveMockSidecar(const std::string& path, const bool force) {
  if (!IsActive() || registry_ == nullptr) return false;
  const std::string dest =
      !path.empty() ? path : (!target_sidecar_path_.empty() ? target_sidecar_path_ : active_mock_path_);
  if (dest.empty()) {
    last_mock_error_ = "No disk path available for .preview.lua sidecar";
    return false;
  }
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  sol::state* luaPtr = nullptr;
  sol::table viewTbl;
  sol::table ctx;
  if (uiRuntime == nullptr || !TryGetActiveViewContext(registry_, luaPtr, viewTbl, ctx)) {
    last_mock_error_ = "Failed to serialize mock state to Lua";
    return false;
  }

  bool hasPresets = false;
  std::string serializedBody;
  const sol::table docTbl = octarine::ui_preview_detail::BuildLiveDocSnapshot(*luaPtr, *this, ctx);
  if (ExportMockLuaScriptForDoc(docTbl, "", "", /*promoteFlatIfNeeded=*/false, &hasPresets, &serializedBody).empty()) {
    last_mock_error_ = "Failed to serialize mock state to Lua";
    return false;
  }

  const std::string& sourcePath = !active_mock_path_.empty() ? active_mock_path_ : dest;
  const std::string reqPreset = (!active_mock_preset_.empty() && active_mock_preset_.front() == '@')
                                    ? active_mock_preset_.substr(1)
                                    : active_mock_preset_;
  std::string scriptText;
  std::string safeErr;
  if (!octarine::ui_preview_detail::BuildSafeMockScriptForSave(*uiRuntime, serializedBody, hasPresets, active_contract_,
                                                               sourcePath, reqPreset, has_live_mock_controller_, force,
                                                               scriptText, safeErr)) {
    last_mock_error_ = safeErr;
    return false;
  }

  std::string writeErr;
  if (!TryWriteTextFile(dest, scriptText, writeErr)) {
    last_mock_error_ = writeErr;
    return false;
  }

  active_mock_path_ = dest;
  RecordMockMtime(dest, last_mock_write_time_, has_mock_write_time_);
  if (AreSameDiskPath(dest, target_sidecar_path_)) mock_source_mode_ = MockSourceMode::SidecarPreview;
  has_live_mock_controller_ = false;
  RefreshSidecarMetadataAfterSave(dest);
  mock_modified_ = false;
  last_mock_error_.clear();
  return true;
}

void UIPreviewer::RefreshSidecarMetadataAfterSave(const std::string& dest) {
  auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr;
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return;
  sol::state& lua = *uiRuntime->GetLua();
  sol::table nextOpts = lua.create_table();
  sol::table nextCtrl;
  sol::table nextRoot;
  std::string resolvedKey;
  bool hasCtrl = false;
  std::string loadErr;
  const std::string reqPreset = (!active_mock_preset_.empty() && active_mock_preset_.front() == '@')
                                    ? active_mock_preset_.substr(1)
                                    : active_mock_preset_;
  if (LoadSandboxedMockScript(*uiRuntime, dest, reqPreset, nextOpts, nextCtrl, nextRoot, resolvedKey, hasCtrl,
                              loadErr)) {
    PinLuaRegistryObject(lua.lua_state(), "__octarine_preview_raw_mock", nextRoot);
    PinLuaRegistryObject(lua.lua_state(), "__octarine_preview_controller", nextCtrl);
    has_live_mock_controller_ = hasCtrl;
    if (!resolvedKey.empty()) active_mock_preset_ = resolvedKey;
  }
}

void UIPreviewer::PollMockHotReload(const float deltaTime, const float pollIntervalSeconds) {
  if (!IsActive() || registry_ == nullptr) return;
  SyncActivePreview();

  if (active_mock_path_.empty() || is_inline_) return;
  if (pollIntervalSeconds > 0.0f) {
    mock_hot_reload_accum_ += deltaTime;
    if (mock_hot_reload_accum_ < pollIntervalSeconds) return;
    mock_hot_reload_accum_ = 0.0f;
  }

  std::error_code ec;
  if (!fs::exists(active_mock_path_, ec)) return;
  const auto mtime = fs::last_write_time(active_mock_path_, ec);
  if (ec) return;
  if (!has_mock_write_time_ || mtime != last_mock_write_time_) {
    last_mock_write_time_ = mtime;
    has_mock_write_time_ = true;
    static_cast<void>(LoadMockScript(active_mock_path_));
  }
}

#endif  // !OCTARINE_SHIPPED
