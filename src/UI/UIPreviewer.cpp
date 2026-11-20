#include "UI/UIPreviewer.h"

#ifndef OCTARINE_SHIPPED

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string_view>

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
#include "Renderer/RenderQueue.h"
#include "Renderer/SpriteRenderCache.h"
#include "Systems/RenderTextSystem.h"
#include "Systems/RenderUIPrimitiveSystem.h"
#include "Systems/RenderUISpriteSystem.h"
#include "Systems/TransformSystem.h"
#include "Systems/UILayoutMetrics.h"
#include "Systems/UILayoutSystem.h"
#include "UI/UIRuntime.h"

namespace {
namespace fs = std::filesystem;

constexpr std::size_t kMaxCallbackEvents = 32;
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

std::string ReadFileOrAssetText(UIRuntime* uiRuntime, const std::string& pathOrId, std::string& outErr) {
  if (uiRuntime != nullptr) {
    if (const auto loaded = uiRuntime->ReadTextAsset(pathOrId); loaded.has_value()) {
      return *loaded;
    }
  }
  outErr = "failed to open '" + pathOrId + "'";
  return {};
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
  if (L == nullptr) return;
  if (viewTable.valid()) {
    viewTable.push(L);
  } else {
    lua_pushnil(L);
  }
  lua_setfield(L, LUA_REGISTRYINDEX, "__octarine_preview_pin");
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

void ApplyMockTableFields(UIRuntime& uiRuntime, const sol::table& tbl, sol::table& outOpts) {
  const sol::object ctxField = tbl["context"];
  const sol::object ctrlField = tbl["controller"];
  const sol::object paramsField = tbl["params"];
  const sol::object stylesField = tbl["styles"];
  const bool structured = ctxField.is<sol::table>() || ctrlField.is<sol::table>() || paramsField.is<sol::table>();
  if (!structured) {
    outOpts.set("context", uiRuntime.CreateObservable(tbl));
    return;
  }
  if (ctxField.is<sol::table>()) outOpts.set("context", uiRuntime.CreateObservable(ctxField.as<sol::table>()));
  if (ctrlField.is<sol::table>()) outOpts.set("controller", ctrlField);
  if (paramsField.is<sol::table>()) outOpts.set("params", paramsField);
  if (stylesField.is<sol::table>()) outOpts.set("styles", stylesField);
}

}  // namespace

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
    : registry_(registry), paused_systems_(std::make_unique<PausedRenderSystems>()) {}

UIPreviewer::~UIPreviewer() = default;
UIPreviewer::UIPreviewer(UIPreviewer&&) noexcept = default;
UIPreviewer& UIPreviewer::operator=(UIPreviewer&&) noexcept = default;

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

sol::table UIPreviewer::BuildFallbackController(const sol::optional<sol::table>& userController) {
  auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr;
  sol::state& lua = *uiRuntime->GetLua();
  sol::table wrapper = lua.create_table();
  sol::table backing = userController.value_or(lua.create_table());
  sol::table meta = lua.create_table();

  UIPreviewer* selfPtr = this;
  meta.set_function("__index", [&lua, backing, selfPtr](const sol::table&, const std::string& key) -> sol::object {
    if (const sol::object existing = LookupUserControllerMember(backing, key); existing != sol::lua_nil) {
      return existing;
    }
    if (key == "on_mount" || key == "on_unmount") {
      return sol::lua_nil;
    }
    return sol::make_object(lua, [selfPtr, key](const sol::table&, const sol::object& arg) {
      const std::string msg = key + "(" + FormatFallbackArg(arg) + ")";
      if (selfPtr != nullptr) {
        selfPtr->RecordCallbackEvent(msg);
      }
      Logger::Info("[ui-preview] callback triggered: " + msg);
    });
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
  sol::state& lua = *uiRuntime->GetLua();

  std::string err;
  const std::string scriptText = ReadFileOrAssetText(uiRuntime, mockPath, err);
  if (scriptText.empty()) {
    Logger::Error("[ui-preview] mock script load error: " + err);
    return false;
  }

  const sol::protected_function_result res = lua.safe_script(scriptText, sol::script_pass_on_error, "@" + mockPath);
  if (!res.valid()) {
    const sol::error luaErr = res;
    Logger::Error("[ui-preview] mock script error in '" + mockPath + "': " + luaErr.what());
    return false;
  }
  if (res.return_count() > 0 && res[0].is<sol::table>()) {
    ApplyMockTableFields(*uiRuntime, res[0].as<sol::table>(), outOpts);
  }
  return true;
}

bool UIPreviewer::MountPreview(const std::string& pathOrId, const sol::optional<sol::table>& opts) {
  if (registry_ == nullptr) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;

  // Copy before UnmountPreview clears member strings in case caller aliases active_source_.
  const std::string sourceCopy = pathOrId;
  RefreshCatalogViews();
  const bool wasIsolated = isolated_;
  UnmountPreview(/*restoreIsolationState=*/false);

  const std::string resolvedSource = MatchCatalogEntryForPreview(catalog_views_, sourceCopy);
  const sol::optional<sol::table> mountOpts = PrepareMountOptions(opts, /*inlineOss=*/"");
  sol::state& lua = *uiRuntime->GetLua();
  const sol::object result = uiRuntime->Mount(sol::make_object(lua, resolvedSource), mountOpts);
  if (!result.is<sol::table>()) {
    if (wasIsolated) {
      RestoreBackgroundIsolation();
    }
    return false;
  }

  const sol::table mountedTable = result.as<sol::table>();
  PinPreviewTableInLuaRegistry(lua.lua_state(), mountedTable);
  active_root_ = ReadRootEntityField(mountedTable);
  active_view_id_ = uiRuntime->FindViewIdByRoot(active_root_).value_or(0);
  active_source_ = resolvedSource;
  active_mock_path_.clear();
  is_inline_ = false;
  inline_xml_.clear();
  inline_oss_.clear();
  captured_orig_base_layer_ = false;

  if (wasIsolated) {
    muted_view_states_ = uiRuntime->SetNonPreviewViewsInert(active_view_id_, true);
  }
  SyncPreviewLayerOrder();
  return true;
}

bool UIPreviewer::MountPreviewWithMock(const std::string& pathOrId, const std::string& mockScriptPath) {
  if (registry_ == nullptr) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;
  if (mockScriptPath.empty()) {
    return MountPreview(pathOrId);
  }

  const std::string sourceCopy = pathOrId;
  const std::string mockCopy = mockScriptPath;
  sol::table mockOpts = uiRuntime->GetLua()->create_table();
  if (!LoadMockScriptOptions(mockCopy, mockOpts)) {
    return MountPreview(sourceCopy);
  }
  const bool ok = MountPreview(sourceCopy, mockOpts);
  if (ok) {
    active_mock_path_ = mockCopy;
  }
  return ok;
}

bool UIPreviewer::MountInlineXml(const std::string& xmlSource, const std::string& ossSource) {
  if (registry_ == nullptr || xmlSource.empty()) return false;
  auto* uiRuntime = registry_->TryGet<UIRuntime>();
  if (uiRuntime == nullptr || uiRuntime->GetLua() == nullptr) return false;

  // Copy before UnmountPreview clears inline_xml_/inline_oss_ in case caller aliases them (e.g. reload).
  const std::string xmlCopy = xmlSource;
  const std::string ossCopy = ossSource;
  const bool wasIsolated = isolated_;
  UnmountPreview(/*restoreIsolationState=*/false);

  const sol::optional<sol::table> mountOpts = PrepareMountOptions(sol::nullopt, ossCopy);
  sol::state& lua = *uiRuntime->GetLua();
  const sol::object result = uiRuntime->Mount(sol::make_object(lua, xmlCopy), mountOpts);
  if (!result.is<sol::table>()) {
    if (wasIsolated) {
      RestoreBackgroundIsolation();
    }
    return false;
  }

  const sol::table mountedTable = result.as<sol::table>();
  PinPreviewTableInLuaRegistry(lua.lua_state(), mountedTable);
  active_root_ = ReadRootEntityField(mountedTable);
  active_view_id_ = uiRuntime->FindViewIdByRoot(active_root_).value_or(0);
  active_source_ = "<inline>";
  active_mock_path_.clear();
  is_inline_ = true;
  inline_xml_ = xmlCopy;
  inline_oss_ = ossCopy;
  captured_orig_base_layer_ = false;

  if (wasIsolated) {
    muted_view_states_ = uiRuntime->SetNonPreviewViewsInert(active_view_id_, true);
  }
  SyncPreviewLayerOrder();
  return true;
}

bool UIPreviewer::ReloadActivePreview() {
  if (registry_ == nullptr || !IsActive()) return false;
  if (!active_mock_path_.empty() && !is_inline_) {
    const std::string src = active_source_;
    const std::string mock = active_mock_path_;
    return MountPreviewWithMock(src, mock);
  }
  if (is_inline_) {
    return MountInlineXml(inline_xml_, inline_oss_);
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
      }
    }
  }
  active_root_ = Entity{};
  active_view_id_ = 0;
  captured_orig_base_layer_ = false;
  active_source_.clear();
  active_mock_path_.clear();
  is_inline_ = false;
  inline_xml_.clear();
  inline_oss_.clear();

  if (restoreIsolationState) {
    isolated_ = false;
    RestoreBackgroundIsolation();
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
  const auto* uiRuntime = registry_ != nullptr ? registry_->TryGet<UIRuntime>() : nullptr;
  const auto* cfg = registry_ != nullptr ? registry_->TryGet<GameConfig>() : nullptr;

  std::ostringstream ss;
  ss << "{\n";
  ss << R"(  "active": )" << (IsActive() ? "true" : "false") << ",\n";
  ss << R"(  "source": ")" << EscapeJsonString(active_source_) << "\",\n";
  ss << R"(  "viewport": { "width": )" << (cfg != nullptr ? cfg->windowWidth : 0) << R"(, "height": )"
     << (cfg != nullptr ? cfg->windowHeight : 0) << " },\n";
  if (uiRuntime != nullptr) {
    const auto& diag = uiRuntime->GetDiagnostics();
    ss << R"(  "diagnostics": { "last_parse_ok": )" << (diag.lastParseOk ? "true" : "false") << R"(, "reload_count": )"
       << diag.reloadCount << R"(, "last_error": ")" << EscapeJsonString(diag.lastError) << R"(", "last_error_file": ")"
       << EscapeJsonString(diag.lastErrorFile) << R"(", "last_error_line": )" << diag.lastErrorLine << " },\n";
  }
  ss << R"(  "issue_count": )" << issues.size() << ",\n";
  ss << R"(  "issues": [)";
  for (std::size_t i = 0; i < issues.size(); ++i) {
    const auto& it = issues[i];
    if (i > 0) ss << ",";
    ss << "\n    {\"code\": \"" << EscapeJsonString(it.code) << R"(", "severity": ")" << EscapeJsonString(it.severity)
       << R"(", "entity": )" << it.entity.GetId() << R"(, "label": ")" << EscapeJsonString(it.label)
       << R"(", "message": ")" << EscapeJsonString(it.message) << R"(", "rect": {"left": )" << it.rect.left
       << R"(, "top": )" << it.rect.top << R"(, "right": )" << it.rect.right << R"(, "bottom": )" << it.rect.bottom
       << "}}";
  }
  ss << (issues.empty() ? "]\n" : "\n  ]\n") << "}\n";
  return ss.str();
}

void UIPreviewer::RecordCallbackEvent(const std::string& eventText) {
  if (callback_events_.size() >= kMaxCallbackEvents) {
    callback_events_.erase(callback_events_.begin());
  }
  callback_events_.push_back(eventText);
}

#endif  // !OCTARINE_SHIPPED
