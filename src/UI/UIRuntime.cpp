#include "UI/UIRuntime.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>

#include "AssetManager/AssetManager.h"
#include "Components/CapsulePrimitiveComponent.h"
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
#include "Components/UILayoutBoxComponent.h"
#include "Components/UINineSliceComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/UIScrollComponent.h"
#include "Components/UIStyleStateComponent.h"
#include "Components/UIZIndexComponent.h"
#include "ECS/Registry.h"
#include "General/Logger.h"

namespace {

namespace fs = std::filesystem;

std::string_view TrimSv(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) {
    sv.remove_suffix(1);
  }
  return sv;
}

bool ParseBoolStr(std::string_view sv, bool fallback = false) {
  const std::string_view t = TrimSv(sv);
  if (t == "true" || t == "1" || t == "yes") return true;
  if (t == "false" || t == "0" || t == "no") return false;
  return fallback;
}

bool IsSimpleDotPath(std::string_view sv) {
  for (const char c : sv) {
    const auto uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) == 0 && c != '_' && c != '.') {
      return false;
    }
  }
  return !sv.empty();
}

sol::object GetPathValue(const sol::table& root, std::string_view path) {
  if (!root.valid()) return sol::lua_nil;
  sol::object current = root;
  std::size_t start = 0;
  while (start <= path.size() && current.is<sol::table>()) {
    const std::size_t dot = path.find('.', start);
    const std::string key(path.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
    sol::table tbl = current.as<sol::table>();
    current = tbl[key];
    if (dot == std::string_view::npos) break;
    start = dot + 1;
  }
  return current;
}

void SetPathValue(sol::table& root, std::string_view path, const sol::object& value) {
  if (!root.valid() || path.empty()) return;
  sol::table current = root;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t dot = path.find('.', start);
    if (dot == std::string_view::npos) {
      const std::string finalKey(path.substr(start));
      current[finalKey] = value;
      return;
    }
    const std::string segment(path.substr(start, dot - start));
    sol::object nextObj = current[segment];
    if (!nextObj.is<sol::table>()) {
      return;
    }
    current = nextObj.as<sol::table>();
    start = dot + 1;
  }
}

std::string NormalizeLuaExpr(std::string_view expr) {
  std::string out;
  out.reserve(expr.size() + 4);
  for (std::size_t i = 0; i < expr.size(); ++i) {
    if (expr[i] == '!' && (i + 1 >= expr.size() || expr[i + 1] != '=')) {
      out += "not ";
    } else if (i + 1 < expr.size() && expr[i] == '!' && expr[i + 1] == '=') {
      out += "~=";
      ++i;
    } else if (i + 1 < expr.size() && expr[i] == '&' && expr[i + 1] == '&') {
      out += " and ";
      ++i;
    } else if (i + 1 < expr.size() && expr[i] == '|' && expr[i + 1] == '|') {
      out += " or ";
      ++i;
    } else {
      out.push_back(expr[i]);
    }
  }
  return out;
}

sol::object EvalBindingExpr(sol::state& lua, std::string_view rawExpr, const sol::table& scope) {
  const std::string_view trimmed = TrimSv(rawExpr);
  if (trimmed.empty()) return sol::lua_nil;
  if (IsSimpleDotPath(trimmed)) {
    return GetPathValue(scope, trimmed);
  }

  const std::string luaExpr = "return (" + NormalizeLuaExpr(trimmed) + ")";
  sol::load_result chunk = lua.load(luaExpr);
  if (!chunk.valid()) {
    return sol::lua_nil;
  }
  sol::protected_function fn = chunk;
  sol::environment env(lua, sol::create);
  sol::table meta = lua.create_table();
  meta.set_function("__index", [scope, &lua](const sol::table&, const std::string& key) -> sol::object {
    if (scope.valid()) {
      sol::object v = scope[key];
      if (v.valid() && v != sol::lua_nil) return v;
    }
    return lua[key];
  });
  env[sol::metatable_key] = meta;
  sol::set_environment(env, fn);

  const sol::protected_function_result res = fn();
  if (!res.valid() || res.return_count() == 0) {
    return sol::lua_nil;
  }
  return res[0];
}

std::string ObjectToString(const sol::object& obj) {
  if (!obj.valid() || obj == sol::lua_nil) return "";
  if (obj.is<std::string>()) return obj.as<std::string>();
  if (obj.is<bool>()) return obj.as<bool>() ? "true" : "false";
  if (obj.is<double>()) {
    const double d = obj.as<double>();
    if (std::floor(d) == d && std::abs(d) < 1e12) {
      return std::to_string(static_cast<long long>(d));
    }
    std::ostringstream oss;
    oss << d;
    return oss.str();
  }
  return "";
}

bool ObjectToBool(const sol::object& obj) {
  if (!obj.valid() || obj == sol::lua_nil) return false;
  if (obj.is<bool>()) return obj.as<bool>();
  if (obj.is<double>()) return obj.as<double>() != 0.0;
  if (obj.is<std::string>()) {
    const std::string s = obj.as<std::string>();
    return !s.empty() && s != "false" && s != "0";
  }
  return true;
}

float ObjectToFloat(const sol::object& obj, float fallback = 0.0f) {
  if (!obj.valid() || obj == sol::lua_nil) return fallback;
  if (obj.is<double>()) return static_cast<float>(obj.as<double>());
  if (obj.is<std::string>()) {
    float v = fallback;
    OssParser::ParseFloat(obj.as<std::string>(), v);
    return v;
  }
  return fallback;
}

std::string EvalInterpolatedString(sol::state& lua, std::string_view raw, const sol::table& scope) {
  std::string out;
  out.reserve(raw.size());
  std::size_t i = 0;
  while (i < raw.size()) {
    const std::size_t open = raw.find('{', i);
    if (open == std::string_view::npos) {
      out.append(raw.substr(i));
      break;
    }
    const std::size_t close = raw.find('}', open + 1);
    if (close == std::string_view::npos) {
      out.append(raw.substr(i));
      break;
    }
    out.append(raw.substr(i, open - i));
    const std::string_view expr = raw.substr(open + 1, close - open - 1);
    out += ObjectToString(EvalBindingExpr(lua, expr, scope));
    i = close + 1;
  }
  return out;
}

FlexAlign ParseFlexAlignStr(std::string_view sv, FlexAlign fallback = FlexAlign::Start) {
  const std::string_view s = TrimSv(sv);
  if (s == "center") return FlexAlign::Center;
  if (s == "end" || s == "flex-end") return FlexAlign::End;
  if (s == "stretch") return FlexAlign::Stretch;
  if (s == "space-between" || s == "space_between") return FlexAlign::SpaceBetween;
  if (s == "space-around" || s == "space_around") return FlexAlign::SpaceAround;
  if (s == "start" || s == "flex-start") return FlexAlign::Start;
  return fallback;
}

FlexDirection ParseFlexDirStr(std::string_view sv, FlexDirection fallback = FlexDirection::Column) {
  const std::string_view s = TrimSv(sv);
  if (s == "row") return FlexDirection::Row;
  if (s == "column" || s == "col") return FlexDirection::Column;
  return fallback;
}

void ApplyDimensionProp(const std::map<std::string, std::string>& props, const std::string& key, float& outVal,
                        UIUnit& outUnit) {
  const auto it = props.find(key);
  if (it != props.end()) {
    OssParser::ParseDimension(it->second, outVal, outUnit);
  }
}

void ApplyFloatProp(const std::map<std::string, std::string>& props, const std::string& key, float& target) {
  const auto it = props.find(key);
  if (it != props.end()) {
    OssParser::ParseFloat(it->second, target);
  }
}

UILayoutBoxComponent BuildLayoutBoxFromStyle(const std::map<std::string, std::string>& props) {
  UILayoutBoxComponent box;
  ApplyDimensionProp(props, "width", box.width, box.widthUnit);
  ApplyDimensionProp(props, "height", box.height, box.heightUnit);
  ApplyDimensionProp(props, "min-width", box.minWidth, box.minWidthUnit);
  ApplyDimensionProp(props, "min-height", box.minHeight, box.minHeightUnit);
  ApplyDimensionProp(props, "max-width", box.maxWidth, box.maxWidthUnit);
  ApplyDimensionProp(props, "max-height", box.maxHeight, box.maxHeightUnit);

  if (const auto it = props.find("margin"); it != props.end()) {
    OssParser::ParseEdges(it->second, box.marginTop, box.marginRight, box.marginBottom, box.marginLeft);
  }
  ApplyFloatProp(props, "margin-top", box.marginTop);
  ApplyFloatProp(props, "margin-right", box.marginRight);
  ApplyFloatProp(props, "margin-bottom", box.marginBottom);
  ApplyFloatProp(props, "margin-left", box.marginLeft);

  if (const auto it = props.find("flex-grow"); it != props.end()) {
    float grow = 0.0f;
    if (OssParser::ParseFloat(it->second, grow) && grow > 0.0f) {
      if (box.widthUnit == UIUnit::Auto) {
        box.SetWidthFr(grow);
      }
      if (box.heightUnit == UIUnit::Auto) {
        box.SetHeightFr(grow);
      }
    }
  }
  return box;
}

UIFlexComponent BuildFlexFromStyle(const std::map<std::string, std::string>& props, FlexDirection defaultDir) {
  UIFlexComponent flex;
  flex.direction = defaultDir;
  if (const auto it = props.find("flex-direction"); it != props.end()) {
    flex.direction = ParseFlexDirStr(it->second, defaultDir);
  } else if (const auto dirIt = props.find("direction"); dirIt != props.end()) {
    flex.direction = ParseFlexDirStr(dirIt->second, defaultDir);
  }

  if (const auto it = props.find("gap"); it != props.end()) {
    OssParser::ParseFloat(it->second, flex.gap);
  } else if (const auto spIt = props.find("spacing"); spIt != props.end()) {
    OssParser::ParseFloat(spIt->second, flex.gap);
  }

  if (const auto it = props.find("padding"); it != props.end()) {
    OssParser::ParseEdges(it->second, flex.paddingTop, flex.paddingRight, flex.paddingBottom, flex.paddingLeft);
  }
  ApplyFloatProp(props, "padding-top", flex.paddingTop);
  ApplyFloatProp(props, "padding-right", flex.paddingRight);
  ApplyFloatProp(props, "padding-bottom", flex.paddingBottom);
  ApplyFloatProp(props, "padding-left", flex.paddingLeft);

  if (const auto it = props.find("align-items"); it != props.end()) {
    flex.align = ParseFlexAlignStr(it->second, flex.align);
  } else if (const auto alIt = props.find("align"); alIt != props.end()) {
    flex.align = ParseFlexAlignStr(alIt->second, flex.align);
  }

  if (const auto it = props.find("justify-content"); it != props.end()) {
    flex.justify = ParseFlexAlignStr(it->second, flex.justify);
  } else if (const auto jIt = props.find("justify"); jIt != props.end()) {
    flex.justify = ParseFlexAlignStr(jIt->second, flex.justify);
  }
  return flex;
}

bool ExtractBgColor(const std::map<std::string, std::string>& props, octarine::Color& outColor) {
  for (const char* key : {"background-color", "bg-color", "background"}) {
    if (const auto it = props.find(key); it != props.end()) {
      if (OssParser::ParseColor(it->second, outColor)) return true;
    }
  }
  return false;
}

bool ExtractTextColor(const std::map<std::string, std::string>& props, octarine::Color& outColor) {
  for (const char* key : {"text-color", "color"}) {
    if (const auto it = props.find(key); it != props.end()) {
      if (OssParser::ParseColor(it->second, outColor)) return true;
    }
  }
  return false;
}

void SetEntityChecked(Registry* reg, Entity entity, bool checked) {
  if (!reg->IsAlive(entity)) return;
  if (!reg->HasComponent<UIFocusComponent>(entity)) {
    reg->AddComponent(entity, UIFocusComponent{});
  }
  reg->GetComponent<UIFocusComponent>(entity).isChecked = checked;
}

bool IsEntityChecked(Registry* reg, Entity entity) {
  if (!reg->IsAlive(entity) || !reg->HasComponent<UIFocusComponent>(entity)) return false;
  return reg->GetComponent<UIFocusComponent>(entity).isChecked;
}

void SetEntityDisabled(Registry* reg, Entity entity, bool disabled) {
  if (!reg->IsAlive(entity)) return;
  if (reg->HasComponent<UIButtonComponent>(entity)) {
    reg->GetComponent<UIButtonComponent>(entity).isActive = !disabled;
  }
  if (!reg->HasComponent<UIFocusComponent>(entity)) {
    reg->AddComponent(entity, UIFocusComponent{});
  }
  reg->GetComponent<UIFocusComponent>(entity).isDisabled = disabled;
}

bool IsEntityDisabled(Registry* reg, Entity entity) {
  if (!reg->IsAlive(entity)) return false;
  if (reg->HasComponent<UIFocusComponent>(entity)) {
    return reg->GetComponent<UIFocusComponent>(entity).isDisabled;
  }
  if (reg->HasComponent<UIButtonComponent>(entity)) {
    return !reg->GetComponent<UIButtonComponent>(entity).isActive;
  }
  return false;
}

struct RadioGroupState {
  std::string bindPath{};
  std::string onChangeMethod{};
  std::string currentValue{};
  std::vector<std::pair<Entity, std::string>> buttons{};
};

struct SliderState {
  Entity entity{};
  float minVal = 0.0f;
  float maxVal = 1.0f;
  float step = 0.0f;
  float value = 0.0f;
  std::string bindPath{};
  std::string onChangeMethod{};
  std::vector<sol::protected_function> changeListeners{};
};

struct RepeaterRecord {
  Entity parentEntity{};
  OxmlNode itemTemplate{};
  OxmlEachClause clause{};
  std::string keyProp{};
  std::vector<Entity> spawnedEntities{};
};

sol::object LookupKeyInTable(const sol::table& tbl, const std::string& key) {
  if (!tbl.valid()) return sol::lua_nil;
  sol::object v = tbl[key];
  return (v.valid() && v != sol::lua_nil) ? v : sol::object(sol::lua_nil);
}

struct UIViewInstance : std::enable_shared_from_this<UIViewInstance> {
  UIRuntime* runtime{nullptr};
  Entity rootEntity{};
  sol::table context{};
  sol::table params{};
  sol::table controller{};
  sol::table refsTable{};
  sol::table viewTable{};
  OssStylesheet stylesheet{};
  std::unordered_map<std::string, OxmlDocument> importedUses{};
  std::vector<std::function<void()>> refreshFns{};
  std::vector<std::function<void()>> unmountFns{};
  std::unordered_map<uint32_t, std::shared_ptr<SliderState>> sliders{};
  std::unordered_map<uint32_t, std::vector<sol::protected_function>> changeCallbacks{};
  bool isMounted = true;
  bool isRefreshing = false;

  [[nodiscard]] sol::table MakeScope(const sol::table& localVars = sol::lua_nil) const {
    sol::state& lua = *runtime->GetLua();
    sol::table scope = lua.create_table();
    sol::table meta = lua.create_table();
    meta.set_function("__index", [this, localVars](const sol::table&, const std::string& key) -> sol::object {
      if (const sol::object v = LookupKeyInTable(localVars, key); v != sol::lua_nil) return v;
      if (const sol::object v = LookupKeyInTable(context, key); v != sol::lua_nil) return v;
      return LookupKeyInTable(params, key);
    });
    scope[sol::metatable_key] = meta;
    return scope;
  }

  void InvokeController(const std::string& methodName, const sol::object& arg1 = sol::lua_nil) {
    if (methodName.empty() || !controller.valid()) return;
    sol::object fnObj = controller[methodName];
    if (!fnObj.is<sol::protected_function>()) return;
    sol::protected_function fn = fnObj.as<sol::protected_function>();
    const auto res = arg1.valid() && arg1 != sol::lua_nil ? fn(controller, arg1, viewTable) : fn(controller, viewTable);
    if (!res.valid()) {
      const sol::error err = res;
      Logger::Error("UI controller '" + methodName + "' error: " + err.what());
    }
  }

  void Refresh() {
    if (!isMounted || isRefreshing) return;
    isRefreshing = true;
    const std::size_t count = refreshFns.size();
    for (std::size_t i = 0; i < count; ++i) {
      refreshFns[i]();
    }
    isRefreshing = false;
  }

  void Unmount() {
    if (!isMounted) return;
    isMounted = false;
    InvokeController("on_unmount");
    for (auto& fn : unmountFns) {
      fn();
    }
    unmountFns.clear();
    refreshFns.clear();
    Registry* reg = runtime->GetRegistry();
    if (reg != nullptr && reg->IsAlive(rootEntity)) {
      reg->BlamEntity(rootEntity);
    }
  }
};

static constexpr const char* kStyleAttrs[] = {
    "width",
    "height",
    "min-width",
    "min-height",
    "max-width",
    "max-height",
    "padding",
    "padding-top",
    "padding-right",
    "padding-bottom",
    "padding-left",
    "margin",
    "margin-top",
    "margin-right",
    "margin-bottom",
    "margin-left",
    "gap",
    "spacing",
    "direction",
    "flex-direction",
    "align",
    "align-items",
    "justify",
    "justify-content",
    "background-color",
    "bg-color",
    "background",
    "border-color",
    "border-width",
    "text-color",
    "color",
    "track-color",
    "font",
    "slice",
    "overflow",
    "clip",
    "flex-grow",
    "text-align",
    "wrap",
    "text-wrap",
    "white-space",
    "wrap-width",
};

std::vector<std::string> ResolveNodeClasses(const OxmlNode& node, const sol::table& scope, sol::state& lua) {
  const std::string rawClass = node.GetAttr("class");
  const std::string evalClass =
      OxmlParser::ContainsBinding(rawClass) ? EvalInterpolatedString(lua, rawClass, scope) : rawClass;
  return OssParser::SplitClassList(evalClass);
}

bool HasDynamicStyleOrClassBinding(const OxmlNode& node) {
  if (OxmlParser::ContainsBinding(node.GetAttr("class")) || OxmlParser::ContainsBinding(node.GetAttr("style"))) {
    return true;
  }
  for (const char* key : kStyleAttrs) {
    const auto it = node.attributes.find(key);
    if (it != node.attributes.end() && OxmlParser::ContainsBinding(it->second)) {
      return true;
    }
  }
  return false;
}

std::map<std::string, std::string> CollectInlineStyleProps(const OxmlNode& node, const sol::table& scope,
                                                           sol::state& lua) {
  std::map<std::string, std::string> props;
  if (node.HasAttr("style")) {
    const std::string rawStyle = node.GetAttr("style");
    const std::string evalStyle =
        OxmlParser::ContainsBinding(rawStyle) ? EvalInterpolatedString(lua, rawStyle, scope) : rawStyle;
    props = OssParser::ParseInlineStyle(evalStyle);
  }
  for (const char* key : kStyleAttrs) {
    const auto it = node.attributes.find(key);
    if (it == node.attributes.end()) continue;
    if (OxmlParser::ContainsBinding(it->second)) {
      props[key] = EvalInterpolatedString(lua, it->second, scope);
    } else {
      props[key] = it->second;
    }
  }
  return props;
}

void AttachCommonComponents(Registry* reg, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved) {
  reg->AddComponent(entity, UIRectComponent{});
  UILayoutBoxComponent box = BuildLayoutBoxFromStyle(resolved.base);
  if (node.tag == "Canvas" && !resolved.Has("width") && !node.HasAttr("width")) {
    box.SetWidthPercent(100.0f);
  }
  if (node.tag == "Canvas" && !resolved.Has("height") && !node.HasAttr("height")) {
    box.SetHeightPercent(100.0f);
  }
  reg->AddComponent(entity, box);

  if (node.HasAttr("id")) {
    reg->AddComponent(entity, NameComponent(node.GetAttr("id")));
  }
  if (resolved.Has("z-index") || node.HasAttr("z-index")) {
    float zf = 0.0f;
    if (OssParser::ParseFloat(resolved.Get("z-index", node.GetAttr("z-index")), zf)) {
      reg->AddComponent(entity, UIZIndexComponent{static_cast<int>(zf)});
    }
  }
  if (resolved.HasPseudoStates()) {
    reg->AddComponent(entity, resolved.BuildStyleStateComponent());
  }
}

void AttachVisualBackground(Registry* reg, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved) {
  octarine::Color bg{0, 0, 0, 0};
  const bool hasBg = ExtractBgColor(resolved.base, bg);
  octarine::Color border{0, 0, 0, 0};
  const bool hasBorder = OssParser::ParseColor(resolved.Get("border-color"), border);
  float borderWidth = 0.0f;
  OssParser::ParseFloat(resolved.Get("border-width"), borderWidth);

  if (hasBg || hasBorder || borderWidth > 0.0f || resolved.HasPseudoStates()) {
    SquarePrimitiveComponent sq;
    sq.color = hasBg ? bg : octarine::Color{0, 0, 0, 0};
    sq.borderColor = hasBorder ? border : sq.color;
    sq.borderThickness = borderWidth;
    reg->AddComponent(entity, sq);
  }

  const std::string spriteId = node.GetAttr("sprite", resolved.Get("sprite"));
  if (!spriteId.empty() && !OxmlParser::ContainsBinding(spriteId)) {
    SpriteComponent sp(spriteId, 0.0f, 0.0f, 0, true);
    reg->AddComponent(entity, sp);
  }
  const std::string sliceStr = node.GetAttr("slice", resolved.Get("slice"));
  if (!sliceStr.empty()) {
    UINineSliceComponent ns;
    if (OssParser::ParseSlice4(sliceStr, ns.left, ns.top, ns.right, ns.bottom)) {
      reg->AddComponent(entity, ns);
    }
  }
}

void ConfigureLabelWrapping(const OxmlNode& node, const OssResolvedStyle& resolved, TextLabelComponent& lbl) {
  const std::string wrapStr = node.GetAttr("wrap", resolved.Get("wrap", resolved.Get("text-wrap")));
  const std::string wsStr = resolved.Get("white-space");
  const std::string wrapWStr = node.GetAttr("wrap-width", resolved.Get("wrap-width"));

  if (wrapStr == "false" || wrapStr == "0" || wrapStr == "nowrap" || wsStr == "nowrap") {
    lbl.wrapToRect = false;
    lbl.wrapWidth = 0;
    return;
  }

  float explicitWrapPx = 0.0f;
  if (!wrapWStr.empty() && OssParser::ParseFloat(wrapWStr, explicitWrapPx) && explicitWrapPx > 0.0f) {
    lbl.wrapWidth = static_cast<int>(explicitWrapPx);
    lbl.wrapToRect = true;
    return;
  }
  if (!wrapStr.empty() && OssParser::ParseFloat(wrapStr, explicitWrapPx) && explicitWrapPx > 0.0f && wrapStr != "1") {
    lbl.wrapWidth = static_cast<int>(explicitWrapPx);
    lbl.wrapToRect = true;
    return;
  }
  if (ParseBoolStr(wrapStr, false) || wrapStr == "wrap" || wsStr == "normal" || wsStr == "pre-wrap") {
    lbl.wrapToRect = true;
  }
  if (node.tag == "Label") {
    float maxVal = 0.0f;
    UIUnit maxUnit = UIUnit::Auto;
    if (OssParser::ParseDimension(resolved.Get("max-width"), maxVal, maxUnit) && maxUnit == UIUnit::Px &&
        maxVal > 0.0f) {
      lbl.wrapToRect = true;
      if (lbl.wrapWidth <= 0) lbl.wrapWidth = static_cast<int>(maxVal);
    }
  }
}

void ConfigureLabelEntity(Registry* reg, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved,
                          const sol::table& scope, sol::state& lua) {
  const std::string rawText = node.GetAttr("text");
  const std::string initialText =
      OxmlParser::ContainsBinding(rawText) ? EvalInterpolatedString(lua, rawText, scope) : rawText;
  const std::string fontId = node.GetAttr("font", resolved.Get("font", "default-16"));
  octarine::Color textColor{255, 255, 255, 255};
  ExtractTextColor(resolved.base, textColor);

  const bool centered = (node.tag == "Button" || resolved.Get("text-align") == "center");
  const TextAlign hAlign = centered ? TextAlign::Center : TextAlign::Start;
  const TextAlign vAlign = centered ? TextAlign::Center : TextAlign::Start;
  TextLabelComponent lbl({0.0f, 0.0f}, 0, initialText, fontId, textColor, true, hAlign, vAlign);
  ConfigureLabelWrapping(node, resolved, lbl);
  reg->AddComponent(entity, lbl);
}

void ConfigureInteractiveFocus(Registry* reg, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved) {
  UIFocusComponent focus;
  if (reg->HasComponent<UIFocusComponent>(entity)) {
    focus = reg->GetComponent<UIFocusComponent>(entity);
  }
  focus.isFocusable = ParseBoolStr(node.GetAttr("focusable", resolved.Get("focusable", "true")), true);
  if (node.HasAttr("tab-index")) {
    float ti = 0.0f;
    if (OssParser::ParseFloat(node.GetAttr("tab-index"), ti)) {
      focus.tabIndex = static_cast<int>(ti);
    }
  }
  reg->AddComponent(entity, focus);
}

void ConfigureButtonEntity(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                           const OssResolvedStyle& resolved, const sol::table& scope) {
  Registry* reg = view->runtime->GetRegistry();
  sol::state& lua = *view->runtime->GetLua();

  if (!reg->HasComponent<SquarePrimitiveComponent>(entity)) {
    octarine::Color bg{42, 34, 62, 255};
    ExtractBgColor(resolved.base, bg);
    octarine::Color border{90, 75, 130, 255};
    OssParser::ParseColor(resolved.Get("border-color"), border);
    float bw = 1.0f;
    OssParser::ParseFloat(resolved.Get("border-width", "1"), bw);
    SquarePrimitiveComponent sq;
    sq.color = bg;
    sq.borderColor = border;
    sq.borderThickness = bw;
    reg->AddComponent(entity, sq);
  }
  if (node.HasAttr("text") || !node.innerText.empty()) {
    ConfigureLabelEntity(reg, entity, node, resolved, scope, lua);
  }

  const bool isDisabled = ParseBoolStr(node.GetAttr("disabled", resolved.Get("disabled", "false")), false);
  UIButtonComponent btn;
  btn.isActive = !isDisabled;
  btn.key = node.GetAttr("key");

  const std::string onClickMethod = node.GetAttr("on-click");
  sol::table localScope = scope;
  std::weak_ptr<UIViewInstance> weakView = view;
  btn.clickFunction = lua.create_table_with("fn", [weakView, onClickMethod, localScope]() {
    if (auto v = weakView.lock()) {
      sol::object itemArg = localScope.valid() ? localScope["item"] : sol::lua_nil;
      v->InvokeController(onClickMethod, itemArg);
    }
  })["fn"];

  reg->AddComponent(entity, btn);
  ConfigureInteractiveFocus(reg, entity, node, resolved);
  SetEntityDisabled(reg, entity, isDisabled);
}

void ConfigureProgressBarEntity(Registry* reg, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved,
                                const sol::table& scope, sol::state& lua) {
  octarine::Color fillColor{6, 214, 160, 255};
  if (!OssParser::ParseColor(node.GetAttr("color", resolved.Get("color")), fillColor)) {
    ExtractBgColor(resolved.base, fillColor);
  }
  octarine::Color trackColor{25, 22, 36, 220};
  OssParser::ParseColor(node.GetAttr("track-color", resolved.Get("track-color")), trackColor);
  octarine::Color borderColor{80, 75, 105, 255};
  OssParser::ParseColor(resolved.Get("border-color"), borderColor);
  float borderWidth = 1.0f;
  OssParser::ParseFloat(resolved.Get("border-width", "1"), borderWidth);

  float val = 1.0f;
  const std::string rawVal = node.GetAttr("value", "1.0");
  std::string expr;
  if (OxmlParser::IsPureBinding(rawVal, expr)) {
    val = ObjectToFloat(EvalBindingExpr(lua, expr, scope), 0.0f);
  } else {
    OssParser::ParseFloat(rawVal, val);
  }
  val = std::clamp(val, 0.0f, 1.0f);

  SquarePrimitiveComponent sq;
  sq.color = fillColor;
  sq.trackColor = trackColor;
  sq.borderColor = borderColor;
  sq.borderThickness = borderWidth;
  sq.fillRatio = val;
  reg->AddComponent(entity, sq);

  if (node.HasAttr("text")) {
    ConfigureLabelEntity(reg, entity, node, resolved, scope, lua);
    reg->GetComponent<TextLabelComponent>(entity).hAlign = TextAlign::Center;
    reg->GetComponent<TextLabelComponent>(entity).vAlign = TextAlign::Center;
  }
}

void EnsureDefaultCheckedStyle(Registry* reg, Entity entity) {
  if (!reg->HasComponent<UIStyleStateComponent>(entity)) {
    reg->AddComponent(entity, UIStyleStateComponent{});
  }
  auto& st = reg->GetComponent<UIStyleStateComponent>(entity);
  if (!st.checked.HasBg()) {
    st.checked.SetBg(octarine::Color{255, 209, 102, 255});
  }
  if (!st.checked.HasBorder()) {
    st.checked.SetBorder(octarine::Color{255, 235, 150, 255});
  }
}

void EnsureToggleBaseVisuals(Registry* reg, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved,
                             const sol::table& scope, sol::state& lua) {
  if (!reg->HasComponent<SquarePrimitiveComponent>(entity)) {
    octarine::Color bg{38, 32, 54, 255};
    ExtractBgColor(resolved.base, bg);
    octarine::Color border{100, 90, 140, 255};
    OssParser::ParseColor(resolved.Get("border-color"), border);
    SquarePrimitiveComponent sq;
    sq.color = bg;
    sq.borderColor = border;
    sq.borderThickness = 1.0f;
    reg->AddComponent(entity, sq);
  }
  if (node.HasAttr("text") || !node.innerText.empty()) {
    ConfigureLabelEntity(reg, entity, node, resolved, scope, lua);
  }
  EnsureDefaultCheckedStyle(reg, entity);
  ConfigureInteractiveFocus(reg, entity, node, resolved);
}

bool ResolveInitialToggleChecked(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node,
                                 const std::string& bindPath, const sol::table& scope, sol::state& lua) {
  if (!bindPath.empty() && view->context.valid()) {
    return ObjectToBool(GetPathValue(view->context, bindPath));
  }
  if (!node.HasAttr("checked")) {
    return false;
  }
  const std::string rawChk = node.GetAttr("checked");
  std::string expr;
  if (OxmlParser::IsPureBinding(rawChk, expr)) {
    return ObjectToBool(EvalBindingExpr(lua, expr, scope));
  }
  return ParseBoolStr(rawChk, false);
}

void HandleToggleClick(const std::shared_ptr<UIViewInstance>& v, Entity entity, const std::string& bindPath,
                       const std::string& onChangeMethod) {
  Registry* r = v->runtime->GetRegistry();
  if (!r->IsAlive(entity)) return;
  const bool nextChecked = !IsEntityChecked(r, entity);
  SetEntityChecked(r, entity, nextChecked);
  sol::object valObj = sol::make_object(*v->runtime->GetLua(), nextChecked);
  if (!bindPath.empty() && v->context.valid()) {
    SetPathValue(v->context, bindPath, valObj);
    v->Refresh();
  }
  v->InvokeController(onChangeMethod, valObj);
  if (const auto it = v->changeCallbacks.find(entity.GetId()); it != v->changeCallbacks.end()) {
    for (const auto& cb : it->second) {
      cb(nextChecked);
    }
  }
}

void ConfigureToggleEntity(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                           const OssResolvedStyle& resolved, const sol::table& scope) {
  Registry* reg = view->runtime->GetRegistry();
  sol::state& lua = *view->runtime->GetLua();
  EnsureToggleBaseVisuals(reg, entity, node, resolved, scope, lua);

  const bool isDisabled = ParseBoolStr(node.GetAttr("disabled", resolved.Get("disabled", "false")), false);
  const std::string bindPath = node.GetAttr("bind-value");
  const std::string onChangeMethod = node.GetAttr("on-change");
  SetEntityChecked(reg, entity, ResolveInitialToggleChecked(view, node, bindPath, scope, lua));

  UIButtonComponent btn;
  btn.isActive = !isDisabled;
  std::weak_ptr<UIViewInstance> weakView = view;
  btn.clickFunction = lua.create_table_with("fn", [weakView, entity, bindPath, onChangeMethod]() {
    if (auto v = weakView.lock()) {
      HandleToggleClick(v, entity, bindPath, onChangeMethod);
    }
  })["fn"];

  reg->AddComponent(entity, btn);
  SetEntityDisabled(reg, entity, isDisabled);

  if (!bindPath.empty()) {
    view->refreshFns.push_back([weakView, entity, bindPath]() {
      auto v = weakView.lock();
      if (!v || !v->context.valid()) return;
      SetEntityChecked(v->runtime->GetRegistry(), entity, ObjectToBool(GetPathValue(v->context, bindPath)));
    });
  }
}

void HandleRadioButtonClick(const std::shared_ptr<UIViewInstance>& v, Entity entity, const std::string& optionValue,
                            const std::shared_ptr<RadioGroupState>& radioGroup, const std::string& onClickMethod) {
  Registry* r = v->runtime->GetRegistry();
  sol::object valObj = sol::make_object(*v->runtime->GetLua(), optionValue);

  if (radioGroup != nullptr) {
    radioGroup->currentValue = optionValue;
    for (const auto& [btnEnt, btnVal] : radioGroup->buttons) {
      SetEntityChecked(r, btnEnt, btnEnt.id == entity.id);
    }
    if (!radioGroup->bindPath.empty() && v->context.valid()) {
      SetPathValue(v->context, radioGroup->bindPath, valObj);
      v->Refresh();
    }
    v->InvokeController(radioGroup->onChangeMethod, valObj);
  } else {
    SetEntityChecked(r, entity, true);
  }

  v->InvokeController(onClickMethod, valObj);
  if (const auto it = v->changeCallbacks.find(entity.GetId()); it != v->changeCallbacks.end()) {
    for (const auto& cb : it->second) {
      cb(optionValue);
    }
  }
}

void ConfigureRadioButtonEntity(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                                const OssResolvedStyle& resolved, const sol::table& scope,
                                const std::shared_ptr<RadioGroupState>& radioGroup) {
  Registry* reg = view->runtime->GetRegistry();
  sol::state& lua = *view->runtime->GetLua();
  EnsureToggleBaseVisuals(reg, entity, node, resolved, scope, lua);

  const std::string optionValue = node.GetAttr("value", node.GetAttr("text"));
  const bool isDisabled = ParseBoolStr(node.GetAttr("disabled", resolved.Get("disabled", "false")), false);
  const bool initialChecked = (radioGroup != nullptr && radioGroup->currentValue == optionValue) ||
                              ParseBoolStr(node.GetAttr("checked"), false);
  SetEntityChecked(reg, entity, initialChecked);

  if (radioGroup != nullptr) {
    radioGroup->buttons.emplace_back(entity, optionValue);
  }

  const std::string onClickMethod = node.GetAttr("on-click");
  UIButtonComponent btn;
  btn.isActive = !isDisabled;
  std::weak_ptr<UIViewInstance> weakView = view;
  btn.clickFunction = lua.create_table_with("fn", [weakView, entity, optionValue, radioGroup, onClickMethod]() {
    if (auto v = weakView.lock()) {
      HandleRadioButtonClick(v, entity, optionValue, radioGroup, onClickMethod);
    }
  })["fn"];

  reg->AddComponent(entity, btn);
  SetEntityDisabled(reg, entity, isDisabled);
}

void UpdateSliderVisuals(Registry* reg, const SliderState& st) {
  if (!reg->IsAlive(st.entity) || !reg->HasComponent<SquarePrimitiveComponent>(st.entity)) return;
  const float span = std::max(0.0001f, st.maxVal - st.minVal);
  const float norm = std::clamp((st.value - st.minVal) / span, 0.0f, 1.0f);
  reg->GetComponent<SquarePrimitiveComponent>(st.entity).fillRatio = norm;
}

void SetSliderValue(const std::shared_ptr<UIViewInstance>& view, const std::shared_ptr<SliderState>& st, float newVal,
                    bool notify) {
  if (st->step > 0.0f) {
    newVal = st->minVal + std::round((newVal - st->minVal) / st->step) * st->step;
  }
  st->value = std::clamp(newVal, st->minVal, st->maxVal);
  UpdateSliderVisuals(view->runtime->GetRegistry(), *st);
  if (!notify) return;

  sol::object valObj = sol::make_object(*view->runtime->GetLua(), st->value);
  if (!st->bindPath.empty() && view->context.valid()) {
    SetPathValue(view->context, st->bindPath, valObj);
    view->Refresh();
  }
  view->InvokeController(st->onChangeMethod, valObj);
  for (const auto& cb : st->changeListeners) {
    cb(st->value);
  }
}

float ResolveInitialSliderValue(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node,
                                const SliderState& st, const sol::table& scope, sol::state& lua) {
  float val = st.minVal;
  if (!st.bindPath.empty() && view->context.valid()) {
    val = ObjectToFloat(GetPathValue(view->context, st.bindPath), st.minVal);
  } else if (node.HasAttr("value")) {
    const std::string rawVal = node.GetAttr("value");
    std::string expr;
    if (OxmlParser::IsPureBinding(rawVal, expr)) {
      val = ObjectToFloat(EvalBindingExpr(lua, expr, scope), st.minVal);
    } else {
      OssParser::ParseFloat(rawVal, val);
    }
  }
  return std::clamp(val, st.minVal, st.maxVal);
}

void HandleSliderClick(const std::shared_ptr<UIViewInstance>& v, const std::shared_ptr<SliderState>& st) {
  Registry* r = v->runtime->GetRegistry();
  if (!r->IsAlive(st->entity) || !r->HasComponent<UIRectComponent>(st->entity)) return;
  const auto& rect = r->GetComponent<UIRectComponent>(st->entity);
  float mx = 0.0f;
  float my = 0.0f;
  SDL_GetMouseState(&mx, &my);
  const float w = rect.Width();
  float nextVal = st->value;
  if (w > 0.0f && mx >= rect.left && mx <= rect.right && my >= rect.top && my <= rect.bottom) {
    const float t = std::clamp((mx - rect.left) / w, 0.0f, 1.0f);
    nextVal = st->minVal + t * (st->maxVal - st->minVal);
  } else {
    const float stepAmt = st->step > 0.0f ? st->step : (st->maxVal - st->minVal) * 0.1f;
    nextVal = (st->value + stepAmt > st->maxVal) ? st->minVal : (st->value + stepAmt);
  }
  SetSliderValue(v, st, nextVal, true);
}

void ConfigureSliderEntity(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                           const OssResolvedStyle& resolved, const sol::table& scope) {
  Registry* reg = view->runtime->GetRegistry();
  sol::state& lua = *view->runtime->GetLua();

  auto st = std::make_shared<SliderState>();
  st->entity = entity;
  OssParser::ParseFloat(node.GetAttr("min", "0"), st->minVal);
  OssParser::ParseFloat(node.GetAttr("max", "1"), st->maxVal);
  OssParser::ParseFloat(node.GetAttr("step", "0"), st->step);
  st->bindPath = node.GetAttr("bind-value");
  st->onChangeMethod = node.GetAttr("on-change");
  st->value = ResolveInitialSliderValue(view, node, *st, scope, lua);

  octarine::Color fillColor{255, 209, 102, 255};
  if (!OssParser::ParseColor(node.GetAttr("color", resolved.Get("color")), fillColor)) {
    ExtractBgColor(resolved.base, fillColor);
  }
  octarine::Color trackColor{25, 22, 36, 220};
  OssParser::ParseColor(node.GetAttr("track-color", resolved.Get("track-color")), trackColor);
  octarine::Color borderColor{90, 80, 120, 255};
  OssParser::ParseColor(resolved.Get("border-color"), borderColor);

  SquarePrimitiveComponent sq;
  sq.color = fillColor;
  sq.trackColor = trackColor;
  sq.borderColor = borderColor;
  sq.borderThickness = 1.0f;
  reg->AddComponent(entity, sq);
  UpdateSliderVisuals(reg, *st);

  view->sliders[entity.GetId()] = st;

  const bool isDisabled = ParseBoolStr(node.GetAttr("disabled", resolved.Get("disabled", "false")), false);
  UIButtonComponent btn;
  btn.isActive = !isDisabled;
  std::weak_ptr<UIViewInstance> weakView = view;
  btn.clickFunction = lua.create_table_with("fn", [weakView, st]() {
    if (auto v = weakView.lock()) {
      HandleSliderClick(v, st);
    }
  })["fn"];

  reg->AddComponent(entity, btn);
  ConfigureInteractiveFocus(reg, entity, node, resolved);
  SetEntityDisabled(reg, entity, isDisabled);

  if (!st->bindPath.empty()) {
    view->refreshFns.push_back([weakView, st]() {
      auto v = weakView.lock();
      if (!v || !v->context.valid()) return;
      const float boundVal = ObjectToFloat(GetPathValue(v->context, st->bindPath), st->value);
      SetSliderValue(v, st, boundVal, false);
    });
  }
}

void SetEntityVisibility(Registry* r, Entity entity, bool vis) {
  if (!r->IsAlive(entity)) return;
  if (r->HasComponent<UIRectComponent>(entity)) {
    r->GetComponent<UIRectComponent>(entity).visible = vis;
  }
  if (r->HasComponent<UILayoutBoxComponent>(entity)) {
    r->GetComponent<UILayoutBoxComponent>(entity).visible = vis;
  }
}

std::string UnwrapBindingOrRaw(std::string rawAttr) {
  std::string unwrapped;
  if (OxmlParser::IsPureBinding(rawAttr, unwrapped)) {
    return unwrapped;
  }
  return rawAttr;
}

void BindVisibilityAttribute(const std::shared_ptr<UIViewInstance>& view, Entity entity, const std::string& rawAttr,
                             const sol::table& scope) {
  const std::string expr = UnwrapBindingOrRaw(rawAttr);
  std::weak_ptr<UIViewInstance> weakView = view;
  auto applyVis = [weakView, entity, expr, scope]() {
    auto v = weakView.lock();
    if (!v) return;
    const bool vis = ObjectToBool(EvalBindingExpr(*v->runtime->GetLua(), expr, scope));
    SetEntityVisibility(v->runtime->GetRegistry(), entity, vis);
  };
  applyVis();
  view->refreshFns.push_back(applyVis);
}

void BindDisabledAttribute(const std::shared_ptr<UIViewInstance>& view, Entity entity, const std::string& rawAttr,
                           const sol::table& scope) {
  const std::string expr = UnwrapBindingOrRaw(rawAttr);
  std::weak_ptr<UIViewInstance> weakView = view;
  auto applyDis = [weakView, entity, expr, scope]() {
    auto v = weakView.lock();
    if (!v) return;
    const bool dis = ObjectToBool(EvalBindingExpr(*v->runtime->GetLua(), expr, scope));
    SetEntityDisabled(v->runtime->GetRegistry(), entity, dis);
  };
  applyDis();
  view->refreshFns.push_back(applyDis);
}

void BindSpriteAttribute(const std::shared_ptr<UIViewInstance>& view, Entity entity, const std::string& rawSprite,
                         const sol::table& scope) {
  std::weak_ptr<UIViewInstance> weakView = view;
  auto applySprite = [weakView, entity, rawSprite, scope]() {
    auto v = weakView.lock();
    if (!v) return;
    Registry* r = v->runtime->GetRegistry();
    if (!r->IsAlive(entity)) return;
    const std::string resolvedId = EvalInterpolatedString(*v->runtime->GetLua(), rawSprite, scope);
    if (r->HasComponent<SpriteComponent>(entity)) {
      r->GetComponent<SpriteComponent>(entity).assetId = resolvedId;
    } else if (!resolvedId.empty()) {
      r->AddComponent(entity, SpriteComponent(resolvedId, 0.0f, 0.0f, 0, true));
    }
  };
  applySprite();
  view->refreshFns.push_back(applySprite);
}

void ApplyResolvedBoxAndFlexUpdate(Registry* r, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved) {
  if (r->HasComponent<UILayoutBoxComponent>(entity)) {
    const bool prevVis = r->GetComponent<UILayoutBoxComponent>(entity).visible;
    UILayoutBoxComponent box = BuildLayoutBoxFromStyle(resolved.base);
    if (node.tag == "Canvas" && !resolved.Has("width") && !node.HasAttr("width")) {
      box.SetWidthPercent(100.0f);
    }
    if (node.tag == "Canvas" && !resolved.Has("height") && !node.HasAttr("height")) {
      box.SetHeightPercent(100.0f);
    }
    box.visible = prevVis;
    r->GetComponent<UILayoutBoxComponent>(entity) = box;
  }
  if (r->HasComponent<UIFlexComponent>(entity)) {
    const FlexDirection defaultDir = (node.tag == "Row") ? FlexDirection::Row : FlexDirection::Column;
    r->GetComponent<UIFlexComponent>(entity) = BuildFlexFromStyle(resolved.base, defaultDir);
  }
}

void ApplyResolvedSquareUpdate(Registry* r, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved) {
  if (node.tag == "ProgressBar" && r->HasComponent<SquarePrimitiveComponent>(entity)) {
    auto& sq = r->GetComponent<SquarePrimitiveComponent>(entity);
    if (!OssParser::ParseColor(resolved.Get("color"), sq.color)) {
      ExtractBgColor(resolved.base, sq.color);
    }
    OssParser::ParseColor(resolved.Get("track-color"), sq.trackColor);
    OssParser::ParseColor(resolved.Get("border-color"), sq.borderColor);
    OssParser::ParseFloat(resolved.Get("border-width", "1"), sq.borderThickness);
    return;
  }

  octarine::Color bg{0, 0, 0, 0};
  const bool hasBg = ExtractBgColor(resolved.base, bg);
  octarine::Color border{0, 0, 0, 0};
  const bool hasBorder = OssParser::ParseColor(resolved.Get("border-color"), border);
  float borderWidth = 0.0f;
  const bool hasBw = OssParser::ParseFloat(resolved.Get("border-width"), borderWidth);

  if (r->HasComponent<SquarePrimitiveComponent>(entity)) {
    auto& sq = r->GetComponent<SquarePrimitiveComponent>(entity);
    if (hasBg) sq.color = bg;
    if (hasBorder) sq.borderColor = border;
    if (hasBw) sq.borderThickness = borderWidth;
  } else if (hasBg || hasBorder || borderWidth > 0.0f) {
    SquarePrimitiveComponent sq;
    sq.color = hasBg ? bg : octarine::Color{0, 0, 0, 0};
    sq.borderColor = hasBorder ? border : sq.color;
    sq.borderThickness = borderWidth;
    r->AddComponent(entity, sq);
  }
}

void ApplyResolvedVisualUpdate(Registry* r, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved) {
  ApplyResolvedSquareUpdate(r, entity, node, resolved);
  if (r->HasComponent<TextLabelComponent>(entity)) {
    auto& lbl = r->GetComponent<TextLabelComponent>(entity);
    ExtractTextColor(resolved.base, lbl.color);
    if (resolved.Has("font")) {
      lbl.fontId = resolved.Get("font", lbl.fontId);
    }
    ConfigureLabelWrapping(node, resolved, lbl);
  }
  if (resolved.HasPseudoStates()) {
    if (r->HasComponent<UIStyleStateComponent>(entity)) {
      r->GetComponent<UIStyleStateComponent>(entity) = resolved.BuildStyleStateComponent();
    } else {
      r->AddComponent(entity, resolved.BuildStyleStateComponent());
    }
  }
}

void BindDynamicStyleAttributes(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                                const sol::table& scope) {
  if (!HasDynamicStyleOrClassBinding(node)) return;
  std::weak_ptr<UIViewInstance> weakView = view;
  view->refreshFns.push_back([weakView, entity, node, scope]() {
    auto v = weakView.lock();
    if (!v) return;
    Registry* r = v->runtime->GetRegistry();
    if (!r->IsAlive(entity)) return;
    sol::state& l = *v->runtime->GetLua();
    const auto classes = ResolveNodeClasses(node, scope, l);
    const auto inlineProps = CollectInlineStyleProps(node, scope, l);
    const OssResolvedStyle resolved = v->stylesheet.Resolve(node.tag, classes, node.GetAttr("id"), inlineProps);
    ApplyResolvedBoxAndFlexUpdate(r, entity, node, resolved);
    ApplyResolvedVisualUpdate(r, entity, node, resolved);
  });
}

void RegisterAttributeBindings(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                               const sol::table& scope) {
  std::weak_ptr<UIViewInstance> weakView = view;

  if (node.HasAttr("if")) {
    BindVisibilityAttribute(view, entity, node.GetAttr("if"), scope);
  }
  if (node.HasAttr("visible")) {
    const std::string rawVis = node.GetAttr("visible");
    if (OxmlParser::ContainsBinding(rawVis)) {
      BindVisibilityAttribute(view, entity, rawVis, scope);
    } else {
      SetEntityVisibility(view->runtime->GetRegistry(), entity, ParseBoolStr(rawVis, true));
    }
  }
  if (node.HasAttr("text") && OxmlParser::ContainsBinding(node.GetAttr("text"))) {
    const std::string rawText = node.GetAttr("text");
    view->refreshFns.push_back([weakView, entity, rawText, scope]() {
      auto v = weakView.lock();
      if (!v) return;
      Registry* r = v->runtime->GetRegistry();
      if (!r->IsAlive(entity) || !r->HasComponent<TextLabelComponent>(entity)) return;
      r->GetComponent<TextLabelComponent>(entity).text = EvalInterpolatedString(*v->runtime->GetLua(), rawText, scope);
    });
  }
  if (node.HasAttr("value") && OxmlParser::ContainsBinding(node.GetAttr("value")) && node.tag == "ProgressBar") {
    const std::string valExpr = UnwrapBindingOrRaw(node.GetAttr("value"));
    view->refreshFns.push_back([weakView, entity, valExpr, scope]() {
      auto v = weakView.lock();
      if (!v) return;
      Registry* r = v->runtime->GetRegistry();
      if (!r->IsAlive(entity) || !r->HasComponent<SquarePrimitiveComponent>(entity)) return;
      const float p =
          std::clamp(ObjectToFloat(EvalBindingExpr(*v->runtime->GetLua(), valExpr, scope), 0.0f), 0.0f, 1.0f);
      r->GetComponent<SquarePrimitiveComponent>(entity).fillRatio = p;
    });
  }
  if (node.HasAttr("disabled") && OxmlParser::ContainsBinding(node.GetAttr("disabled"))) {
    BindDisabledAttribute(view, entity, node.GetAttr("disabled"), scope);
  }
  if (node.HasAttr("sprite") && OxmlParser::ContainsBinding(node.GetAttr("sprite"))) {
    BindSpriteAttribute(view, entity, node.GetAttr("sprite"), scope);
  }
  BindDynamicStyleAttributes(view, entity, node, scope);
}

void AttachRefTextAndValueMethods(sol::table& h, const std::weak_ptr<UIViewInstance>& weakView, Entity entity) {
  h.set_function("set_text", [weakView, entity](const sol::table&, const std::string& text) {
    auto v = weakView.lock();
    if (!v) return;
    Registry* r = v->runtime->GetRegistry();
    if (r->IsAlive(entity) && r->HasComponent<TextLabelComponent>(entity)) {
      r->GetComponent<TextLabelComponent>(entity).text = text;
    }
  });

  h.set_function("get_text", [weakView, entity](const sol::table&) -> std::string {
    auto v = weakView.lock();
    if (!v) return "";
    Registry* r = v->runtime->GetRegistry();
    if (r->IsAlive(entity) && r->HasComponent<TextLabelComponent>(entity)) {
      return r->GetComponent<TextLabelComponent>(entity).text;
    }
    return "";
  });

  h.set_function("set_value", [weakView, entity](const sol::table&, const float val) {
    auto v = weakView.lock();
    if (!v) return;
    if (const auto sIt = v->sliders.find(entity.GetId()); sIt != v->sliders.end()) {
      SetSliderValue(v, sIt->second, val, false);
      return;
    }
    Registry* r = v->runtime->GetRegistry();
    if (r->IsAlive(entity) && r->HasComponent<SquarePrimitiveComponent>(entity)) {
      r->GetComponent<SquarePrimitiveComponent>(entity).fillRatio = std::clamp(val, 0.0f, 1.0f);
    }
  });

  h.set_function("get_value", [weakView, entity](const sol::table&) -> float {
    auto v = weakView.lock();
    if (!v) return 0.0f;
    if (const auto sIt = v->sliders.find(entity.GetId()); sIt != v->sliders.end()) {
      return sIt->second->value;
    }
    Registry* r = v->runtime->GetRegistry();
    if (r->IsAlive(entity) && r->HasComponent<SquarePrimitiveComponent>(entity)) {
      return r->GetComponent<SquarePrimitiveComponent>(entity).fillRatio;
    }
    return 0.0f;
  });
}

void AttachRefStateMethods(sol::table& h, const std::weak_ptr<UIViewInstance>& weakView, Entity entity) {
  h.set_function("set_checked", [weakView, entity](const sol::table&, const bool checked) {
    if (auto v = weakView.lock()) SetEntityChecked(v->runtime->GetRegistry(), entity, checked);
  });

  h.set_function("is_checked", [weakView, entity](const sol::table&) -> bool {
    auto v = weakView.lock();
    return v ? IsEntityChecked(v->runtime->GetRegistry(), entity) : false;
  });

  h.set_function("set_visible", [weakView, entity](const sol::table&, const bool visible) {
    if (auto v = weakView.lock()) SetEntityVisibility(v->runtime->GetRegistry(), entity, visible);
  });

  h.set_function("is_visible", [weakView, entity](const sol::table&) -> bool {
    auto v = weakView.lock();
    if (!v) return false;
    Registry* r = v->runtime->GetRegistry();
    return (r->IsAlive(entity) && r->HasComponent<UIRectComponent>(entity))
               ? r->GetComponent<UIRectComponent>(entity).visible
               : false;
  });

  h.set_function("set_disabled", [weakView, entity](const sol::table&, const bool disabled) {
    if (auto v = weakView.lock()) SetEntityDisabled(v->runtime->GetRegistry(), entity, disabled);
  });

  h.set_function("is_disabled", [weakView, entity](const sol::table&) -> bool {
    auto v = weakView.lock();
    return v ? IsEntityDisabled(v->runtime->GetRegistry(), entity) : false;
  });
}

void RegisterRefEventHandler(const std::shared_ptr<UIViewInstance>& v, Entity entity, const std::string& eventName,
                             const sol::protected_function& fn) {
  Registry* r = v->runtime->GetRegistry();
  if (!r->IsAlive(entity)) return;
  if (eventName == "click" && r->HasComponent<UIButtonComponent>(entity)) {
    auto& btn = r->GetComponent<UIButtonComponent>(entity);
    const sol::protected_function prev = btn.clickFunction;
    btn.clickFunction = v->runtime->GetLua()->create_table_with("fn", [prev, fn]() {
      if (prev.valid()) prev();
      fn();
    })["fn"];
  } else if (eventName == "change") {
    if (const auto sIt = v->sliders.find(entity.GetId()); sIt != v->sliders.end()) {
      sIt->second->changeListeners.push_back(fn);
    } else {
      v->changeCallbacks[entity.GetId()].push_back(fn);
    }
  }
}

sol::table CreateRefHandle(const std::shared_ptr<UIViewInstance>& view, Entity entity) {
  sol::state& lua = *view->runtime->GetLua();
  sol::table h = lua.create_table();
  h.set("entity", entity);
  std::weak_ptr<UIViewInstance> weakView = view;

  AttachRefTextAndValueMethods(h, weakView, entity);
  AttachRefStateMethods(h, weakView, entity);

  h.set_function(
      "on", [weakView, entity](const sol::table&, const std::string& eventName, const sol::protected_function& fn) {
        auto v = weakView.lock();
        if (!v || !fn.valid()) return;
        RegisterRefEventHandler(v, entity, eventName, fn);
      });

  return h;
}

// Forward declaration for recursive node instantiation.
Entity InstantiateNode(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node, std::optional<Entity> parent,
                       const sol::table& scope, const std::vector<OxmlNode>* slotChildren,
                       const std::shared_ptr<RadioGroupState>& activeRadioGroup);

void MountRepeaterNode(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node, Entity parentEntity,
                       const sol::table& scope, const std::vector<OxmlNode>* slotChildren,
                       const std::shared_ptr<RadioGroupState>& activeRadioGroup) {
  const OxmlEachClause clause = OxmlParser::ParseEachClause(node.GetAttr("each"));
  if (!clause.valid) {
    Logger::Error("OXML: invalid each clause '" + node.GetAttr("each") + "'");
    return;
  }

  OxmlNode cleanTemplate = node;
  cleanTemplate.attributes.erase("each");
  cleanTemplate.attributes.erase("item-key");

  auto rec = std::make_shared<RepeaterRecord>();
  rec->parentEntity = parentEntity;
  rec->itemTemplate = std::move(cleanTemplate);
  rec->clause = clause;

  std::weak_ptr<UIViewInstance> weakView = view;
  const std::vector<OxmlNode> savedSlots = slotChildren != nullptr ? *slotChildren : std::vector<OxmlNode>{};

  auto rebuild = [weakView, rec, scope, savedSlots, activeRadioGroup]() {
    auto v = weakView.lock();
    if (!v) return;
    Registry* r = v->runtime->GetRegistry();
    if (!r->IsAlive(rec->parentEntity)) return;

    for (const Entity e : rec->spawnedEntities) {
      if (r->IsAlive(e)) {
        r->BlamEntity(e);
      }
    }
    rec->spawnedEntities.clear();

    sol::state& lua = *v->runtime->GetLua();
    const sol::object listObj = EvalBindingExpr(lua, rec->clause.listExpr, scope);
    if (!listObj.is<sol::table>()) return;

    const sol::table listTbl = listObj.as<sol::table>();
    const std::size_t len = listTbl.size();
    for (std::size_t idx = 1; idx <= len; ++idx) {
      sol::object itemVal = listTbl[idx];
      if (!itemVal.valid() || itemVal == sol::lua_nil) continue;
      sol::table itemVars = lua.create_table();
      itemVars[rec->clause.itemVar] = itemVal;
      if (!rec->clause.indexVar.empty()) {
        itemVars[rec->clause.indexVar] = static_cast<int>(idx);
      }
      sol::table childScope = v->MakeScope(itemVars);
      const Entity spawned =
          InstantiateNode(v, rec->itemTemplate, rec->parentEntity, childScope, &savedSlots, activeRadioGroup);
      rec->spawnedEntities.push_back(spawned);
    }
  };

  rebuild();
  view->refreshFns.push_back(rebuild);
}

bool IsReservedCallSiteAttr(const std::string& key) { return key == "if" || key == "visible" || key == "ref"; }

sol::object EvalBoundAttrValue(sol::state& lua, const std::string& rawVal, const sol::table& scope) {
  std::string pureExpr;
  if (OxmlParser::IsPureBinding(rawVal, pureExpr)) {
    return EvalBindingExpr(lua, pureExpr, scope);
  }
  return sol::make_object(lua, EvalInterpolatedString(lua, rawVal, scope));
}

void ApplyCompositeNodeBindings(const std::shared_ptr<UIViewInstance>& view, Entity rootEnt, const OxmlNode& node,
                                const sol::table& scope) {
  if (node.HasAttr("ref")) {
    view->refsTable.set(node.GetAttr("ref"), CreateRefHandle(view, rootEnt));
  }
  if (node.HasAttr("if")) {
    BindVisibilityAttribute(view, rootEnt, node.GetAttr("if"), scope);
  }
  if (node.HasAttr("visible")) {
    const std::string rawVis = node.GetAttr("visible");
    if (OxmlParser::ContainsBinding(rawVis)) {
      BindVisibilityAttribute(view, rootEnt, rawVis, scope);
    } else {
      SetEntityVisibility(view->runtime->GetRegistry(), rootEnt, ParseBoolStr(rawVis, true));
    }
  }
}

Entity InstantiateImportedUse(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node,
                              const OxmlDocument& importedDoc, std::optional<Entity> parent, const sol::table& scope,
                              const std::shared_ptr<RadioGroupState>& activeRadioGroup) {
  sol::state& lua = *view->runtime->GetLua();
  sol::table paramVars = lua.create_table();
  for (const auto& p : importedDoc.params) {
    paramVars[p.name] = p.defaultValue;
  }
  std::weak_ptr<UIViewInstance> weakView = view;
  for (const auto& [k, rawVal] : node.attributes) {
    if (IsReservedCallSiteAttr(k)) continue;
    if (OxmlParser::ContainsBinding(rawVal)) {
      paramVars[k] = EvalBoundAttrValue(lua, rawVal, scope);
      const std::string key = k;
      const std::string valStr = rawVal;
      view->refreshFns.push_back([weakView, paramVars, key, valStr, scope]() mutable {
        auto v = weakView.lock();
        if (!v) return;
        paramVars[key] = EvalBoundAttrValue(*v->runtime->GetLua(), valStr, scope);
      });
    } else {
      paramVars[k] = rawVal;
    }
  }
  sol::table templateScope = view->MakeScope(paramVars);
  Entity rootEnt{};
  if (importedDoc.roots.empty()) {
    rootEnt = view->runtime->GetRegistry()->CreateEntity();
  } else {
    rootEnt = InstantiateNode(view, importedDoc.roots.front(), parent, templateScope, &node.children, activeRadioGroup);
  }
  ApplyCompositeNodeBindings(view, rootEnt, node, scope);
  return rootEnt;
}

Entity InstantiateCustomLuaWidget(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node,
                                  const sol::table& descriptor, std::optional<Entity> parent, const sol::table& scope,
                                  const std::shared_ptr<RadioGroupState>& activeRadioGroup) {
  sol::state& lua = *view->runtime->GetLua();
  sol::table props = lua.create_table();
  if (const sol::optional<sol::table> defaults = descriptor["props"]; defaults) {
    for (const auto& [k, v] : *defaults) {
      props[k] = v;
    }
  }
  sol::table instanceCtx = lua.create_table();
  const sol::optional<sol::protected_function> onPropChanged = descriptor["on_prop_changed"];
  std::weak_ptr<UIViewInstance> weakView = view;

  for (const auto& [k, rawVal] : node.attributes) {
    if (IsReservedCallSiteAttr(k)) continue;
    if (OxmlParser::ContainsBinding(rawVal)) {
      props[k] = EvalBoundAttrValue(lua, rawVal, scope);
      const std::string key = k;
      const std::string valStr = rawVal;
      view->refreshFns.push_back([weakView, instanceCtx, props, key, valStr, scope, onPropChanged]() mutable {
        auto v = weakView.lock();
        if (!v) return;
        sol::object nextVal = EvalBoundAttrValue(*v->runtime->GetLua(), valStr, scope);
        props[key] = nextVal;
        if (onPropChanged.has_value()) {
          (*onPropChanged)(instanceCtx, key, nextVal);
        }
      });
    } else {
      props[k] = rawVal;
    }
  }

  Entity rootEnt{};
  bool createdFromTemplate = false;
  if (const sol::optional<std::string> tplStr = descriptor["template"]; tplStr && !tplStr->empty()) {
    const OxmlParseResult parsed = OxmlParser::Parse(*tplStr);
    if (parsed.ok && !parsed.document.roots.empty()) {
      sol::table tplScope = view->MakeScope(props);
      rootEnt =
          InstantiateNode(view, parsed.document.roots.front(), parent, tplScope, &node.children, activeRadioGroup);
      createdFromTemplate = true;
    }
  }
  if (!createdFromTemplate) {
    OxmlNode panelNode = node;
    panelNode.tag = "Panel";
    rootEnt = InstantiateNode(view, panelNode, parent, scope, nullptr, activeRadioGroup);
  }
  ApplyCompositeNodeBindings(view, rootEnt, node, scope);

  instanceCtx.set("entity", rootEnt);
  instanceCtx.set("props", props);
  instanceCtx.set("refs", view->refsTable);
  instanceCtx.set("view", view->viewTable);

  if (const sol::optional<sol::protected_function> onMount = descriptor["on_mount"]; onMount) {
    (*onMount)(instanceCtx);
  }
  if (const sol::optional<sol::protected_function> onUnmount = descriptor["on_unmount"]; onUnmount) {
    sol::protected_function cb = *onUnmount;
    view->unmountFns.push_back([cb, instanceCtx]() { cb(instanceCtx); });
  }
  return rootEnt;
}

bool IsContainerTag(const std::string& tag) {
  return tag == "Canvas" || tag == "Panel" || tag == "Box" || tag == "Row" || tag == "Column" || tag == "ScrollView" ||
         tag == "RadioGroup";
}

std::string ResolveInitialRadioValue(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node,
                                     const std::string& bindPath, const sol::table& scope, sol::state& lua) {
  if (!bindPath.empty() && view->context.valid()) {
    return ObjectToString(GetPathValue(view->context, bindPath));
  }
  if (node.HasAttr("value")) {
    return EvalInterpolatedString(lua, node.GetAttr("value"), scope);
  }
  return "";
}

std::shared_ptr<RadioGroupState> CreateRadioGroupState(const std::shared_ptr<UIViewInstance>& view,
                                                       const OxmlNode& node, const sol::table& scope, sol::state& lua) {
  auto rg = std::make_shared<RadioGroupState>();
  rg->bindPath = node.GetAttr("bind-value");
  rg->onChangeMethod = node.GetAttr("on-change");
  rg->currentValue = ResolveInitialRadioValue(view, node, rg->bindPath, scope, lua);
  if (!rg->bindPath.empty()) {
    std::weak_ptr<UIViewInstance> weakView = view;
    view->refreshFns.push_back([weakView, rg]() {
      auto v = weakView.lock();
      if (!v || !v->context.valid()) return;
      Registry* r = v->runtime->GetRegistry();
      rg->currentValue = ObjectToString(GetPathValue(v->context, rg->bindPath));
      for (const auto& [btnEnt, btnVal] : rg->buttons) {
        SetEntityChecked(r, btnEnt, btnVal == rg->currentValue);
      }
    });
  }
  return rg;
}

void ConfigureContainerEntity(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                              const OssResolvedStyle& resolved, const sol::table& scope,
                              std::shared_ptr<RadioGroupState>& currentRadioGroup) {
  Registry* reg = view->runtime->GetRegistry();
  sol::state& lua = *view->runtime->GetLua();
  const FlexDirection defaultDir = (node.tag == "Row") ? FlexDirection::Row : FlexDirection::Column;
  reg->AddComponent(entity, BuildFlexFromStyle(resolved.base, defaultDir));
  AttachVisualBackground(reg, entity, node, resolved);
  if (node.tag == "ScrollView") {
    UIScrollComponent scroll;
    OssParser::ParseFloat(node.GetAttr("wheel-step", "36"), scroll.wheelStep);
    reg->AddComponent(entity, scroll);
  }
  const bool shouldClip = (node.tag == "ScrollView") ||
                          ParseBoolStr(node.GetAttr("clip", resolved.Get("clip", "false"))) ||
                          resolved.Get("overflow") == "hidden";
  if (shouldClip) {
    reg->AddComponent(entity, UIClipComponent{});
  }
  if (node.tag == "RadioGroup") {
    currentRadioGroup = CreateRadioGroupState(view, node, scope, lua);
  }
}

void ConfigureWidgetByTag(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                          const OssResolvedStyle& resolved, const sol::table& scope,
                          std::shared_ptr<RadioGroupState>& currentRadioGroup) {
  Registry* reg = view->runtime->GetRegistry();
  sol::state& lua = *view->runtime->GetLua();
  if (IsContainerTag(node.tag)) {
    ConfigureContainerEntity(view, entity, node, resolved, scope, currentRadioGroup);
  } else if (node.tag == "Label") {
    ConfigureLabelEntity(reg, entity, node, resolved, scope, lua);
  } else if (node.tag == "Image") {
    AttachVisualBackground(reg, entity, node, resolved);
  } else if (node.tag == "Button") {
    AttachVisualBackground(reg, entity, node, resolved);
    ConfigureButtonEntity(view, entity, node, resolved, scope);
  } else if (node.tag == "ProgressBar") {
    ConfigureProgressBarEntity(reg, entity, node, resolved, scope, lua);
  } else if (node.tag == "Toggle") {
    AttachVisualBackground(reg, entity, node, resolved);
    ConfigureToggleEntity(view, entity, node, resolved, scope);
  } else if (node.tag == "RadioButton") {
    AttachVisualBackground(reg, entity, node, resolved);
    ConfigureRadioButtonEntity(view, entity, node, resolved, scope, currentRadioGroup);
  } else if (node.tag == "Slider") {
    ConfigureSliderEntity(view, entity, node, resolved, scope);
  }
}

// NOLINTNEXTLINE(misc-no-recursion)
void InstantiateSingleOrRepeater(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& child,
                                 Entity parentEntity, const sol::table& scope,
                                 const std::vector<OxmlNode>* slotChildren,
                                 const std::shared_ptr<RadioGroupState>& currentRadioGroup) {
  if (child.HasAttr("each")) {
    MountRepeaterNode(view, child, parentEntity, scope, slotChildren, currentRadioGroup);
  } else {
    InstantiateNode(view, child, parentEntity, scope, slotChildren, currentRadioGroup);
  }
}

// NOLINTNEXTLINE(misc-no-recursion)
Entity InstantiateNode(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node, std::optional<Entity> parent,
                       const sol::table& scope, const std::vector<OxmlNode>* slotChildren,
                       const std::shared_ptr<RadioGroupState>& activeRadioGroup) {
  Registry* reg = view->runtime->GetRegistry();
  sol::state& lua = *view->runtime->GetLua();

  if (const auto useIt = view->importedUses.find(node.tag); useIt != view->importedUses.end()) {
    return InstantiateImportedUse(view, node, useIt->second, parent, scope, activeRadioGroup);
  }
  if (const sol::table* customDesc = view->runtime->FindCustomWidget(node.tag); customDesc != nullptr) {
    return InstantiateCustomLuaWidget(view, node, *customDesc, parent, scope, activeRadioGroup);
  }

  const Entity entity = reg->CreateEntity();
  if (parent.has_value()) {
    reg->SetParent(entity, *parent);
  }

  const auto classes = ResolveNodeClasses(node, scope, lua);
  const auto inlineProps = CollectInlineStyleProps(node, scope, lua);
  const OssResolvedStyle resolved = view->stylesheet.Resolve(node.tag, classes, node.GetAttr("id"), inlineProps);

  if (node.tag == "Canvas" || !parent.has_value()) {
    UICanvasComponent canvas(true, 0.0f, 0.0f, 0);
    if (node.HasAttr("base-layer")) {
      float bl = 0.0f;
      if (OssParser::ParseFloat(node.GetAttr("base-layer"), bl)) canvas.baseLayer = static_cast<int>(bl);
    }
    reg->AddComponent(entity, canvas);
  }

  AttachCommonComponents(reg, entity, node, resolved);

  std::shared_ptr<RadioGroupState> currentRadioGroup = activeRadioGroup;
  ConfigureWidgetByTag(view, entity, node, resolved, scope, currentRadioGroup);

  if (node.HasAttr("ref")) {
    view->refsTable.set(node.GetAttr("ref"), CreateRefHandle(view, entity));
  }

  RegisterAttributeBindings(view, entity, node, scope);

  for (const auto& child : node.children) {
    if (child.tag == "Slot") {
      if (slotChildren != nullptr) {
        for (const auto& slotChild : *slotChildren) {
          InstantiateSingleOrRepeater(view, slotChild, entity, scope, nullptr, currentRadioGroup);
        }
      }
      continue;
    }
    InstantiateSingleOrRepeater(view, child, entity, scope, slotChildren, currentRadioGroup);
  }

  return entity;
}

bool IsTableObservable(const sol::table& tbl) { return tbl.valid() && tbl.get_or("__is_observable", false); }

void SubscribeObservableTable(sol::table& obsTbl, const std::function<void(const std::string&)>& fn) {
  sol::protected_function subFn = obsTbl.get<sol::protected_function>("__subscribe");
  if (subFn.valid()) {
    subFn(obsTbl, fn);
  }
}

void LoadStylesAndUsesForView(UIRuntime* runtime, const OxmlDocument& doc, const sol::optional<sol::table>& opts,
                              const std::shared_ptr<UIViewInstance>& view, std::vector<std::string>& watchedPaths) {
  for (const auto& styleImp : doc.styles) {
    std::string styleDiskPath;
    if (const auto ossText = runtime->ReadTextAsset(styleImp.src, &styleDiskPath); ossText.has_value()) {
      const OssParseResult ossRes = OssParser::Parse(*ossText);
      if (ossRes.ok) {
        view->stylesheet.MergeFrom(ossRes.stylesheet);
        if (!styleDiskPath.empty()) watchedPaths.push_back(styleDiskPath);
      } else {
        Logger::Error("ui.mount: OSS parse error in '" + styleImp.src + "': " + ossRes.error);
      }
    } else {
      Logger::Error("ui.mount: missing stylesheet '" + styleImp.src + "'");
    }
  }

  if (opts.has_value()) {
    if (const sol::optional<sol::table> extraStyles = (*opts)["styles"]; extraStyles) {
      for (const auto& [_, val] : *extraStyles) {
        if (!val.is<std::string>()) continue;
        const std::string s = val.as<std::string>();
        std::string styleDiskPath;
        const auto text = (s.find('{') != std::string::npos) ? std::optional<std::string>(s)
                                                             : runtime->ReadTextAsset(s, &styleDiskPath);
        if (text.has_value()) {
          const OssParseResult ossRes = OssParser::Parse(*text);
          if (ossRes.ok) view->stylesheet.MergeFrom(ossRes.stylesheet);
          if (!styleDiskPath.empty()) watchedPaths.push_back(styleDiskPath);
        }
      }
    }
  }

  for (const auto& useImp : doc.uses) {
    std::string useDiskPath;
    const auto useText = (!useImp.src.empty() && useImp.src.front() == '<')
                             ? std::optional<std::string>(useImp.src)
                             : runtime->ReadTextAsset(useImp.src, &useDiskPath);
    if (useText.has_value()) {
      const OxmlParseResult useParsed = OxmlParser::Parse(*useText);
      if (useParsed.ok) {
        view->importedUses[useImp.name] = useParsed.document;
        if (!useDiskPath.empty()) watchedPaths.push_back(useDiskPath);
      } else {
        Logger::Error("ui.mount: failed to parse <Use src='" + useImp.src + "'>: " + useParsed.error);
      }
    } else {
      Logger::Error("ui.mount: missing <Use src='" + useImp.src + "'>");
    }
  }
}

void ApplyMountOptions(const sol::optional<sol::table>& opts, const std::shared_ptr<UIViewInstance>& view,
                       std::optional<Entity>& parentEntity) {
  if (!opts.has_value()) return;
  const sol::table& o = *opts;
  if (const sol::optional<Entity> p = o["parent"]; p) parentEntity = *p;
  if (const sol::optional<sol::table> ctx = o["context"]; ctx) view->context = *ctx;
  if (const sol::optional<sol::table> ctrl = o["controller"]; ctrl) view->controller = *ctrl;
  if (const sol::optional<sol::table> prm = o["params"]; prm) {
    for (const auto& [k, v] : *prm) view->params[k] = v;
  }
}

void PopulateMountViewTable(const std::shared_ptr<UIViewInstance>& view) {
  auto bindContextObservable = [view]() {
    if (IsTableObservable(view->context)) {
      std::weak_ptr<UIViewInstance> weakView = view;
      SubscribeObservableTable(view->context, [weakView](const std::string&) {
        if (auto v = weakView.lock()) {
          v->Refresh();
        }
      });
    }
  };
  bindContextObservable();

  sol::table vt = view->viewTable;
  vt.set("root", view->rootEntity);
  vt.set("refs", view->refsTable);
  vt.set_function("refresh", [view](const sol::table&) { view->Refresh(); });
  vt.set_function("set_context", [view, bindContextObservable](const sol::table&, const sol::table& newCtx) {
    view->context = newCtx;
    bindContextObservable();
    view->Refresh();
  });
  vt.set_function("get_context", [view](const sol::table&) -> sol::object { return view->context; });
  vt.set_function(
      "show", [view](const sol::table&) { SetEntityVisibility(view->runtime->GetRegistry(), view->rootEntity, true); });
  vt.set_function("hide", [view](const sol::table&) {
    SetEntityVisibility(view->runtime->GetRegistry(), view->rootEntity, false);
  });
  vt.set_function("is_mounted", [view](const sol::table&) -> bool { return view->isMounted; });
  vt.set_function("unmount", [view](const sol::table&) { view->Unmount(); });
  vt.set_function("destroy", [view](const sol::table&) { view->Unmount(); });
}

}  // namespace

UIRuntime::UIRuntime(sol::state* lua, Registry* registry, AssetManager* assetManager)
    : lua_(lua), registry_(registry), asset_manager_(assetManager) {}

AssetManager* UIRuntime::GetAssetManager() const {
  if (asset_manager_ != nullptr) return asset_manager_;
  if (registry_ != nullptr) return registry_->TryGet<AssetManager>();
  return nullptr;
}

void UIRuntime::InstallLuaFunctions(sol::table& uiTable) {
  uiTable.set_function("mount", [this](const sol::object& source, const sol::optional<sol::table>& opts) {
    return Mount(source, opts);
  });

  uiTable.set_function("observable", [this](const sol::optional<sol::table>& raw) {
    sol::table base = raw.value_or(lua_->create_table());
    return CreateObservable(base);
  });

  uiTable.set_function("register_widget", [this](const std::string& name, const sol::table& descriptor) {
    RegisterWidget(name, descriptor);
  });
}

// Out-parameter populates the resolved disk path when the asset exists on disk.
// NOLINTNEXTLINE(readability-non-const-parameter)
std::optional<std::string> UIRuntime::ReadTextAsset(const std::string& pathOrId, std::string* outResolvedPath) const {
  AssetManager* am = GetAssetManager();
  if (am == nullptr) return std::nullopt;
  std::string fullPath;
  if (const CatalogEntry* entry = am->GetCatalog().Find(pathOrId); entry != nullptr) {
    fullPath = entry->fullPath;
  } else {
    const std::string filename = fs::path(pathOrId).filename().string();
    if (const CatalogEntry* byFile = am->GetCatalog().Find(filename); byFile != nullptr) {
      fullPath = byFile->fullPath;
    } else {
      fullPath = am->GetFullPath(pathOrId);
    }
  }
  if (fullPath.empty()) return std::nullopt;
  if (outResolvedPath != nullptr) {
    *outResolvedPath = fullPath;
  }
  SDL_IOStream* io = am->OpenAssetIO(fullPath);
  if (io == nullptr) return std::nullopt;
  std::size_t size = 0;
  void* data = SDL_LoadFile_IO(io, &size, true);
  if (data == nullptr) return std::nullopt;
  std::string text(static_cast<const char*>(data), size);
  SDL_free(data);
  return text;
}

const sol::table* UIRuntime::FindCustomWidget(const std::string& name) const {
  const auto it = custom_widgets_.find(name);
  return it != custom_widgets_.end() ? &it->second : nullptr;
}

void UIRuntime::RegisterWidget(const std::string& name, const sol::table& descriptor) {
  if (name.empty() || !descriptor.valid()) {
    Logger::Error("ui.register_widget: invalid name or descriptor");
    return;
  }
  custom_widgets_[name] = descriptor;
}

// NOLINTNEXTLINE(misc-no-recursion)
sol::table UIRuntime::CreateObservable(const sol::table& rawTable) {
  if (IsTableObservable(rawTable)) {
    return rawTable;
  }

  sol::table listeners = lua_->create_table();
  sol::table backing = lua_->create_table();

  auto notifyAll = [listeners](const std::string& key) {
    const std::size_t n = listeners.size();
    for (std::size_t i = 1; i <= n; ++i) {
      sol::object fnObj = listeners[i];
      if (fnObj.is<sol::protected_function>()) {
        fnObj.as<sol::protected_function>()(key);
      }
    }
  };

  for (const auto& [k, v] : rawTable) {
    if (v.is<sol::table>() && !IsTableObservable(v.as<sol::table>())) {
      sol::table childObs = CreateObservable(v.as<sol::table>());
      SubscribeObservableTable(childObs, [notifyAll](const std::string& subKey) { notifyAll(subKey); });
      backing[k] = childObs;
    } else {
      backing[k] = v;
    }
  }

  sol::table proxy = lua_->create_table();
  proxy.set("__is_observable", true);
  proxy.set("__raw", backing);
  proxy.set("__listeners", listeners);
  proxy.set_function("__subscribe", [listeners](const sol::table&, const sol::protected_function& fn) mutable {
    listeners[listeners.size() + 1] = fn;
  });

  sol::table meta = lua_->create_table();
  meta.set_function("__index",
                    [backing](const sol::table&, const sol::object& key) -> sol::object { return backing[key]; });
  meta.set_function("__newindex", [this, backing, notifyAll](const sol::table&, const sol::object& key,
                                                             const sol::object& val) mutable {
    sol::object prev = backing[key];
    if (prev == val && !val.is<sol::table>()) return;
    if (val.is<sol::table>() && !IsTableObservable(val.as<sol::table>())) {
      sol::table childObs = CreateObservable(val.as<sol::table>());
      SubscribeObservableTable(childObs, [notifyAll](const std::string& subKey) { notifyAll(subKey); });
      backing[key] = childObs;
    } else {
      backing[key] = val;
    }
    notifyAll(key.is<std::string>() ? key.as<std::string>() : "");
  });
  meta.set_function("__len", [backing](const sol::table&) -> std::size_t { return backing.size(); });

  proxy[sol::metatable_key] = meta;
  return proxy;
}

sol::object UIRuntime::Mount(const sol::object& sourceObj, const sol::optional<sol::table>& opts) {
  if (!sourceObj.is<std::string>()) {
    Logger::Error("ui.mount: source must be a .oxml path/id or XML string");
    return sol::lua_nil;
  }

  const std::string sourceStr = sourceObj.as<std::string>();
  const std::string_view trimmed = TrimSv(sourceStr);
  std::string xmlContent;
  std::string resolvedMainPath;
  std::vector<std::string> watchedPaths;

  if (!trimmed.empty() && trimmed.front() == '<') {
    xmlContent = sourceStr;
  } else {
    auto loaded = ReadTextAsset(sourceStr, &resolvedMainPath);
    if (!loaded.has_value()) {
      Logger::Error("ui.mount: failed to load OXML asset '" + sourceStr + "'");
      return sol::lua_nil;
    }
    xmlContent = std::move(*loaded);
    if (!resolvedMainPath.empty()) watchedPaths.push_back(resolvedMainPath);
  }

  const OxmlParseResult parsed = OxmlParser::Parse(xmlContent);
  if (!parsed.ok || parsed.document.roots.empty()) {
    Logger::Error("ui.mount: OXML parse error at line " + std::to_string(parsed.errorLine) + ": " + parsed.error);
    return sol::lua_nil;
  }

  auto view = std::make_shared<UIViewInstance>();
  view->runtime = this;
  view->refsTable = lua_->create_table();
  view->viewTable = lua_->create_table();
  view->params = lua_->create_table();

  for (const auto& p : parsed.document.params) {
    view->params[p.name] = p.defaultValue;
  }

  std::optional<Entity> parentEntity;
  ApplyMountOptions(opts, view, parentEntity);
  LoadStylesAndUsesForView(this, parsed.document, opts, view, watchedPaths);

  const sol::table rootScope = view->MakeScope();
  view->rootEntity = InstantiateNode(view, parsed.document.roots.front(), parentEntity, rootScope, nullptr, nullptr);
  PopulateMountViewTable(view);
  view->InvokeController("on_mount");

  if (!watchedPaths.empty()) {
    auto rec = std::make_shared<MountedViewRecord>();
    rec->id = next_view_id_++;
    rec->sourcePath = sourceStr;
    rec->watchedFiles = std::move(watchedPaths);
    std::error_code ec;
    for (const auto& f : rec->watchedFiles) {
      if (fs::exists(f, ec)) {
        rec->lastWriteTimes[f] = fs::last_write_time(f, ec);
      }
    }
    rec->viewTable = view->viewTable;
    mounted_views_.push_back(std::move(rec));
  }

  return view->viewTable;
}

void UIRuntime::PollHotReload() {
  std::error_code ec;
  for (const auto& rec : mounted_views_) {
    if (!rec->viewTable.valid()) continue;
    bool changed = false;
    for (const auto& file : rec->watchedFiles) {
      if (!fs::exists(file, ec)) continue;
      const auto mtime = fs::last_write_time(file, ec);
      if (ec) continue;
      const auto it = rec->lastWriteTimes.find(file);
      if (it == rec->lastWriteTimes.end() || it->second != mtime) {
        rec->lastWriteTimes[file] = mtime;
        changed = true;
      }
    }
    if (changed) {
      Logger::Info("UIRuntime: hot-reloading UI view '" + rec->sourcePath + "'");
      if (sol::optional<sol::protected_function> refresh = rec->viewTable["refresh"]; refresh) {
        (*refresh)(rec->viewTable);
      }
    }
  }
}

int UIRuntime::ValidateCatalogUI(const AssetManager& assetManager) {
  int failures = 0;
  for (const auto& [id, entry] : assetManager.GetCatalog().Entries()) {
    if (entry.type != AssetType::UIView && entry.type != AssetType::UIStyle) continue;
    SDL_IOStream* io = assetManager.OpenAssetIO(entry.fullPath);
    if (io == nullptr) {
      Logger::Error("Bake UI validation: failed to open '" + entry.fullPath + "'");
      ++failures;
      continue;
    }
    std::size_t sz = 0;
    void* data = SDL_LoadFile_IO(io, &sz, true);
    if (data == nullptr) {
      Logger::Error("Bake UI validation: failed to read '" + entry.fullPath + "'");
      ++failures;
      continue;
    }
    std::string content(static_cast<const char*>(data), sz);
    SDL_free(data);

    if (entry.type == AssetType::UIStyle) {
      const OssParseResult res = OssParser::Parse(content);
      if (!res.ok) {
        Logger::Error("Bake UI validation: OSS syntax error in '" + id + "': " + res.error);
        ++failures;
      }
      continue;
    }

    const OxmlParseResult res = OxmlParser::Parse(content);
    if (!res.ok) {
      Logger::Error("Bake UI validation: OXML syntax error in '" + id + "' (line " + std::to_string(res.errorLine) +
                    "): " + res.error);
      ++failures;
      continue;
    }
    const OxmlAssetRefs refs = OxmlParser::CollectStaticAssetRefs(res.document);
    for (const auto& sp : refs.spriteIds) {
      if (!assetManager.GetCatalog().Contains(sp)) {
        Logger::Error("Bake UI validation: OXML '" + id + "' references unknown sprite '" + sp + "'");
        ++failures;
      }
    }
    for (const auto& fnt : refs.fontIds) {
      if (fnt != "default-16" && !assetManager.GetCatalog().Contains(fnt)) {
        Logger::Error("Bake UI validation: OXML '" + id + "' references unknown font '" + fnt + "'");
        ++failures;
      }
    }
  }
  return failures;
}
