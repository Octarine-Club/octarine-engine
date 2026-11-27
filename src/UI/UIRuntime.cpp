#include "UI/UIRuntime.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <unordered_set>

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
#include "Components/ViewportInfo.h"
#include "ECS/Registry.h"
#include "Game/EngineContext.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "Lua/Bindings/TextLabelComponentLuaBinding.h"
#include "Lua/LuaSchemaRegistry.h"
#include "Renderer/SpriteRenderCache.h"
#include "Systems/UIButtonSystem.h"
#ifndef OCTARINE_SHIPPED
#include "UI/OxmlContract.h"
#endif

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
    current = tbl.get<sol::object>(key);
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
    sol::object nextObj = current.get<sol::object>(segment);
    if (!nextObj.is<sol::table>()) {
      sol::state_view luaView(root.lua_state());
      sol::table created = luaView.create_table();
      current[segment] = created;
      nextObj = current.get<sol::object>(segment);
      if (!nextObj.is<sol::table>()) {
        return;
      }
    }
    current = nextObj.as<sol::table>();
    start = dot + 1;
  }
}

void CopyQuotedLiteral(std::string_view expr, std::size_t& i, std::string& out) {
  const char quote = expr[i];
  out.push_back(quote);
  ++i;
  while (i < expr.size()) {
    const char c = expr[i];
    out.push_back(c);
    if (c == '\\' && i + 1 < expr.size()) {
      out.push_back(expr[++i]);
    } else if (c == quote) {
      break;
    }
    ++i;
  }
}

std::string NormalizeLuaExpr(std::string_view expr) {
  std::string out;
  out.reserve(expr.size() + 4);
  for (std::size_t i = 0; i < expr.size(); ++i) {
    const char c = expr[i];
    if (c == '\'' || c == '"') {
      CopyQuotedLiteral(expr, i, out);
    } else if (c == '!' && (i + 1 >= expr.size() || expr[i + 1] != '=')) {
      out += "not ";
    } else if (i + 1 < expr.size() && c == '!' && expr[i + 1] == '=') {
      out += "~=";
      ++i;
    } else if (i + 1 < expr.size() && c == '&' && expr[i + 1] == '&') {
      out += " and ";
      ++i;
    } else if (i + 1 < expr.size() && c == '|' && expr[i + 1] == '|') {
      out += " or ";
      ++i;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool IsIdentStart(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalpha(uc) != 0 || c == '_';
}

bool IsIdentCont(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) != 0 || c == '_';
}

bool IsLuaExprKeyword(std::string_view tok) {
  return tok == "and" || tok == "or" || tok == "not" || tok == "true" || tok == "false" || tok == "nil";
}

void SkipQuotedSpan(std::string_view expr, std::size_t& i) {
  const char quote = expr[i++];
  while (i < expr.size()) {
    if (expr[i] == '\\' && i + 1 < expr.size()) {
      i += 2;
      continue;
    }
    if (expr[i] == quote) break;
    ++i;
  }
}

void ExtractExprIdentifiers(std::string_view expr, std::unordered_set<std::string>& out) {
  for (std::size_t i = 0; i < expr.size(); ++i) {
    const char c = expr[i];
    if (c == '\'' || c == '"') {
      SkipQuotedSpan(expr, i);
      continue;
    }
    if (!IsIdentStart(c)) continue;
    const std::size_t start = i;
    while (i + 1 < expr.size() && IsIdentCont(expr[i + 1])) {
      ++i;
    }
    const std::string_view tok = expr.substr(start, i - start + 1);
    if (!IsLuaExprKeyword(tok)) {
      out.emplace(tok);
    }
  }
}

void ExtractInterpolatedIdentifiers(std::string_view raw, std::unordered_set<std::string>& out) {
  std::size_t i = 0;
  while (i < raw.size()) {
    const std::size_t open = raw.find('{', i);
    if (open == std::string_view::npos) break;
    const std::size_t close = raw.find('}', open + 1);
    if (close == std::string_view::npos) break;
    ExtractExprIdentifiers(raw.substr(open + 1, close - open - 1), out);
    i = close + 1;
  }
}

std::string LastDotPathSegment(std::string_view path) {
  const std::size_t dot = path.rfind('.');
  return std::string(dot == std::string_view::npos ? path : path.substr(dot + 1));
}

constexpr const char* kUiExprStateRegKey = "__octarine_ui_expr_state";
constexpr const char* kMountedViewsRegKey = "__octarine_mounted_views";
constexpr std::size_t kMaxCachedUiExpressions = 4096;

sol::table GetOrInitUiExprState(sol::state& lua) {
  sol::table reg = lua.registry();
  sol::object existing = reg.raw_get<sol::object>(kUiExprStateRegKey);
  if (existing.is<sol::table>()) {
    return existing.as<sol::table>();
  }

  sol::table state = lua.create_table();
  sol::table cache = lua.create_table();
  sol::table env = lua.create_table();
  sol::table meta = lua.create_table();
  meta.set_function("__index",
                    [state](const sol::table&, const std::string& key, const sol::this_state s) -> sol::object {
                      const sol::object activeScope = state.raw_get<sol::object>("scope");
                      if (activeScope.is<sol::table>()) {
                        const sol::object v = activeScope.as<sol::table>().get<sol::object>(key);
                        if (v.valid() && v != sol::lua_nil) return v;
                      }
                      sol::state_view sv(s);
                      return sv.globals().get<sol::object>(key);
                    });
  env[sol::metatable_key] = meta;
  state.raw_set("cache", cache);
  state.raw_set("env", env);
  state.raw_set("count", 0);
  reg.raw_set(kUiExprStateRegKey, state);
  return state;
}

sol::object CompileOrGetCachedUiExpr(sol::state& lua, sol::table& state, std::string_view trimmed) {
  sol::table cache = state.raw_get<sol::table>("cache");
  const std::string normExpr = NormalizeLuaExpr(trimmed);
  sol::object cached = cache.raw_get<sol::object>(normExpr);
  if (cached.valid() && cached != sol::lua_nil) {
    return cached;
  }

  const int count = state.raw_get_or("count", 0);
  if (static_cast<std::size_t>(count) >= kMaxCachedUiExpressions) {
    cache = lua.create_table();
    state.raw_set("cache", cache);
    state.raw_set("count", 0);
  }

  lua_State* L = lua.lua_state();
  const std::string code = "return (" + normExpr + ")";
  if (luaL_loadbufferx(L, code.data(), code.size(), "=oxml_expr", "t") != LUA_OK) {
    lua_pop(L, 1);
    cache.raw_set(normExpr, false);
    state.raw_set("count", state.raw_get_or("count", 0) + 1);
    return sol::make_object(lua, false);
  }

  sol::table env = state.raw_get<sol::table>("env");
  env.push(L);
  lua_setupvalue(L, -2, 1);
  sol::protected_function fn(L, -1);
  lua_pop(L, 1);

  sol::object fnObj = sol::make_object(lua, fn);
  cache.raw_set(normExpr, fnObj);
  state.raw_set("count", state.raw_get_or("count", 0) + 1);
  return fnObj;
}

sol::object EvalBindingExpr(sol::state& lua, std::string_view rawExpr, const sol::table& scope) {
  const std::string_view trimmed = TrimSv(rawExpr);
  if (trimmed.empty()) return sol::lua_nil;
  if (IsSimpleDotPath(trimmed)) {
    return GetPathValue(scope, trimmed);
  }

  sol::table state = GetOrInitUiExprState(lua);
  const sol::object fnObj = CompileOrGetCachedUiExpr(lua, state, trimmed);
  if (!fnObj.is<sol::protected_function>()) {
    return sol::lua_nil;
  }

  const sol::object prevScope = state.raw_get<sol::object>("scope");
  state.raw_set("scope", scope);
  const sol::protected_function fn = fnObj.as<sol::protected_function>();
  const sol::protected_function_result res = fn();
  state.raw_set("scope", prevScope);

  if (!res.valid() || res.return_count() == 0) {
    return sol::lua_nil;
  }
  return res.get<sol::object>();
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

void ApplyFirstMatchingFloatProp(const std::map<std::string, std::string>& props,
                                 std::initializer_list<const char*> keys, float& target) {
  for (const char* key : keys) {
    if (const auto it = props.find(key); it != props.end()) {
      OssParser::ParseFloat(it->second, target);
      return;
    }
  }
}

void ApplyFlexWrapAndColumnsProps(const std::map<std::string, std::string>& props, UIFlexComponent& flex) {
  float colsF = 0.0f;
  ApplyFirstMatchingFloatProp(props, {"columns", "grid-columns"}, colsF);
  if (colsF > 0.0f) {
    flex.columns = static_cast<std::uint16_t>(colsF);
    flex.wrap = true;
  }
  if (const auto it = props.find("flex-wrap"); it != props.end()) {
    const std::string_view fw = TrimSv(it->second);
    flex.wrap = (fw == "wrap" || ParseBoolStr(fw, flex.wrap));
  } else if (const auto wIt = props.find("wrap"); wIt != props.end()) {
    const std::string_view wv = TrimSv(wIt->second);
    if (wv == "wrap" || wv == "nowrap" || wv == "true" || wv == "false") {
      flex.wrap = (wv == "wrap" || wv == "true");
    }
  }
}

UIFlexComponent BuildFlexFromStyle(const std::map<std::string, std::string>& props, FlexDirection defaultDir,
                                   bool isGrid = false) {
  UIFlexComponent flex;
  flex.direction = defaultDir;
  flex.wrap = isGrid;
  if (const auto it = props.find("flex-direction"); it != props.end()) {
    flex.direction = ParseFlexDirStr(it->second, defaultDir);
  } else if (const auto dirIt = props.find("direction"); dirIt != props.end()) {
    flex.direction = ParseFlexDirStr(dirIt->second, defaultDir);
  }

  ApplyFirstMatchingFloatProp(props, {"gap", "spacing"}, flex.gap);
  ApplyFirstMatchingFloatProp(props, {"cross-gap", "row-gap", "column-gap"}, flex.crossGap);
  ApplyFlexWrapAndColumnsProps(props, flex);

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
  if (!reg->HasComponent<UIFocusComponent>(entity)) {
    reg->AddComponent(entity, UIFocusComponent{});
  }
  auto& foc = reg->GetComponent<UIFocusComponent>(entity);
  foc.isDisabled = disabled;
  if (disabled) {
    foc.isFocused = false;
  }
  if (reg->HasComponent<UIButtonComponent>(entity)) {
    auto& btn = reg->GetComponent<UIButtonComponent>(entity);
    btn.isActive = !disabled && !foc.isInert;
    if (!btn.isActive) {
      btn.isHovered = false;
      btn.isPressed = false;
    }
  }
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

void SetEntityInert(Registry* reg, Entity entity, bool inert, bool forceFocusComponent = false) {
  if (!reg || !reg->IsAlive(entity)) return;
  const bool hasButton = reg->HasComponent<UIButtonComponent>(entity);
  const bool hasFocus = reg->HasComponent<UIFocusComponent>(entity);
  if (!hasButton && !hasFocus && !forceFocusComponent) return;
  if (!hasFocus) {
    UIFocusComponent foc{};
    foc.isFocusable = hasButton;
    reg->AddComponent(entity, foc);
  }
  auto& foc = reg->GetComponent<UIFocusComponent>(entity);
  foc.isInert = inert;
  if (inert) {
    foc.isFocused = false;
  }
  if (hasButton) {
    auto& btn = reg->GetComponent<UIButtonComponent>(entity);
    btn.isActive = !inert && !foc.isDisabled;
    if (inert) {
      btn.isHovered = false;
      btn.isPressed = false;
    }
  }
}

bool IsEntityInert(Registry* reg, Entity entity) {
  if (!reg || !reg->IsAlive(entity)) return false;
  std::optional<Entity> curr = entity;
  while (curr.has_value() && reg->IsAlive(*curr)) {
    if (reg->HasComponent<UIFocusComponent>(*curr) && reg->GetComponent<UIFocusComponent>(*curr).isInert) {
      return true;
    }
    curr = reg->GetParent(*curr);
  }
  return false;
}

void SetSubtreeInert(Registry* reg, Entity root, bool inert) {
  if (!reg || !reg->IsAlive(root)) return;
  std::vector<Entity> stack{root};
  while (!stack.empty()) {
    const Entity curr = stack.back();
    stack.pop_back();
    if (!reg->IsAlive(curr)) continue;
    SetEntityInert(reg, curr, inert, curr == root);
    reg->ForEachChild(curr, [&stack](const Entity child) { stack.push_back(child); });
  }
}

bool ParsePointerEventsBlocks(std::string_view raw) {
  const std::string_view v = TrimSv(raw);
  return v == "stop" || v == "block" || v == "all";
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

struct RefreshEntry {
  std::unordered_set<std::string> deps{};
  std::string producesKey{};
  std::function<void(std::unordered_set<std::string>*)> fn{};
};

bool MatchesAnyDep(const std::unordered_set<std::string>& deps, const std::unordered_set<std::string>& dirtyKeys) {
  return std::ranges::any_of(dirtyKeys, [&deps](const std::string& k) { return deps.contains(k); });
}

struct RepeaterRecord {
  Entity parentEntity{};
  OxmlNode itemTemplate{};
  OxmlEachClause clause{};
  std::unordered_set<std::string> listDeps{};
  std::vector<Entity> spawnedEntities{};
  std::vector<RefreshEntry> itemRefreshEntries{};
};

struct ChildOrderSlot {
  bool isRepeater{false};
  Entity staticChild{};
  std::weak_ptr<RepeaterRecord> repeater{};
};

sol::object LookupKeyInTable(const sol::table& tbl, const std::string& key) {
  if (!tbl.valid()) return sol::lua_nil;
  sol::object v = tbl.get<sol::object>(key);
  return (v.valid() && v != sol::lua_nil) ? v : sol::object(sol::lua_nil);
}

struct UIViewInstance;

void LogIfLuaError(const sol::protected_function_result& res, const char* context) {
  if (!res.valid()) {
    const sol::error err = res;
    Logger::ErrorLua(std::string(context) + ": " + err.what());
  }
}

void RunRefreshEntries(const UIViewInstance& view, const std::vector<RefreshEntry>& entries,
                       std::unordered_set<std::string>* dirtyKeys);

void PinMountedView(sol::state* lua, uint64_t viewId, const std::shared_ptr<UIViewInstance>& view);
void UnpinMountedView(sol::state* lua, uint64_t viewId);

struct UIViewInstance : std::enable_shared_from_this<UIViewInstance> {
  UIRuntime* runtime{nullptr};
  uint64_t viewId{0};
  Entity rootEntity{};
  sol::table context{};
  sol::table params{};
  sol::table controller{};
  sol::table refsTable{};
  sol::table viewTable{};
  sol::table paramOverrides{};
  OssStylesheet stylesheet{};
  std::vector<std::string> extraStyleSources{};
  std::unordered_map<std::string, OxmlDocument> importedUses{};
#ifndef OCTARINE_SHIPPED
  OxmlDocument document{};
#endif
  std::vector<RefreshEntry> refreshEntries{};
  std::vector<RefreshEntry>* activeRefreshSink{nullptr};
  std::vector<std::function<void()>> unmountFns{};
  std::unordered_map<EntityID, std::shared_ptr<SliderState>> sliders{};
  std::unordered_map<EntityID, std::vector<sol::protected_function>> changeCallbacks{};
  std::unordered_map<EntityID, std::vector<ChildOrderSlot>> parentChildSlots{};
  std::vector<Entity> inertSubtreeRoots{};
  std::vector<std::string> acquiredAssetIds{};
  bool isMounted = true;
  bool isBuildingTree = false;
  bool isRefreshing = false;
  bool isInert = false;

  [[nodiscard]] bool IsActiveOrBuilding() const { return isMounted && (isBuildingTree || IsCurrentlyMounted()); }

  struct BuildingTreeGuard {
    UIViewInstance& view;
    explicit BuildingTreeGuard(UIViewInstance& v) : view(v) { view.isBuildingTree = true; }
    ~BuildingTreeGuard() { view.isBuildingTree = false; }
    BuildingTreeGuard(const BuildingTreeGuard&) = delete;
    BuildingTreeGuard& operator=(const BuildingTreeGuard&) = delete;
    BuildingTreeGuard(BuildingTreeGuard&&) = delete;
    BuildingTreeGuard& operator=(BuildingTreeGuard&&) = delete;
  };

  void SetInert(const bool inert) {
    isInert = inert;
    if (!IsCurrentlyMounted()) return;
    SetSubtreeInert(runtime->GetRegistry(), rootEntity, inert);
    if (!inert) {
      for (const Entity subRoot : inertSubtreeRoots) {
        if (runtime->GetRegistry()->IsAlive(subRoot)) {
          SetSubtreeInert(runtime->GetRegistry(), subRoot, true);
        }
      }
    }
  }

  // Mutates inertSubtreeRoots; clang-tidy false-positives when macOS SDK headers fail to parse std::vector<Entity>.
  // NOLINTNEXTLINE(readability-make-member-function-const)
  void SetRefSubtreeInert(const Entity entity, const bool inert) {
    std::erase(inertSubtreeRoots, entity);
    if (inert) {
      inertSubtreeRoots.push_back(entity);
    }
    if (!IsCurrentlyMounted()) return;
    SetSubtreeInert(runtime->GetRegistry(), entity, inert || isInert);
  }

  void AddRefreshFn(std::function<void()> fn, std::unordered_set<std::string> deps = {}, std::string producesKey = {}) {
    auto& target = (activeRefreshSink != nullptr) ? *activeRefreshSink : refreshEntries;
    target.push_back(RefreshEntry{
        .deps = std::move(deps),
        .producesKey = std::move(producesKey),
        .fn = [cb = std::move(fn)](std::unordered_set<std::string>*) { cb(); },
    });
  }

  void AddRefreshEntry(RefreshEntry entry) {
    auto& target = (activeRefreshSink != nullptr) ? *activeRefreshSink : refreshEntries;
    target.push_back(std::move(entry));
  }

  [[nodiscard]] sol::table MakeScope(const sol::table& localVars = sol::lua_nil) const {
    sol::state& lua = *runtime->GetLua();
    sol::table scope = lua.create_table();
    sol::table meta = lua.create_table();
    const std::weak_ptr<const UIViewInstance> weakSelf = weak_from_this();
    meta.set_function("__index", [weakSelf, localVars](const sol::table&, const std::string& key) -> sol::object {
      if (const sol::object v = LookupKeyInTable(localVars, key); v != sol::lua_nil) return v;
      const auto self = weakSelf.lock();
      if (!self) return sol::lua_nil;
      if (const sol::object v = LookupKeyInTable(self->context, key); v != sol::lua_nil) return v;
      return LookupKeyInTable(self->params, key);
    });
    scope[sol::metatable_key] = meta;
    return scope;
  }

  void InvokeController(const std::string& methodName, const sol::object& arg1 = sol::lua_nil) {
    if (methodName.empty() || !controller.valid()) return;
    std::string resolvedMethod = methodName;
    const size_t paren = resolvedMethod.find('(');
    if (paren != std::string::npos) {
      resolvedMethod = resolvedMethod.substr(0, paren);
    }
    sol::object fnObj = controller.get<sol::object>(resolvedMethod);
    if (!fnObj.is<sol::protected_function>()) {
      fnObj = controller.get<sol::object>(methodName);
    }
    if (!fnObj.is<sol::protected_function>()) return;
    sol::protected_function fn = fnObj.as<sol::protected_function>();
    const auto res = arg1.valid() && arg1 != sol::lua_nil ? fn(controller, arg1, viewTable) : fn(controller, viewTable);
    if (!res.valid()) {
      const sol::error err = res;
      Logger::Error("UI controller '" + resolvedMethod + "' error: " + err.what());
    }
  }

  [[nodiscard]] bool IsCurrentlyMounted() const {
    if (!isMounted || runtime == nullptr) return false;
    const Registry* reg = runtime->GetRegistry();
    return reg != nullptr && reg->IsAlive(rootEntity);
  }

  void SyncInertAfterRefresh() {
    if (!IsCurrentlyMounted()) return;
    if (isInert) {
      SetSubtreeInert(runtime->GetRegistry(), rootEntity, true);
    } else if (!inertSubtreeRoots.empty()) {
      Registry* reg = runtime->GetRegistry();
      std::erase_if(inertSubtreeRoots, [reg](const Entity e) { return !reg->IsAlive(e); });
      for (const Entity subRoot : inertSubtreeRoots) {
        SetSubtreeInert(reg, subRoot, true);
      }
    }
  }

  void RefreshKey(const std::string& changedKey) {
    if (!IsCurrentlyMounted() || isRefreshing) {
      if (!IsCurrentlyMounted()) isMounted = false;
      return;
    }
    isRefreshing = true;
    const auto entries = refreshEntries;
    if (changedKey.empty()) {
      RunRefreshEntries(*this, entries, nullptr);
    } else {
      std::unordered_set<std::string> dirtyKeys{changedKey};
      RunRefreshEntries(*this, entries, &dirtyKeys);
    }
    SyncInertAfterRefresh();
    isRefreshing = false;
  }

  void Refresh() { RefreshKey(""); }

  void Unmount() {
    if (!isMounted) return;
    const auto self = shared_from_this();
    isMounted = false;
    InvokeController("on_unmount");
    for (auto& fn : unmountFns) {
      fn();
    }
    unmountFns.clear();
    refreshEntries.clear();
    sliders.clear();
    changeCallbacks.clear();
    parentChildSlots.clear();
    inertSubtreeRoots.clear();
    if (runtime != nullptr && !acquiredAssetIds.empty()) {
      if (AssetManager* am = runtime->GetAssetManager()) {
        am->ReleaseAll(acquiredAssetIds);
      }
      acquiredAssetIds.clear();
    }
    Registry* reg = runtime != nullptr ? runtime->GetRegistry() : nullptr;
    if (reg != nullptr && reg->IsAlive(rootEntity)) {
      reg->BlamEntity(rootEntity);
    }
    if (runtime != nullptr) {
      UnpinMountedView(runtime->GetLua(), viewId);
    }
    context = sol::lua_nil;
    params = sol::lua_nil;
    controller = sol::lua_nil;
    refsTable = sol::lua_nil;
    viewTable = sol::lua_nil;
    paramOverrides = sol::lua_nil;
  }
};

void RunRefreshEntries(const UIViewInstance& view, const std::vector<RefreshEntry>& entries,
                       std::unordered_set<std::string>* dirtyKeys) {
  for (const auto& entry : entries) {
    if (!view.IsActiveOrBuilding()) {
      return;
    }
    if (dirtyKeys != nullptr && !entry.deps.empty() && !MatchesAnyDep(entry.deps, *dirtyKeys)) {
      continue;
    }
    entry.fn(dirtyKeys);
    if (dirtyKeys != nullptr && !entry.producesKey.empty()) {
      dirtyKeys->insert(entry.producesKey);
    }
  }
}

std::shared_ptr<UIViewInstance> LockActiveView(const std::weak_ptr<UIViewInstance>& weakView) {
  auto v = weakView.lock();
  if (!v || !v->IsActiveOrBuilding()) return nullptr;
  return v;
}

std::shared_ptr<UIViewInstance> LockMountedView(const std::weak_ptr<UIViewInstance>& weakView) {
  auto v = weakView.lock();
  if (!v || !v->IsCurrentlyMounted()) return nullptr;
  return v;
}

void PinMountedView(sol::state* lua, const uint64_t viewId, const std::shared_ptr<UIViewInstance>& view) {
  if (lua == nullptr || viewId == 0) return;
  sol::table reg = lua->registry();
  sol::object tblObj = reg.raw_get<sol::object>(kMountedViewsRegKey);
  sol::table tbl = tblObj.is<sol::table>() ? tblObj.as<sol::table>() : lua->create_table();
  if (!tblObj.is<sol::table>()) {
    reg.raw_set(kMountedViewsRegKey, tbl);
  }
  tbl.raw_set(viewId, view);
}

void UnpinMountedView(sol::state* lua, const uint64_t viewId) {
  if (lua == nullptr || viewId == 0) return;
  sol::table reg = lua->registry();
  sol::object tblObj = reg.raw_get<sol::object>(kMountedViewsRegKey);
  if (tblObj.is<sol::table>()) {
    tblObj.as<sol::table>().raw_set(viewId, sol::lua_nil);
  }
}

constexpr const char* kStyleAttrs[] = {
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
    "cross-gap",
    "row-gap",
    "column-gap",
    "columns",
    "grid-columns",
    "flex-wrap",
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
    "fill-color",
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
    "sound-hover",
    "sound-click",
    "sound-press",
    "sound-focus",
    "shadow-color",
    "shadow-offset",
    "outline",
    "outline-color",
    "outline-width",
    "outline-thickness",
    "text-outline",
    "text-outline-color",
    "text-outline-width",
    "pointer-events",
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
  return std::ranges::any_of(kStyleAttrs, [&node](const char* key) {
    const auto it = node.attributes.find(key);
    return it != node.attributes.end() && OxmlParser::ContainsBinding(it->second);
  });
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
  UIRectComponent rect{};
  rect.blocksPointer = ParsePointerEventsBlocks(node.GetAttr("pointer-events", resolved.Get("pointer-events")));
  reg->AddComponent(entity, rect);
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

  std::string spriteId = node.GetAttr("sprite", resolved.Get("sprite"));
  if (spriteId.empty() && node.tag == "Image") {
    spriteId = node.GetAttr("src");
  }
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

void ApplyLabelShadowEffects(const OssResolvedStyle& resolved, TextLabelComponent& lbl) {
  lbl.shadowColor = octarine::Color{0, 0, 0, 200};
  lbl.shadowOffset = glm::vec2{0.0f, 0.0f};

  const std::string shadowColStr = resolved.Get("shadow-color");
  if (!shadowColStr.empty()) {
    octarine::Color sc{0, 0, 0, 200};
    if (OssParser::ParseColor(shadowColStr, sc)) {
      lbl.shadowColor = sc;
    }
  }
  const std::string shadowOffStr = resolved.Get("shadow-offset");
  if (!shadowOffStr.empty()) {
    float offX = 0.0f;
    float offY = 0.0f;
    float unusedB = 0.0f;
    float unusedL = 0.0f;
    if (OssParser::ParseEdges(shadowOffStr, offX, offY, unusedB, unusedL)) {
      lbl.shadowOffset = glm::vec2(offX, offY);
    }
  }
}

void ApplyOutlineWidthToken(const std::string& outWidthStr, TextLabelComponent& lbl) {
  if (outWidthStr == "false" || outWidthStr == "0" || outWidthStr == "none") {
    lbl.outlineWidth = 0.0f;
    return;
  }
  if (outWidthStr == "true") {
    lbl.outlineWidth = 1.0f;
    return;
  }
  float ow = 0.0f;
  if (OssParser::ParseFloat(outWidthStr, ow)) {
    lbl.outlineWidth = std::max(0.0f, ow);
    return;
  }
  octarine::Color oc{0, 0, 0, 255};
  if (OssParser::ParseColor(outWidthStr, oc)) {
    lbl.outlineColor = oc;
    lbl.outlineWidth = oc.a > 0 ? 1.0f : 0.0f;
  }
}

void ApplyLabelOutlineEffects(const OssResolvedStyle& resolved, TextLabelComponent& lbl) {
  lbl.outlineColor = octarine::Color{0, 0, 0, 255};
  lbl.outlineWidth = 0.0f;

  const std::string outColStr = resolved.Get("outline-color", resolved.Get("text-outline-color"));
  bool hasOutlineColor = false;
  if (!outColStr.empty()) {
    octarine::Color oc{0, 0, 0, 255};
    if (OssParser::ParseColor(outColStr, oc)) {
      lbl.outlineColor = oc;
      hasOutlineColor = (oc.a > 0);
    }
  }

  std::string outWidthStr =
      resolved.Get("outline-width", resolved.Get("outline-thickness", resolved.Get("text-outline-width")));
  if (outWidthStr.empty()) {
    outWidthStr = resolved.Get("outline", resolved.Get("text-outline"));
  }
  if (!outWidthStr.empty()) {
    ApplyOutlineWidthToken(outWidthStr, lbl);
  } else if (hasOutlineColor) {
    lbl.outlineWidth = 1.0f;
  }
}

void ConfigureLabelEffects(const OssResolvedStyle& resolved, TextLabelComponent& lbl) {
  ApplyLabelShadowEffects(resolved, lbl);
  ApplyLabelOutlineEffects(resolved, lbl);
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
  ConfigureLabelEffects(resolved, lbl);
  reg->AddComponent(entity, lbl);
}

std::string StripStyleQuotes(std::string_view raw) {
  std::string_view v = TrimSv(raw);
  if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
    v = TrimSv(v.substr(1, v.size() - 2));
  }
  return std::string(v);
}

std::string LookupStyleProp(const std::map<std::string, std::string>& props, const std::string& key) {
  const auto it = props.find(key);
  return it != props.end() ? StripStyleQuotes(it->second) : "";
}

void ApplyButtonSounds(const OxmlNode& node, const OssResolvedStyle& resolved, UIButtonComponent& btn) {
  std::string hover = StripStyleQuotes(node.GetAttr("sound-hover", resolved.Get("sound-hover")));
  if (hover.empty()) hover = LookupStyleProp(resolved.hover, "sound-hover");
  if (hover.empty()) hover = LookupStyleProp(resolved.hover, "sound");

  std::string click = StripStyleQuotes(node.GetAttr(
      "sound-click", node.GetAttr("sound-press", resolved.Get("sound-click", resolved.Get("sound-press")))));
  if (click.empty()) click = LookupStyleProp(resolved.press, "sound-click");
  if (click.empty()) click = LookupStyleProp(resolved.press, "sound-press");
  if (click.empty()) click = LookupStyleProp(resolved.press, "sound");

  std::string focus = StripStyleQuotes(node.GetAttr("sound-focus", resolved.Get("sound-focus")));
  if (focus.empty()) focus = LookupStyleProp(resolved.focus, "sound-focus");
  if (focus.empty()) focus = LookupStyleProp(resolved.focus, "sound");

  btn.hoverSound = std::move(hover);
  btn.clickSound = std::move(click);
  btn.focusSound = std::move(focus);
}

void ApplyButtonInputAttrs(const OxmlNode& node, UIButtonComponent& btn) {
  btn.key = node.GetAttr("key");
  btn.controllerButton = node.GetAttr("gamepad", node.GetAttr("gamepad-button", node.GetAttr("controller-button")));
  btn.action = node.GetAttr("action");
}

sol::protected_function MakeControllerItemCallback(const std::shared_ptr<UIViewInstance>& view,
                                                   const std::string& methodName, const sol::table& scope,
                                                   sol::state& lua) {
  if (methodName.empty()) return sol::lua_nil;
  std::weak_ptr<UIViewInstance> weakView = view;
  return lua
      .create_table_with("fn",
                         [weakView, methodName, scope]() {
                           if (auto v = weakView.lock()) {
                             sol::object itemArg = LookupKeyInTable(scope, "item");
                             if (itemArg == sol::lua_nil) {
                               itemArg = LookupKeyInTable(scope, "__item");
                             }
                             v->InvokeController(methodName, itemArg);
                           }
                         })
      .get<sol::protected_function>("fn");
}

void ApplyButtonFocusAttr(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node, UIButtonComponent& btn,
                          const sol::table& scope, sol::state& lua) {
  if (node.HasAttr("on-focus")) {
    btn.focusFunction = MakeControllerItemCallback(view, node.GetAttr("on-focus"), scope, lua);
  }
}

bool IsFocusableEntity(Registry* r, const Entity entity) {
  if (!r || !r->IsAlive(entity) || !r->HasComponent<UIFocusComponent>(entity)) return false;
  if (r->HasComponent<UIRectComponent>(entity) && !r->GetComponent<UIRectComponent>(entity).visible) return false;
  const auto& foc = r->GetComponent<UIFocusComponent>(entity);
  return foc.isFocusable && !foc.isDisabled && !foc.isInert;
}

bool SubtreeHasFocus(Registry* r, const Entity root) {
  if (!r || !r->IsAlive(root)) return false;
  std::vector<Entity> stack{root};
  while (!stack.empty()) {
    const Entity cur = stack.back();
    stack.pop_back();
    if (!r->IsAlive(cur)) continue;
    if (r->HasComponent<UIFocusComponent>(cur) && r->GetComponent<UIFocusComponent>(cur).isFocused) {
      return true;
    }
    const auto& children = r->GetChildren(cur);
    stack.insert(stack.end(), children.begin(), children.end());
  }
  return false;
}

// Depth-first tree scan bounded by UI hierarchy depth.
// NOLINTNEXTLINE(misc-no-recursion)
std::optional<Entity> FindFirstFocusableInSubtree(Registry* r, const Entity root) {
  if (!r || !r->IsAlive(root)) return std::nullopt;
  if (r->HasComponent<UIRectComponent>(root) && !r->GetComponent<UIRectComponent>(root).visible) {
    return std::nullopt;
  }
  if (IsFocusableEntity(r, root)) return root;
  for (const Entity child : r->GetChildren(root)) {
    if (const auto found = FindFirstFocusableInSubtree(r, child)) {
      return found;
    }
  }
  return std::nullopt;
}

void FocusEntitySafe(Registry* r, const Entity target, const bool fireFocusCallback = true) {
  if (!r || !r->IsAlive(target)) return;
  if (auto* btnSys = r->TryGet<UIButtonSystem>()) {
    btnSys->SetFocus(target, fireFocusCallback);
  } else if (r->HasComponent<UIFocusComponent>(target)) {
    auto q = r->CreateQuery<UIFocusComponent>();
    q->Update();
    q->ForEach([target](const Entity e, UIFocusComponent& foc) {
      foc.isFocused = (e == target && foc.isFocusable && !foc.isDisabled && !foc.isInert);
    });
  }
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

  octarine::Color bg{42, 34, 62, 255};
  ExtractBgColor(resolved.base, bg);
  octarine::Color border{90, 75, 130, 255};
  OssParser::ParseColor(resolved.Get("border-color"), border);
  float bw = 1.0f;
  OssParser::ParseFloat(resolved.Get("border-width", "1"), bw);
  if (!reg->HasComponent<SquarePrimitiveComponent>(entity)) {
    SquarePrimitiveComponent sq;
    sq.color = bg;
    sq.borderColor = border;
    sq.borderThickness = bw;
    reg->AddComponent(entity, sq);
  } else {
    auto& sq = reg->GetComponent<SquarePrimitiveComponent>(entity);
    if (sq.color.a == 0 && sq.borderThickness <= 0.0f) {
      sq.color = bg;
      sq.borderColor = border;
      sq.borderThickness = bw;
    }
  }
  if (node.HasAttr("text") || !node.innerText.empty()) {
    ConfigureLabelEntity(reg, entity, node, resolved, scope, lua);
  }

  const bool isDisabled = ParseBoolStr(node.GetAttr("disabled", resolved.Get("disabled", "false")), false);
  UIButtonComponent btn;
  btn.isActive = !isDisabled;
  ApplyButtonInputAttrs(node, btn);
  btn.clickFunction = MakeControllerItemCallback(view, node.GetAttr("on-click"), scope, lua);
  ApplyButtonFocusAttr(view, node, btn, scope, lua);

  ApplyButtonSounds(node, resolved, btn);
  reg->AddComponent(entity, btn);
  ConfigureInteractiveFocus(reg, entity, node, resolved);
  SetEntityDisabled(reg, entity, isDisabled);
}

void ConfigureProgressBarEntity(Registry* reg, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved,
                                const sol::table& scope, sol::state& lua) {
  octarine::Color fillColor{6, 214, 160, 255};
  const std::string rawFillColor =
      node.GetAttr("fill-color", resolved.Get("fill-color", node.GetAttr("color", resolved.Get("color"))));
  if (!OssParser::ParseColor(rawFillColor, fillColor)) {
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
  octarine::Color bg{38, 32, 54, 255};
  ExtractBgColor(resolved.base, bg);
  octarine::Color border{100, 90, 140, 255};
  OssParser::ParseColor(resolved.Get("border-color"), border);
  if (!reg->HasComponent<SquarePrimitiveComponent>(entity)) {
    SquarePrimitiveComponent sq;
    sq.color = bg;
    sq.borderColor = border;
    sq.borderThickness = 1.0f;
    reg->AddComponent(entity, sq);
  } else {
    auto& sq = reg->GetComponent<SquarePrimitiveComponent>(entity);
    if (sq.color.a == 0 && sq.borderThickness <= 0.0f) {
      sq.color = bg;
      sq.borderColor = border;
      sq.borderThickness = 1.0f;
    }
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
    v->RefreshKey(LastDotPathSegment(bindPath));
  }
  v->InvokeController(onChangeMethod, valObj);
  if (!v->IsCurrentlyMounted()) return;
  if (const auto it = v->changeCallbacks.find(entity.id); it != v->changeCallbacks.end()) {
    const auto cbs = it->second;
    for (const auto& cb : cbs) {
      if (!v->IsCurrentlyMounted()) break;
      LogIfLuaError(cb(nextChecked), "UI toggle on-change");
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
  ApplyButtonInputAttrs(node, btn);
  std::weak_ptr<UIViewInstance> weakView = view;
  btn.clickFunction = lua.create_table_with("fn",
                                            [weakView, entity, bindPath, onChangeMethod]() {
                                              if (auto v = weakView.lock()) {
                                                HandleToggleClick(v, entity, bindPath, onChangeMethod);
                                              }
                                            })
                          .get<sol::protected_function>("fn");

  ApplyButtonSounds(node, resolved, btn);
  ApplyButtonFocusAttr(view, node, btn, scope, lua);
  reg->AddComponent(entity, btn);
  SetEntityDisabled(reg, entity, isDisabled);

  if (!bindPath.empty()) {
    std::unordered_set<std::string> deps;
    ExtractExprIdentifiers(bindPath, deps);
    view->AddRefreshFn(
        [weakView, entity, bindPath]() {
          auto v = LockActiveView(weakView);
          if (!v || !v->context.valid()) return;
          SetEntityChecked(v->runtime->GetRegistry(), entity, ObjectToBool(GetPathValue(v->context, bindPath)));
        },
        std::move(deps));
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
      v->RefreshKey(LastDotPathSegment(radioGroup->bindPath));
    }
    v->InvokeController(radioGroup->onChangeMethod, valObj);
  } else {
    SetEntityChecked(r, entity, true);
  }

  v->InvokeController(onClickMethod, valObj);
  if (!v->IsCurrentlyMounted()) return;
  if (const auto it = v->changeCallbacks.find(entity.id); it != v->changeCallbacks.end()) {
    const auto cbs = it->second;
    for (const auto& cb : cbs) {
      if (!v->IsCurrentlyMounted()) break;
      LogIfLuaError(cb(optionValue), "UI radio on-change");
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
  ApplyButtonInputAttrs(node, btn);
  std::weak_ptr<UIViewInstance> weakView = view;
  btn.clickFunction =
      lua.create_table_with("fn",
                            [weakView, entity, optionValue, radioGroup, onClickMethod]() {
                              if (auto v = weakView.lock()) {
                                HandleRadioButtonClick(v, entity, optionValue, radioGroup, onClickMethod);
                              }
                            })
          .get<sol::protected_function>("fn");

  ApplyButtonSounds(node, resolved, btn);
  ApplyButtonFocusAttr(view, node, btn, scope, lua);
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
    view->RefreshKey(LastDotPathSegment(st->bindPath));
  }
  view->InvokeController(st->onChangeMethod, valObj);
  if (!view->IsCurrentlyMounted()) return;
  const auto listeners = st->changeListeners;
  for (const auto& cb : listeners) {
    if (!view->IsCurrentlyMounted()) break;
    LogIfLuaError(cb(st->value), "UI slider on-change");
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
  const auto* viewport = r->TryGet<ViewportInfo>();
  const auto* config = r->TryGet<GameConfig>();
  if (viewport != nullptr && config != nullptr) {
    const glm::vec2 transformed = viewport->TransformCoordinates(mx, my, config->windowWidth, config->windowHeight);
    mx = transformed.x;
    my = transformed.y;
  }
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

  view->sliders[entity.id] = st;

  const bool isDisabled = ParseBoolStr(node.GetAttr("disabled", resolved.Get("disabled", "false")), false);
  UIButtonComponent btn;
  btn.isActive = !isDisabled;
  std::weak_ptr<UIViewInstance> weakView = view;
  btn.clickFunction = lua.create_table_with("fn",
                                            [weakView, st]() {
                                              if (auto v = weakView.lock()) {
                                                HandleSliderClick(v, st);
                                              }
                                            })
                          .get<sol::protected_function>("fn");
  btn.stepFunction = lua.create_table_with("fn",
                                           [weakView, st](const float dir) {
                                             if (auto v = weakView.lock()) {
                                               const float stepAmt =
                                                   st->step > 0.0f ? st->step : (st->maxVal - st->minVal) * 0.05f;
                                               SetSliderValue(v, st, st->value + dir * stepAmt, true);
                                             }
                                           })
                         .get<sol::protected_function>("fn");

  ApplyButtonInputAttrs(node, btn);
  ApplyButtonSounds(node, resolved, btn);
  ApplyButtonFocusAttr(view, node, btn, scope, lua);
  reg->AddComponent(entity, btn);
  ConfigureInteractiveFocus(reg, entity, node, resolved);
  SetEntityDisabled(reg, entity, isDisabled);

  if (!st->bindPath.empty()) {
    std::unordered_set<std::string> deps;
    ExtractExprIdentifiers(st->bindPath, deps);
    view->AddRefreshFn(
        [weakView, st]() {
          auto v = LockActiveView(weakView);
          if (!v || !v->context.valid()) return;
          const float boundVal = ObjectToFloat(GetPathValue(v->context, st->bindPath), st->value);
          SetSliderValue(v, st, boundVal, false);
        },
        std::move(deps));
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

bool IsEntityVisible(Registry* r, Entity entity) {
  if (!r->IsAlive(entity) || !r->HasComponent<UIRectComponent>(entity)) return false;
  return r->GetComponent<UIRectComponent>(entity).visible;
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
  std::unordered_set<std::string> deps;
  ExtractExprIdentifiers(expr, deps);
  std::weak_ptr<UIViewInstance> weakView = view;
  auto applyVis = [weakView, entity, expr, scope]() {
    auto v = LockActiveView(weakView);
    if (!v) return;
    const bool vis = ObjectToBool(EvalBindingExpr(*v->runtime->GetLua(), expr, scope));
    SetEntityVisibility(v->runtime->GetRegistry(), entity, vis);
  };
  applyVis();
  view->AddRefreshFn(applyVis, std::move(deps));
}

void BindDisabledAttribute(const std::shared_ptr<UIViewInstance>& view, Entity entity, const std::string& rawAttr,
                           const sol::table& scope) {
  const std::string expr = UnwrapBindingOrRaw(rawAttr);
  std::unordered_set<std::string> deps;
  ExtractExprIdentifiers(expr, deps);
  std::weak_ptr<UIViewInstance> weakView = view;
  auto applyDis = [weakView, entity, expr, scope]() {
    auto v = LockActiveView(weakView);
    if (!v) return;
    const bool dis = ObjectToBool(EvalBindingExpr(*v->runtime->GetLua(), expr, scope));
    SetEntityDisabled(v->runtime->GetRegistry(), entity, dis);
  };
  applyDis();
  view->AddRefreshFn(applyDis, std::move(deps));
}

void BindSpriteAttribute(const std::shared_ptr<UIViewInstance>& view, Entity entity, const std::string& rawSprite,
                         const sol::table& scope) {
  std::unordered_set<std::string> deps;
  ExtractInterpolatedIdentifiers(rawSprite, deps);
  std::weak_ptr<UIViewInstance> weakView = view;
  auto applySprite = [weakView, entity, rawSprite, scope]() {
    auto v = LockActiveView(weakView);
    if (!v) return;
    Registry* r = v->runtime->GetRegistry();
    if (!r->IsAlive(entity)) return;
    const std::string resolvedId = EvalInterpolatedString(*v->runtime->GetLua(), rawSprite, scope);
    if (r->HasComponent<SpriteComponent>(entity)) {
      auto& sp = r->GetComponent<SpriteComponent>(entity);
      if (sp.assetId != resolvedId) {
        sp.assetId = resolvedId;
        if (auto* spriteCache = r->TryGet<SpriteRenderCache>()) {
          spriteCache->Forget(entity);
        }
      }
    } else if (!resolvedId.empty()) {
      r->AddComponent(entity, SpriteComponent(resolvedId, 0.0f, 0.0f, 0, true));
    }
  };
  applySprite();
  view->AddRefreshFn(applySprite, std::move(deps));
}

void ApplyResolvedBoxAndFlexUpdate(Registry* r, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved) {
  if (r->HasComponent<UIRectComponent>(entity)) {
    r->GetComponent<UIRectComponent>(entity).blocksPointer =
        ParsePointerEventsBlocks(node.GetAttr("pointer-events", resolved.Get("pointer-events")));
  }
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
    const bool isGrid = (node.tag == "Grid");
    const FlexDirection defaultDir = (node.tag == "Row" || isGrid) ? FlexDirection::Row : FlexDirection::Column;
    r->GetComponent<UIFlexComponent>(entity) = BuildFlexFromStyle(resolved.base, defaultDir, isGrid);
  }
}

void ApplyProgressBarSquareUpdate(SquarePrimitiveComponent& sq, const OssResolvedStyle& resolved) {
  const std::string rawColor = resolved.Has("fill-color") ? resolved.Get("fill-color") : resolved.Get("color");
  if (!OssParser::ParseColor(rawColor, sq.color)) {
    ExtractBgColor(resolved.base, sq.color);
  }
  OssParser::ParseColor(resolved.Get("track-color"), sq.trackColor);
  OssParser::ParseColor(resolved.Get("border-color"), sq.borderColor);
  OssParser::ParseFloat(resolved.Get("border-width", "1"), sq.borderThickness);
}

void ApplyResolvedSquareUpdate(Registry* r, Entity entity, const OxmlNode& node, const OssResolvedStyle& resolved) {
  if (node.tag == "ProgressBar" && r->HasComponent<SquarePrimitiveComponent>(entity)) {
    ApplyProgressBarSquareUpdate(r->GetComponent<SquarePrimitiveComponent>(entity), resolved);
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
    ConfigureLabelEffects(resolved, lbl);
  }
  if (resolved.HasPseudoStates()) {
    if (r->HasComponent<UIStyleStateComponent>(entity)) {
      r->GetComponent<UIStyleStateComponent>(entity) = resolved.BuildStyleStateComponent();
    } else {
      r->AddComponent(entity, resolved.BuildStyleStateComponent());
    }
  }
}

std::unordered_set<std::string> CollectDynamicStyleDeps(const OxmlNode& node) {
  std::unordered_set<std::string> deps;
  ExtractInterpolatedIdentifiers(node.GetAttr("class"), deps);
  ExtractInterpolatedIdentifiers(node.GetAttr("style"), deps);
  for (const char* key : kStyleAttrs) {
    const auto it = node.attributes.find(key);
    if (it != node.attributes.end() && OxmlParser::ContainsBinding(it->second)) {
      ExtractInterpolatedIdentifiers(it->second, deps);
    }
  }
  return deps;
}

void BindDynamicStyleAttributes(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                                const sol::table& scope) {
  if (!HasDynamicStyleOrClassBinding(node)) return;
  std::unordered_set<std::string> deps = CollectDynamicStyleDeps(node);
  std::weak_ptr<UIViewInstance> weakView = view;
  view->AddRefreshFn(
      [weakView, entity, node, scope]() {
        auto v = LockActiveView(weakView);
        if (!v) return;
        Registry* r = v->runtime->GetRegistry();
        if (!r->IsAlive(entity)) return;
        sol::state& l = *v->runtime->GetLua();
        const auto classes = ResolveNodeClasses(node, scope, l);
        const auto inlineProps = CollectInlineStyleProps(node, scope, l);
        const OssResolvedStyle resolved = v->stylesheet.Resolve(node.tag, classes, node.GetAttr("id"), inlineProps);
        ApplyResolvedBoxAndFlexUpdate(r, entity, node, resolved);
        ApplyResolvedVisualUpdate(r, entity, node, resolved);
      },
      std::move(deps));
}

void BindTextAttribute(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                       const sol::table& scope) {
  if (!node.HasAttr("text") || !OxmlParser::ContainsBinding(node.GetAttr("text"))) {
    return;
  }
  const std::string rawText = node.GetAttr("text");
  std::unordered_set<std::string> deps;
  ExtractInterpolatedIdentifiers(rawText, deps);
  std::weak_ptr<UIViewInstance> weakView = view;
  view->AddRefreshFn(
      [weakView, entity, rawText, scope]() {
        auto v = LockActiveView(weakView);
        if (!v) return;
        Registry* r = v->runtime->GetRegistry();
        if (!r->IsAlive(entity) || !r->HasComponent<TextLabelComponent>(entity)) return;
        r->GetComponent<TextLabelComponent>(entity).text =
            EvalInterpolatedString(*v->runtime->GetLua(), rawText, scope);
      },
      std::move(deps));
}

void BindProgressValueAttribute(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                                const sol::table& scope) {
  if (node.tag != "ProgressBar" || !node.HasAttr("value") || !OxmlParser::ContainsBinding(node.GetAttr("value"))) {
    return;
  }
  const std::string valExpr = UnwrapBindingOrRaw(node.GetAttr("value"));
  std::unordered_set<std::string> deps;
  ExtractExprIdentifiers(valExpr, deps);
  std::weak_ptr<UIViewInstance> weakView = view;
  view->AddRefreshFn(
      [weakView, entity, valExpr, scope]() {
        auto v = LockActiveView(weakView);
        if (!v) return;
        Registry* r = v->runtime->GetRegistry();
        if (!r->IsAlive(entity) || !r->HasComponent<SquarePrimitiveComponent>(entity)) return;
        const float p =
            std::clamp(ObjectToFloat(EvalBindingExpr(*v->runtime->GetLua(), valExpr, scope), 0.0f), 0.0f, 1.0f);
        r->GetComponent<SquarePrimitiveComponent>(entity).fillRatio = p;
      },
      std::move(deps));
}

void RegisterAttributeBindings(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                               const sol::table& scope) {
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
  BindTextAttribute(view, entity, node, scope);
  BindProgressValueAttribute(view, entity, node, scope);
  if (node.HasAttr("disabled") && OxmlParser::ContainsBinding(node.GetAttr("disabled"))) {
    BindDisabledAttribute(view, entity, node.GetAttr("disabled"), scope);
  }
  const std::string spriteBindingAttr =
      (node.HasAttr("sprite") && OxmlParser::ContainsBinding(node.GetAttr("sprite")))
          ? node.GetAttr("sprite")
          : ((node.tag == "Image" && node.HasAttr("src") && OxmlParser::ContainsBinding(node.GetAttr("src")))
                 ? node.GetAttr("src")
                 : std::string());
  if (!spriteBindingAttr.empty()) {
    BindSpriteAttribute(view, entity, spriteBindingAttr, scope);
  }
  BindDynamicStyleAttributes(view, entity, node, scope);
}

void AttachRefTextMethods(sol::table& h, const std::weak_ptr<UIViewInstance>& weakView, Entity entity) {
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

  h.set_function("set_outline", [weakView, entity](const sol::table&, const sol::object& colorOrWidth,
                                                   const sol::optional<float>& maybeWidth) {
    auto v = weakView.lock();
    if (!v) return;
    Registry* r = v->runtime->GetRegistry();
    if (!r->IsAlive(entity) || !r->HasComponent<TextLabelComponent>(entity)) return;
    LuaBinding<TextLabelComponent>::ApplyOutline(r->GetComponent<TextLabelComponent>(entity), colorOrWidth, maybeWidth);
  });
}

void AttachRefValueMethods(sol::table& h, const std::weak_ptr<UIViewInstance>& weakView, Entity entity) {
  h.set_function("set_value", [weakView, entity](const sol::table&, const float val) {
    auto v = weakView.lock();
    if (!v) return;
    if (const auto sIt = v->sliders.find(entity.id); sIt != v->sliders.end()) {
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
    if (const auto sIt = v->sliders.find(entity.id); sIt != v->sliders.end()) {
      return sIt->second->value;
    }
    Registry* r = v->runtime->GetRegistry();
    if (r->IsAlive(entity) && r->HasComponent<SquarePrimitiveComponent>(entity)) {
      return r->GetComponent<SquarePrimitiveComponent>(entity).fillRatio;
    }
    return 0.0f;
  });
}

void AttachRefStateSetters(sol::table& h, const std::weak_ptr<UIViewInstance>& weakView, Entity entity) {
  h.set_function("set_checked", [weakView, entity](const sol::table&, const bool checked) {
    if (auto v = weakView.lock()) SetEntityChecked(v->runtime->GetRegistry(), entity, checked);
  });
  h.set_function("set_visible", [weakView, entity](const sol::table&, const bool visible) {
    if (auto v = weakView.lock()) SetEntityVisibility(v->runtime->GetRegistry(), entity, visible);
  });
  h.set_function("set_disabled", [weakView, entity](const sol::table&, const bool disabled) {
    if (auto v = weakView.lock()) SetEntityDisabled(v->runtime->GetRegistry(), entity, disabled);
  });
  h.set_function("set_inert", [weakView, entity](const sol::table&, const bool inert) {
    if (auto v = weakView.lock()) v->SetRefSubtreeInert(entity, inert);
  });
}

void AttachRefStateGetters(sol::table& h, const std::weak_ptr<UIViewInstance>& weakView, Entity entity) {
  h.set_function("is_checked", [weakView, entity](const sol::table&) -> bool {
    auto v = weakView.lock();
    return v ? IsEntityChecked(v->runtime->GetRegistry(), entity) : false;
  });
  h.set_function("is_visible", [weakView, entity](const sol::table&) -> bool {
    auto v = weakView.lock();
    return v ? IsEntityVisible(v->runtime->GetRegistry(), entity) : false;
  });
  h.set_function("is_disabled", [weakView, entity](const sol::table&) -> bool {
    auto v = weakView.lock();
    return v ? IsEntityDisabled(v->runtime->GetRegistry(), entity) : false;
  });
  h.set_function("is_inert", [weakView, entity](const sol::table&) -> bool {
    auto v = weakView.lock();
    return v ? IsEntityInert(v->runtime->GetRegistry(), entity) : false;
  });
}

void AttachRefStateMethods(sol::table& h, const std::weak_ptr<UIViewInstance>& weakView, Entity entity) {
  AttachRefStateSetters(h, weakView, entity);
  AttachRefStateGetters(h, weakView, entity);
}

void RegisterRefEventHandler(const std::shared_ptr<UIViewInstance>& v, Entity entity, const std::string& eventName,
                             const sol::protected_function& fn) {
  Registry* r = v->runtime->GetRegistry();
  if (!r->IsAlive(entity)) return;
  if (eventName == "click" && r->HasComponent<UIButtonComponent>(entity)) {
    auto& btn = r->GetComponent<UIButtonComponent>(entity);
    const sol::protected_function prev = btn.clickFunction;
    std::weak_ptr<UIViewInstance> weakView = v;
    btn.clickFunction =
        v->runtime->GetLua()
            ->create_table_with("fn",
                                [weakView, prev, fn](sol::variadic_args va) {
                                  if (prev.valid()) {
                                    LogIfLuaError(prev(sol::as_args(va)), "UI ref previous click handler");
                                  }
                                  if (!LockMountedView(weakView)) return;
                                  LogIfLuaError(fn(sol::as_args(va)), "UI ref click handler");
                                })
            .get<sol::protected_function>("fn");
  } else if (eventName == "change") {
    if (const auto sIt = v->sliders.find(entity.id); sIt != v->sliders.end()) {
      sIt->second->changeListeners.push_back(fn);
    } else {
      v->changeCallbacks[entity.id].push_back(fn);
    }
  }
}

sol::table CreateRefHandle(const std::shared_ptr<UIViewInstance>& view, Entity entity) {
  sol::state& lua = *view->runtime->GetLua();
  sol::table h = lua.create_table();
  h.set("entity", entity);
  std::weak_ptr<UIViewInstance> weakView = view;

  AttachRefTextMethods(h, weakView, entity);
  AttachRefValueMethods(h, weakView, entity);
  AttachRefStateMethods(h, weakView, entity);

  h.set_function(
      "on", [weakView, entity](const sol::table&, const std::string& eventName, const sol::protected_function& fn) {
        auto v = weakView.lock();
        if (!v || !fn.valid()) return;
        RegisterRefEventHandler(v, entity, eventName, fn);
      });

  return h;
}

struct SlotProjection {
  const std::vector<OxmlNode>* nodes{nullptr};
  sol::table callerScope{};
};

// Forward declaration for recursive node instantiation.
Entity InstantiateNode(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node, std::optional<Entity> parent,
                       const sol::table& scope, const SlotProjection* slotProjection,
                       const std::shared_ptr<RadioGroupState>& activeRadioGroup);

void EraseSubtreeChildOrderSlots(Registry* r, UIViewInstance& v, const Entity root) {
  std::vector<Entity> stack{root};
  while (!stack.empty()) {
    const Entity cur = stack.back();
    stack.pop_back();
    v.parentChildSlots.erase(cur.id);
    if (r->IsAlive(cur)) {
      r->ForEachChild(cur, [&stack](const Entity child) { stack.push_back(child); });
    }
  }
}

void ClearRepeaterSpawnedItems(const std::shared_ptr<UIViewInstance>& v, const std::shared_ptr<RepeaterRecord>& rec) {
  Registry* r = v->runtime->GetRegistry();
  for (const Entity e : rec->spawnedEntities) {
    EraseSubtreeChildOrderSlots(r, *v, e);
    if (r->IsAlive(e)) {
      r->BlamEntity(e);
    }
  }
  rec->spawnedEntities.clear();
  rec->itemRefreshEntries.clear();
  std::erase_if(v->sliders, [r](const auto& kv) { return !r->IsAlive(kv.second->entity); });
  std::erase_if(v->changeCallbacks, [r](const auto& kv) { return !r->IsAlive(Entity(kv.first)); });
}

void SyncParentChildOrder(Registry* r, const UIViewInstance& v, const Entity parentEntity) {
  const auto it = v.parentChildSlots.find(parentEntity.id);
  if (it == v.parentChildSlots.end()) return;
  std::vector<Entity> desiredOrder;
  for (const auto& slot : it->second) {
    if (!slot.isRepeater) {
      if (r->IsAlive(slot.staticChild)) desiredOrder.push_back(slot.staticChild);
      continue;
    }
    const auto locked = slot.repeater.lock();
    if (!locked) continue;
    for (const Entity spawned : locked->spawnedEntities) {
      if (r->IsAlive(spawned)) desiredOrder.push_back(spawned);
    }
  }
  r->ReorderChildren(parentEntity, desiredOrder);
}

std::optional<std::size_t> FindFocusedRepeaterIndex(Registry* r, const std::vector<Entity>& spawned) {
  for (std::size_t i = 0; i < spawned.size(); ++i) {
    if (SubtreeHasFocus(r, spawned[i])) return i;
  }
  return std::nullopt;
}

void FinalizeRepeaterItems(Registry* r, const std::shared_ptr<UIViewInstance>& v,
                           const std::shared_ptr<RepeaterRecord>& rec,
                           const std::optional<std::size_t> prevFocusedIdx) {
  RunRefreshEntries(*v, rec->itemRefreshEntries, nullptr);
  if (!v->IsActiveOrBuilding()) return;
  if (v->isInert || IsEntityInert(r, rec->parentEntity)) {
    for (const Entity e : rec->spawnedEntities) {
      SetSubtreeInert(r, e, true);
    }
  }
  if (!prevFocusedIdx.has_value() || rec->spawnedEntities.empty()) return;
  const std::size_t idx = std::min(*prevFocusedIdx, rec->spawnedEntities.size() - 1);
  if (const auto focusTarget = FindFirstFocusableInSubtree(r, rec->spawnedEntities[idx])) {
    // Suppress on-focus while restoring repeater selection so reactive rebuilds cannot re-trigger loops.
    FocusEntitySafe(r, *focusTarget, false);
  }
}

// Repeater rebuild instantiates child OXML trees which may contain nested repeaters.
// NOLINTNEXTLINE(misc-no-recursion)
void RebuildRepeaterItems(const std::shared_ptr<UIViewInstance>& v, const std::shared_ptr<RepeaterRecord>& rec,
                          const sol::table& scope, const SlotProjection* projPtr,
                          const std::shared_ptr<RadioGroupState>& activeRadioGroup) {
  Registry* r = v->runtime->GetRegistry();
  if (!r->IsAlive(rec->parentEntity)) return;

  const std::optional<std::size_t> prevFocusedIdx = FindFocusedRepeaterIndex(r, rec->spawnedEntities);
  ClearRepeaterSpawnedItems(v, rec);

  sol::state& lua = *v->runtime->GetLua();
  const sol::object listObj = EvalBindingExpr(lua, rec->clause.listExpr, scope);
  if (!listObj.is<sol::table>()) return;

  auto* prevSink = v->activeRefreshSink;
  v->activeRefreshSink = &rec->itemRefreshEntries;
  const sol::table listTbl = listObj.as<sol::table>();
  const std::size_t len = listTbl.size();
  for (std::size_t idx = 1; idx <= len; ++idx) {
    sol::object itemVal = listTbl.get<sol::object>(idx);
    if (!itemVal.valid() || itemVal == sol::lua_nil) continue;
    sol::table itemVars = lua.create_table();
    itemVars.set(rec->clause.itemVar, itemVal);
    itemVars.set("__item", itemVal);
    if (!rec->clause.indexVar.empty()) {
      itemVars.set(rec->clause.indexVar, static_cast<int>(idx));
    }
    sol::table childScope = v->MakeScope(itemVars);
    const Entity spawned =
        InstantiateNode(v, rec->itemTemplate, rec->parentEntity, childScope, projPtr, activeRadioGroup);
    rec->spawnedEntities.push_back(spawned);
  }
  v->activeRefreshSink = prevSink;
  FinalizeRepeaterItems(r, v, rec, prevFocusedIdx);
  SyncParentChildOrder(r, *v, rec->parentEntity);
}

// Repeater mounting delegates to RebuildRepeaterItems which recursively instantiates child nodes.
// NOLINTNEXTLINE(misc-no-recursion)
void MountRepeaterNode(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node, Entity parentEntity,
                       const sol::table& scope, const SlotProjection* slotProjection,
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
  ExtractExprIdentifiers(clause.listExpr, rec->listDeps);
  view->parentChildSlots[parentEntity.id].push_back(ChildOrderSlot{.isRepeater = true, .repeater = rec});

  std::weak_ptr<UIViewInstance> weakView = view;
  const std::vector<OxmlNode> savedSlots = (slotProjection != nullptr && slotProjection->nodes != nullptr)
                                               ? *slotProjection->nodes
                                               : std::vector<OxmlNode>{};
  const sol::table savedCallerScope = slotProjection != nullptr ? slotProjection->callerScope : sol::table{};

  // Repeater refresh callback invokes RebuildRepeaterItems which traverses child OXML subtrees.
  // NOLINTNEXTLINE(misc-no-recursion)
  auto rebuild = [weakView, rec, scope, savedSlots, savedCallerScope, activeRadioGroup]() {
    auto v = LockActiveView(weakView);
    if (!v) return;
    const SlotProjection savedProj{&savedSlots, savedCallerScope};
    const SlotProjection* projPtr = savedSlots.empty() && !savedCallerScope.valid() ? nullptr : &savedProj;
    RebuildRepeaterItems(v, rec, scope, projPtr, activeRadioGroup);
  };

  rebuild();
  view->AddRefreshEntry(RefreshEntry{
      .deps = {},
      .producesKey = {},
      // NOLINTNEXTLINE(misc-no-recursion)
      .fn =
          [weakView, rec, rebuild](std::unordered_set<std::string>* dirtyKeys) {
            auto v = LockActiveView(weakView);
            if (!v) return;
            if (dirtyKeys == nullptr || rec->listDeps.empty() || MatchesAnyDep(rec->listDeps, *dirtyKeys)) {
              rebuild();
            } else {
              RunRefreshEntries(*v, rec->itemRefreshEntries, dirtyKeys);
            }
          },
  });
}

bool IsReservedCallSiteAttr(const std::string& key) {
  return key == "if" || key == "visible" || key == "ref" || key == "slot";
}

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

// Imported <Use> components instantiate a child OXML subtree via InstantiateNode.
// NOLINTNEXTLINE(misc-no-recursion)
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
      std::unordered_set<std::string> deps;
      ExtractInterpolatedIdentifiers(valStr, deps);
      view->AddRefreshFn(
          [weakView, paramVars, key, valStr, scope]() mutable {
            auto v = LockActiveView(weakView);
            if (!v) return;
            paramVars[key] = EvalBoundAttrValue(*v->runtime->GetLua(), valStr, scope);
          },
          std::move(deps), key);
    } else {
      paramVars[k] = rawVal;
    }
  }
  sol::table templateScope = view->MakeScope(paramVars);
  Entity rootEnt{};
  if (importedDoc.roots.empty()) {
    rootEnt = view->runtime->GetRegistry()->CreateEntity();
  } else {
    const SlotProjection proj{&node.children, scope};
    rootEnt = InstantiateNode(view, importedDoc.roots.front(), parent, templateScope, &proj, activeRadioGroup);
  }
  ApplyCompositeNodeBindings(view, rootEnt, node, scope);
  return rootEnt;
}

void BindCustomWidgetAttributes(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node,
                                const sol::table& descriptor, const sol::table& scope, sol::table& props,
                                const sol::table& instanceCtx) {
  sol::state& lua = *view->runtime->GetLua();
  if (const sol::optional<sol::table> defaults = descriptor["props"]; defaults) {
    for (const auto& [k, v] : *defaults) {
      props[k] = v;
    }
  }
  const sol::optional<sol::protected_function> onPropChanged = descriptor["on_prop_changed"];
  std::weak_ptr<UIViewInstance> weakView = view;

  for (const auto& [k, rawVal] : node.attributes) {
    if (IsReservedCallSiteAttr(k)) continue;
    if (!OxmlParser::ContainsBinding(rawVal)) {
      props[k] = rawVal;
      continue;
    }
    props[k] = EvalBoundAttrValue(lua, rawVal, scope);
    const std::string key = k;
    const std::string valStr = rawVal;
    std::unordered_set<std::string> deps;
    ExtractInterpolatedIdentifiers(valStr, deps);
    view->AddRefreshFn(
        [weakView, instanceCtx, props, key, valStr, scope, onPropChanged]() mutable {
          auto v = LockActiveView(weakView);
          if (!v) return;
          sol::object nextVal = EvalBoundAttrValue(*v->runtime->GetLua(), valStr, scope);
          props[key] = nextVal;
          if (onPropChanged.has_value()) {
            LogIfLuaError((*onPropChanged)(instanceCtx, key, nextVal), "UI custom widget on_prop_changed");
          }
        },
        std::move(deps), key);
  }
}

// Custom Lua widgets instantiate their template or fallback panel through InstantiateNode.
// NOLINTNEXTLINE(misc-no-recursion)
Entity InstantiateCustomLuaWidget(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node,
                                  const sol::table& descriptor, std::optional<Entity> parent, const sol::table& scope,
                                  const std::shared_ptr<RadioGroupState>& activeRadioGroup) {
  sol::state& lua = *view->runtime->GetLua();
  sol::table props = lua.create_table();
  sol::table instanceCtx = lua.create_table();
  BindCustomWidgetAttributes(view, node, descriptor, scope, props, instanceCtx);

  Entity rootEnt{};
  bool createdFromTemplate = false;
  if (const sol::optional<std::string> tplStr = descriptor["template"]; tplStr && !tplStr->empty()) {
    const OxmlParseResult parsed = OxmlParser::Parse(*tplStr);
    if (parsed.ok && !parsed.document.roots.empty()) {
      sol::table tplScope = view->MakeScope(props);
      const SlotProjection proj{&node.children, scope};
      rootEnt = InstantiateNode(view, parsed.document.roots.front(), parent, tplScope, &proj, activeRadioGroup);
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
    LogIfLuaError((*onMount)(instanceCtx), "UI custom widget on_mount");
  }
  if (const sol::optional<sol::protected_function> onUnmount = descriptor["on_unmount"]; onUnmount) {
    const sol::protected_function& cb = *onUnmount;
    view->unmountFns.emplace_back(
        [cb, instanceCtx]() { LogIfLuaError(cb(instanceCtx), "UI custom widget on_unmount"); });
  }
  return rootEnt;
}

bool IsContainerTag(const std::string& tag) {
  return tag == "Canvas" || tag == "Panel" || tag == "Box" || tag == "Row" || tag == "Column" || tag == "Grid" ||
         tag == "ScrollView" || tag == "RadioGroup";
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
    std::unordered_set<std::string> deps;
    ExtractExprIdentifiers(rg->bindPath, deps);
    std::weak_ptr<UIViewInstance> weakView = view;
    view->AddRefreshFn(
        [weakView, rg]() {
          auto v = LockActiveView(weakView);
          if (!v || !v->context.valid()) return;
          Registry* r = v->runtime->GetRegistry();
          rg->currentValue = ObjectToString(GetPathValue(v->context, rg->bindPath));
          if (!v->IsActiveOrBuilding()) return;
          for (const auto& [btnEnt, btnVal] : rg->buttons) {
            SetEntityChecked(r, btnEnt, btnVal == rg->currentValue);
          }
        },
        std::move(deps));
  }
  return rg;
}

void ConfigureContainerEntity(const std::shared_ptr<UIViewInstance>& view, Entity entity, const OxmlNode& node,
                              const OssResolvedStyle& resolved, const sol::table& scope,
                              std::shared_ptr<RadioGroupState>& currentRadioGroup) {
  Registry* reg = view->runtime->GetRegistry();
  sol::state& lua = *view->runtime->GetLua();
  const bool isGrid = (node.tag == "Grid");
  const FlexDirection defaultDir = (node.tag == "Row" || isGrid) ? FlexDirection::Row : FlexDirection::Column;
  reg->AddComponent(entity, BuildFlexFromStyle(resolved.base, defaultDir, isGrid));
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
  if (node.HasAttr("on-click") || node.HasAttr("on-focus")) {
    const bool isDisabled = ParseBoolStr(node.GetAttr("disabled", resolved.Get("disabled", "false")), false);
    UIButtonComponent btn;
    btn.isActive = !isDisabled;
    ApplyButtonInputAttrs(node, btn);
    if (node.HasAttr("on-click")) {
      btn.clickFunction = MakeControllerItemCallback(view, node.GetAttr("on-click"), scope, lua);
    }
    ApplyButtonFocusAttr(view, node, btn, scope, lua);
    ApplyButtonSounds(node, resolved, btn);
    reg->AddComponent(entity, btn);
    ConfigureInteractiveFocus(reg, entity, node, resolved);
    SetEntityDisabled(reg, entity, isDisabled);
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

// Dispatches child node instantiation to repeater or single-node tree traversal.
// NOLINTNEXTLINE(misc-no-recursion)
void InstantiateSingleOrRepeater(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& child,
                                 Entity parentEntity, const sol::table& scope, const SlotProjection* slotProjection,
                                 const std::shared_ptr<RadioGroupState>& currentRadioGroup) {
  if (child.HasAttr("each")) {
    MountRepeaterNode(view, child, parentEntity, scope, slotProjection, currentRadioGroup);
    return;
  }
  const Entity spawned = InstantiateNode(view, child, parentEntity, scope, slotProjection, currentRadioGroup);
  if (view->runtime->GetRegistry()->IsAlive(spawned)) {
    view->parentChildSlots[parentEntity.id].push_back(ChildOrderSlot{.staticChild = spawned});
  }
}

// Projects caller slot children or default fallback children into the parent entity hierarchy.
// NOLINTNEXTLINE(misc-no-recursion)
void InstantiateSlotProjection(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& slotNode,
                               Entity parentEntity, const sol::table& templateScope,
                               const SlotProjection* slotProjection,
                               const std::shared_ptr<RadioGroupState>& currentRadioGroup) {
  const std::string slotName = slotNode.GetAttr("name");
  bool matchedAny = false;
  if (slotProjection != nullptr && slotProjection->nodes != nullptr) {
    const sol::table& effScope = slotProjection->callerScope.valid() ? slotProjection->callerScope : templateScope;
    for (const auto& candidate : *slotProjection->nodes) {
      if (candidate.GetAttr("slot") == slotName) {
        InstantiateSingleOrRepeater(view, candidate, parentEntity, effScope, nullptr, currentRadioGroup);
        matchedAny = true;
      }
    }
  }
  if (!matchedAny) {
    for (const auto& fallbackChild : slotNode.children) {
      InstantiateSingleOrRepeater(view, fallbackChild, parentEntity, templateScope, slotProjection, currentRadioGroup);
    }
  }
}

// Recursively instantiates an OXML element and its child hierarchy into ECS entities.
// NOLINTNEXTLINE(misc-no-recursion)
Entity InstantiateNode(const std::shared_ptr<UIViewInstance>& view, const OxmlNode& node, std::optional<Entity> parent,
                       const sol::table& scope, const SlotProjection* slotProjection,
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
      InstantiateSlotProjection(view, child, entity, scope, slotProjection, currentRadioGroup);
      continue;
    }
    InstantiateSingleOrRepeater(view, child, entity, scope, slotProjection, currentRadioGroup);
  }

  return entity;
}

bool IsTableObservable(const sol::table& tbl) { return tbl.valid() && tbl.get_or("__is_observable", false); }

void SubscribeObservableTable(sol::table& obsTbl, const std::function<void(const std::string&)>& fn) {
  sol::protected_function subFn = obsTbl.get<sol::protected_function>("__subscribe");
  if (subFn.valid()) {
    LogIfLuaError(subFn(obsTbl, fn), "UI observable __subscribe");
  }
}

void AppendUniqueStr(std::vector<std::string>& out, const std::string& val) {
  if (!val.empty() && std::find(out.begin(), out.end(), val) == out.end()) {
    out.push_back(val);
  }
}

struct LoadedViewResources {
  bool ok = true;
  std::string errorFile{};
  int errorLine = 0;
  std::string error{};
  OssStylesheet stylesheet{};
  std::unordered_map<std::string, OxmlDocument> uses{};
  std::vector<std::string> watchedPaths{};

  void RecordError(std::string file, int line, std::string msg) {
    if (!ok) return;
    ok = false;
    errorFile = std::move(file);
    errorLine = line;
    error = std::move(msg);
  }
};

void LoadDeclaredStylesForView(UIRuntime* runtime, const OxmlDocument& doc, LoadedViewResources& out) {
  for (const auto& styleImp : doc.styles) {
    std::string styleDiskPath;
    const auto ossText = runtime->ReadTextAsset(styleImp.src, &styleDiskPath);
    AppendUniqueStr(out.watchedPaths, styleDiskPath);
    if (!ossText.has_value()) {
      const std::string msg = "missing stylesheet '" + styleImp.src + "'";
      Logger::Error("ui.mount: " + msg);
      out.RecordError(styleImp.src, styleImp.line, msg);
      continue;
    }
    const OssParseResult ossRes = OssParser::Parse(*ossText);
    if (ossRes.ok) {
      out.stylesheet.MergeFrom(ossRes.stylesheet);
    } else {
      const std::string msg = "OSS parse error in '" + styleImp.src + "': " + ossRes.error;
      Logger::Error("ui.mount: " + msg);
      out.RecordError(styleImp.src, ossRes.errorLine, ossRes.error);
    }
  }
}

void LoadExtraOptionStylesFromSources(UIRuntime* runtime, const std::vector<std::string>& extraStyles,
                                      LoadedViewResources& out) {
  for (const std::string& s : extraStyles) {
    std::string styleDiskPath;
    const bool isInline = (s.find('{') != std::string::npos);
    const auto text = isInline ? std::optional<std::string>(s) : runtime->ReadTextAsset(s, &styleDiskPath);
    AppendUniqueStr(out.watchedPaths, styleDiskPath);
    if (!text.has_value()) continue;
    const OssParseResult ossRes = OssParser::Parse(*text);
    if (ossRes.ok) {
      out.stylesheet.MergeFrom(ossRes.stylesheet);
    } else {
      out.RecordError(isInline ? "<inline-oss>" : s, ossRes.errorLine, ossRes.error);
    }
  }
}

std::vector<std::string> ExtractExtraStyleSources(const sol::optional<sol::table>& opts) {
  std::vector<std::string> sources;
  if (!opts.has_value()) return sources;
  const sol::optional<sol::table> extraStyles = (*opts)["styles"];
  if (!extraStyles) return sources;
  for (const auto& [_, val] : *extraStyles) {
    if (val.is<std::string>()) {
      sources.push_back(val.as<std::string>());
    }
  }
  return sources;
}

void LoadUsesForView(UIRuntime* runtime, const OxmlDocument& doc, LoadedViewResources& out) {
  for (const auto& useImp : doc.uses) {
    std::string useDiskPath;
    const auto useText = (!useImp.src.empty() && useImp.src.front() == '<')
                             ? std::optional<std::string>(useImp.src)
                             : runtime->ReadTextAsset(useImp.src, &useDiskPath);
    AppendUniqueStr(out.watchedPaths, useDiskPath);
    if (!useText.has_value()) {
      const std::string msg = "missing <Use src='" + useImp.src + "'>";
      Logger::Error("ui.mount: " + msg);
      out.RecordError(useImp.src, useImp.line, msg);
      continue;
    }
    const OxmlParseResult useParsed = OxmlParser::Parse(*useText);
    if (useParsed.ok) {
      out.uses[useImp.name] = useParsed.document;
    } else {
      const std::string msg = "failed to parse <Use src='" + useImp.src + "'>: " + useParsed.error;
      Logger::Error("ui.mount: " + msg);
      out.RecordError(useImp.src, useParsed.errorLine, useParsed.error);
    }
  }
}

LoadedViewResources LoadResourcesForDocument(UIRuntime* runtime, const OxmlDocument& doc,
                                             const std::vector<std::string>& extraStyles,
                                             const std::string& resolvedMainPath) {
  LoadedViewResources res;
  AppendUniqueStr(res.watchedPaths, resolvedMainPath);
  LoadDeclaredStylesForView(runtime, doc, res);
  LoadExtraOptionStylesFromSources(runtime, extraStyles, res);
  LoadUsesForView(runtime, doc, res);
  return res;
}

void CollectStaticRefsFromDoc(const OxmlDocument& doc, std::vector<std::string>& outIds) {
  const OxmlAssetRefs refs = OxmlParser::CollectStaticAssetRefs(doc);
  for (const auto& id : refs.spriteIds) AppendUniqueStr(outIds, id);
  for (const auto& id : refs.fontIds) AppendUniqueStr(outIds, id);
  for (const auto& id : refs.audioIds) AppendUniqueStr(outIds, id);
}

void CollectStaticRefsFromStylesheet(const OssStylesheet& sheet, std::vector<std::string>& outIds) {
  static constexpr const char* kAssetPropKeys[] = {
      "font", "sprite", "icon", "sound-hover", "sound-click", "sound-press", "sound-focus", "sound",
  };
  for (const auto& rule : sheet.rules) {
    for (const char* key : kAssetPropKeys) {
      const auto it = rule.properties.find(key);
      if (it == rule.properties.end()) continue;
      const std::string resolved = StripStyleQuotes(sheet.ResolveVariables(it->second));
      if (!resolved.empty() && !OxmlParser::ContainsBinding(resolved)) {
        AppendUniqueStr(outIds, resolved);
      }
    }
  }
}

bool CanAcquireAssetType(AssetType type, SDL_Renderer* renderer, MIX_Mixer* mixer) {
  switch (type) {
    case AssetType::Texture:
      return renderer != nullptr;
    case AssetType::Font:
      return TTF_WasInit() > 0;
    case AssetType::Audio:
      return mixer != nullptr;
    default:
      return false;
  }
}

std::vector<std::string> AcquireViewCatalogAssets(UIRuntime* runtime, const OxmlDocument& doc,
                                                  const OssStylesheet& sheet,
                                                  const std::unordered_map<std::string, OxmlDocument>& uses) {
  std::vector<std::string> acquired;
  AssetManager* am = runtime != nullptr ? runtime->GetAssetManager() : nullptr;
  if (am == nullptr) return acquired;

  std::vector<std::string> candidates;
  CollectStaticRefsFromDoc(doc, candidates);
  for (const auto& [_, useDoc] : uses) {
    CollectStaticRefsFromDoc(useDoc, candidates);
  }
  CollectStaticRefsFromStylesheet(sheet, candidates);

  Registry* reg = runtime->GetRegistry();
  const auto* ctx = reg != nullptr ? reg->TryGet<EngineContext>() : nullptr;
  SDL_Renderer* renderer = ctx != nullptr ? ctx->sdlRenderer : nullptr;
  MIX_Mixer* mixer = ctx != nullptr ? ctx->mixer : nullptr;

  for (const auto& id : candidates) {
    const CatalogEntry* entry = am->GetCatalog().Find(id);
    if (entry == nullptr || !CanAcquireAssetType(entry->type, renderer, mixer)) continue;
    if (am->Acquire(id, renderer, mixer)) {
      acquired.push_back(id);
    }
  }
  return acquired;
}

void SeedParamsFromDocument(const std::shared_ptr<UIViewInstance>& view, const OxmlDocument& doc, sol::state& lua) {
  view->params = lua.create_table();
  for (const auto& p : doc.params) {
    view->params[p.name] = p.defaultValue;
  }
  if (view->paramOverrides.valid()) {
    for (const auto& [k, v] : view->paramOverrides) view->params[k] = v;
  }
}

void SyncRecordWatchTimes(std::vector<std::string>& destFiles,
                          std::unordered_map<std::string, fs::file_time_type>& destTimes,
                          std::vector<std::string> newFiles) {
  destFiles = std::move(newFiles);
  destTimes.clear();
  std::error_code ec;
  for (const auto& f : destFiles) {
    if (fs::exists(f, ec)) {
      destTimes[f] = fs::last_write_time(f, ec);
    }
  }
}

void MergeNewWatchPaths(std::vector<std::string>& destFiles,
                        std::unordered_map<std::string, fs::file_time_type>& destTimes,
                        const std::vector<std::string>& discoveredFiles) {
  std::error_code ec;
  for (const auto& f : discoveredFiles) {
    AppendUniqueStr(destFiles, f);
    if (destTimes.find(f) == destTimes.end() && fs::exists(f, ec)) {
      destTimes[f] = fs::last_write_time(f, ec);
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
    view->paramOverrides = *prm;
  }
}

void BindViewContextObservable(const std::weak_ptr<UIViewInstance>& weakView) {
  auto v = weakView.lock();
  if (!v || !IsTableObservable(v->context)) return;
  SubscribeObservableTable(v->context, [weakView](const std::string& changedKey) {
    if (auto locked = weakView.lock()) {
      locked->RefreshKey(changedKey);
    }
  });
}

void BindMountViewContextMethods(sol::table& vt, const std::weak_ptr<UIViewInstance>& weakView) {
  vt.set_function("refresh", [weakView](const sol::table&) {
    if (auto v = weakView.lock()) v->Refresh();
  });
  vt.set_function("set_context", [weakView](const sol::table&, const sol::table& newCtx) {
    if (auto v = weakView.lock()) {
      v->context = newCtx;
      BindViewContextObservable(weakView);
      v->Refresh();
    }
  });
  vt.set_function("get_context", [weakView](const sol::table&) -> sol::object {
    const auto v = weakView.lock();
    return v ? sol::object(v->context) : sol::object(sol::lua_nil);
  });
}

void BindMountViewVisibilityAndFocusMethods(sol::table& vt, const std::weak_ptr<UIViewInstance>& weakView) {
  vt.set_function("show", [weakView](const sol::table&) {
    if (auto v = weakView.lock()) SetEntityVisibility(v->runtime->GetRegistry(), v->rootEntity, true);
  });
  vt.set_function("hide", [weakView](const sol::table&) {
    if (auto v = weakView.lock()) SetEntityVisibility(v->runtime->GetRegistry(), v->rootEntity, false);
  });
  vt.set_function("set_inert", [weakView](const sol::table&, const bool inert) {
    if (auto v = weakView.lock()) v->SetInert(inert);
  });
  vt.set_function("is_inert", [weakView](const sol::table&) -> bool {
    const auto v = weakView.lock();
    return v ? v->isInert : false;
  });
  vt.set_function("focus_first", [weakView](const sol::table&) -> sol::optional<Entity> {
    const auto v = weakView.lock();
    if (!v || !v->IsCurrentlyMounted()) return sol::nullopt;
    Registry* r = v->runtime->GetRegistry();
    if (const auto target = FindFirstFocusableInSubtree(r, v->rootEntity)) {
      FocusEntitySafe(r, *target);
      return *target;
    }
    return sol::nullopt;
  });
}

void PopulateMountViewTable(const std::shared_ptr<UIViewInstance>& view) {
  const std::weak_ptr<UIViewInstance> weakView = view;
  BindViewContextObservable(weakView);

  sol::table vt = view->viewTable;
  vt.set("root", view->rootEntity);
  vt.set("refs", view->refsTable);
  BindMountViewContextMethods(vt, weakView);
  BindMountViewVisibilityAndFocusMethods(vt, weakView);
  vt.set_function("is_mounted", [weakView](const sol::table&) -> bool {
    const auto v = weakView.lock();
    return v ? v->IsCurrentlyMounted() : false;
  });
  vt.set_function("unmount", [weakView](const sol::table&) {
    if (auto v = weakView.lock()) v->Unmount();
  });
  vt.set_function("destroy", [weakView](const sol::table&) {
    if (auto v = weakView.lock()) v->Unmount();
  });
}

void RebuildViewTreeForReload(UIRuntime* runtime, const std::shared_ptr<UIViewInstance>& v, const OxmlDocument& doc,
                              LoadedViewResources& resources, const std::optional<Entity>& savedParent) {
  std::vector<std::string> newlyAcquired = AcquireViewCatalogAssets(runtime, doc, resources.stylesheet, resources.uses);
  std::vector<std::string> prevAcquired = std::move(v->acquiredAssetIds);

  Registry* reg = runtime->GetRegistry();
  if (reg != nullptr && reg->IsAlive(v->rootEntity)) {
    reg->BlamEntity(v->rootEntity);
  }
  v->refreshEntries.clear();
  v->unmountFns.clear();
  v->sliders.clear();
  v->changeCallbacks.clear();
  v->parentChildSlots.clear();
  v->inertSubtreeRoots.clear();
  v->stylesheet = std::move(resources.stylesheet);
  v->importedUses = std::move(resources.uses);
#ifndef OCTARINE_SHIPPED
  v->document = doc;
#endif
  v->acquiredAssetIds = std::move(newlyAcquired);
  if (AssetManager* am = runtime->GetAssetManager(); am != nullptr && !prevAcquired.empty()) {
    am->ReleaseAll(prevAcquired);
  }

  SeedParamsFromDocument(v, doc, *runtime->GetLua());
  const sol::table reloadedScope = v->MakeScope();
  {
    const UIViewInstance::BuildingTreeGuard guard(*v);
    v->rootEntity = InstantiateNode(v, doc.roots.front(), savedParent, reloadedScope, nullptr, nullptr);
  }
  v->viewTable.set("root", v->rootEntity);
  v->Refresh();
}

bool ReloadMountedView(UIRuntime* runtime, const std::shared_ptr<UIViewInstance>& v, const std::string& sourceStr,
                       const std::optional<Entity>& savedParent, std::vector<std::string>* watchedFilesOut = nullptr,
                       std::unordered_map<std::string, fs::file_time_type>* lastWriteTimesOut = nullptr) {
  if (v == nullptr || !v->IsCurrentlyMounted()) return false;
  std::string resolvedMainPath;
  const auto loaded = runtime->ReadTextAsset(sourceStr, &resolvedMainPath);
  if (!loaded.has_value()) {
    const std::string err = "failed to read OXML asset '" + sourceStr + "'";
    Logger::Error("UIRuntime: hot-reload " + err);
    runtime->RecordDiagnosticsError(sourceStr, 0, err);
    return false;
  }
  const OxmlParseResult reParsed = OxmlParser::Parse(*loaded);
  if (!reParsed.ok || reParsed.document.roots.empty()) {
    const std::string err = reParsed.error.empty() ? "OXML document contains no root elements" : reParsed.error;
    Logger::Error("UIRuntime: hot-reload OXML parse error: " + err);
    runtime->RecordDiagnosticsError(resolvedMainPath.empty() ? sourceStr : resolvedMainPath, reParsed.errorLine, err);
    return false;
  }

  LoadedViewResources resources =
      LoadResourcesForDocument(runtime, reParsed.document, v->extraStyleSources, resolvedMainPath);
  if (!resources.ok) {
    if (watchedFilesOut != nullptr && lastWriteTimesOut != nullptr) {
      MergeNewWatchPaths(*watchedFilesOut, *lastWriteTimesOut, resources.watchedPaths);
    }
    runtime->RecordDiagnosticsError(resources.errorFile, resources.errorLine, resources.error);
    return false;
  }

  RebuildViewTreeForReload(runtime, v, reParsed.document, resources, savedParent);

  if (watchedFilesOut != nullptr && lastWriteTimesOut != nullptr) {
    SyncRecordWatchTimes(*watchedFilesOut, *lastWriteTimesOut, resources.watchedPaths);
  }
  runtime->RecordDiagnosticsSuccess(std::move(resources.watchedPaths), /*isReload=*/true);
  return true;
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
  using octarine::lua_schema::LuaSchemaRegistry;
  LuaSchemaRegistry::SetFunction(
      uiTable, "ui", "mount",
      [this](const sol::object& source, const sol::optional<sol::table>& opts) { return Mount(source, opts); });

  LuaSchemaRegistry::SetFunction(uiTable, "ui", "observable",
                                 [this](const sol::optional<sol::table>& raw, const sol::this_state s) {
                                   sol::state_view sv(s);
                                   sol::table base = raw.value_or(sv.create_table());
                                   return CreateObservable(base);
                                 });

  LuaSchemaRegistry::SetFunction(
      uiTable, "ui", "register_widget",
      [this](const std::string& name, const sol::table& descriptor) { RegisterWidget(name, descriptor); });

  LuaSchemaRegistry::SetFunction(uiTable, "ui", "debug_draw", [this](const sol::optional<bool>& enabled) -> bool {
    if (registry_ == nullptr) return false;
    auto* cfg = registry_->TryGet<UIDebugConfig>();
    if (cfg == nullptr) {
      cfg = &registry_->Set<UIDebugConfig>(UIDebugConfig{});
    }
    cfg->enabled = enabled.has_value() ? *enabled : !cfg->enabled;
    return cfg->enabled;
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

namespace {
void NotifyObservableListeners(const sol::table& listeners, const std::string& key) {
  static thread_local std::unordered_set<const void*> activeNotifies;
  const void* ptr = listeners.pointer();
  if (ptr == nullptr || !activeNotifies.insert(ptr).second) return;
  struct Guard {
    const void* p;
    explicit Guard(const void* ptr) : p(ptr) {}
    ~Guard() { activeNotifies.erase(p); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Guard(Guard&&) = delete;
    Guard& operator=(Guard&&) = delete;
  } guard(ptr);

  const std::size_t n = listeners.size();
  for (std::size_t i = 1; i <= n; ++i) {
    sol::object fnObj = listeners.get<sol::object>(i);
    if (fnObj.is<sol::protected_function>()) {
      LogIfLuaError(fnObj.as<sol::protected_function>()(key), "UI observable listener");
    }
  }
}

void ForwardChildObservable(sol::table& childObs, const sol::table& listeners) {
  const sol::object childListeners = childObs.raw_get<sol::object>("__listeners");
  if (childListeners.is<sol::table>() && childListeners.as<sol::table>().pointer() == listeners.pointer()) {
    return;
  }
  sol::object sinksObj = childObs.raw_get<sol::object>("__forward_sinks");
  sol::table sinks;
  if (sinksObj.is<sol::table>()) {
    sinks = sinksObj.as<sol::table>();
  } else {
    sol::state_view luaView(childObs.lua_state());
    sinks = luaView.create_table();
    sol::table weakMeta = luaView.create_table_with("__mode", "k");
    sinks[sol::metatable_key] = weakMeta;
    childObs.raw_set("__forward_sinks", sinks);
  }
  const sol::object already = sinks.get<sol::object>(listeners);
  if (already.valid() && already != sol::lua_nil) return;
  sinks.set(listeners, true);
  SubscribeObservableTable(childObs,
                           [listeners](const std::string& subKey) { NotifyObservableListeners(listeners, subKey); });
}

sol::table CreateObservableWithVisited(UIRuntime* runtime, const sol::table& rawTable,
                                       std::unordered_set<const void*>& visited);

// Wraps nested child tables recursively in observables so deep mutations propagate to parent listeners.
// NOLINTNEXTLINE(misc-no-recursion)
void AssignObservableBackingField(UIRuntime* runtime, sol::table& backing, const sol::object& key,
                                  const sol::object& val, const sol::table& listeners,
                                  std::unordered_set<const void*>& visited) {
  if (val.is<sol::table>()) {
    sol::table tbl = val.as<sol::table>();
    if (IsTableObservable(tbl)) {
      ForwardChildObservable(tbl, listeners);
      backing[key] = tbl;
    } else if (visited.contains(tbl.pointer())) {
      backing[key] = tbl;
    } else {
      sol::table childObs = CreateObservableWithVisited(runtime, tbl, visited);
      ForwardChildObservable(childObs, listeners);
      backing[key] = childObs;
    }
  } else {
    backing[key] = val;
  }
}

// NOLINTNEXTLINE(misc-no-recursion)
sol::table CreateObservableWithVisited(UIRuntime* runtime, const sol::table& rawTable,
                                       std::unordered_set<const void*>& visited) {
  if (IsTableObservable(rawTable) || !visited.insert(rawTable.pointer()).second) {
    return rawTable;
  }

  sol::state* luaPtr = runtime->GetLua();
  sol::table listeners = luaPtr->create_table();
  sol::table backing = luaPtr->create_table();

  for (const auto& [k, v] : rawTable) {
    AssignObservableBackingField(runtime, backing, k, v, listeners, visited);
  }
  visited.erase(rawTable.pointer());

  sol::table proxy = luaPtr->create_table();
  proxy.set("__is_observable", true);
  proxy.set("__raw", backing);
  proxy.set("__listeners", listeners);
  sol::table sinks = luaPtr->create_table();
  sinks[sol::metatable_key] = luaPtr->create_table_with("__mode", "k");
  proxy.set("__forward_sinks", sinks);
  proxy.set_function("__subscribe", [listeners](const sol::table&, const sol::protected_function& fn) mutable {
    listeners[listeners.size() + 1] = fn;
  });

  sol::table meta = luaPtr->create_table();
  meta.set_function("__index", [backing](const sol::table&, const sol::object& key) -> sol::object {
    return backing.get<sol::object>(key);
  });
  meta.set_function("__newindex", [runtime, backing, listeners](const sol::table&, const sol::object& key,
                                                                const sol::object& val) mutable {
    if (backing[key] == val && !val.is<sol::table>()) return;
    std::unordered_set<const void*> localVisited;
    AssignObservableBackingField(runtime, backing, key, val, listeners, localVisited);
    NotifyObservableListeners(listeners, key.is<std::string>() ? key.as<std::string>() : "");
  });
  meta.set_function("__len", [backing](const sol::table&) -> std::size_t { return backing.size(); });

  proxy[sol::metatable_key] = meta;
  return proxy;
}
}  // namespace

// Observable tables wrap nested child tables recursively so deep mutations propagate to parent listeners.
// NOLINTNEXTLINE(misc-no-recursion)
sol::table UIRuntime::CreateObservable(const sol::table& rawTable) {
  std::unordered_set<const void*> visited;
  return CreateObservableWithVisited(this, rawTable, visited);
}

namespace {
bool LoadOxmlMountContent(UIRuntime& runtime, const std::string& sourceStr, std::string& xmlContent,
                          std::string& resolvedMainPath, bool& isInlineXml) {
  const std::string_view trimmed = TrimSv(sourceStr);
  isInlineXml = (!trimmed.empty() && trimmed.front() == '<');
  if (isInlineXml) {
    xmlContent = sourceStr;
    return true;
  }
  auto loaded = runtime.ReadTextAsset(sourceStr, &resolvedMainPath);
  if (!loaded.has_value()) {
    const std::string err = "failed to load OXML asset '" + sourceStr + "'";
    Logger::Error("ui.mount: " + err);
    runtime.RecordDiagnosticsError(sourceStr, 0, err);
    return false;
  }
  xmlContent = std::move(*loaded);
  return true;
}

#ifndef OCTARINE_SHIPPED
std::optional<OxmlDocument> ResolveUseDocForContract(const UIRuntime& runtime, const std::string& src) {
  const auto loaded =
      (!src.empty() && src.front() == '<') ? std::optional<std::string>(src) : runtime.ReadTextAsset(src);
  if (!loaded.has_value()) return std::nullopt;
  const OxmlParseResult parsed = OxmlParser::Parse(*loaded);
  return parsed.ok ? std::optional<OxmlDocument>(parsed.document) : std::nullopt;
}
#endif
}  // namespace

#ifndef OCTARINE_SHIPPED
namespace {
bool ApplyMountedViewParamOverride(sol::state* lua, const std::shared_ptr<UIViewInstance>& v, const std::string& name,
                                   const std::string& value) {
  if (!v || !v->IsCurrentlyMounted() || lua == nullptr) return false;
  if (!v->paramOverrides.valid()) v->paramOverrides = lua->create_table();
  v->paramOverrides.set(name, value);
  if (!v->params.valid()) v->params = lua->create_table();
  v->params.set(name, value);
  v->Refresh();
  return true;
}
}  // namespace

void UIRuntime::WireMountedViewPreviewCallbacks(MountedViewRecord& rec, const std::shared_ptr<void>& viewErased) {
  const std::weak_ptr<UIViewInstance> weakView = std::static_pointer_cast<UIViewInstance>(viewErased);
  rec.inspectContractFn = [this, weakView](OxmlContract& outContract) -> bool {
    const auto v = weakView.lock();
    if (!v || !v->IsCurrentlyMounted()) return false;
    auto resolveUse = [this](const std::string& src) { return ResolveUseDocForContract(*this, src); };
    outContract = InspectOxmlContract(v->document, v->importedUses, resolveUse);
    return true;
  };
  rec.setParamFn = [this, weakView](const std::string& name, const std::string& value) -> bool {
    return ApplyMountedViewParamOverride(lua_, weakView.lock(), name, value);
  };
  rec.getParamsFn = [weakView]() -> sol::object {
    const auto v = weakView.lock();
    if (!v || !v->IsCurrentlyMounted() || !v->params.valid()) return sol::lua_nil;
    return v->params;
  };
  rec.invokeControllerFn = [weakView](const std::string& methodName) -> bool {
    const auto v = weakView.lock();
    if (!v || !v->IsCurrentlyMounted() || methodName.empty()) return false;
    v->InvokeController(methodName);
    return true;
  };
}
#endif

void UIRuntime::WireMountedViewLifecycleCallbacks(MountedViewRecord& rec, const std::shared_ptr<void>& viewErased,
                                                  const std::string& sourceStr,
                                                  const std::optional<Entity>& parentEntity) {
  const std::weak_ptr<UIViewInstance> weakView = std::static_pointer_cast<UIViewInstance>(viewErased);
  rec.isMountedFn = [weakView]() {
    const auto v = weakView.lock();
    return v != nullptr && v->IsCurrentlyMounted();
  };
  rec.rootEntityFn = [weakView]() {
    const auto v = weakView.lock();
    return v != nullptr ? v->rootEntity : Entity{};
  };
  rec.isInertFn = [weakView]() {
    const auto v = weakView.lock();
    return v != nullptr && v->isInert;
  };
  rec.setInertFn = [weakView](const bool inert) {
    if (const auto v = weakView.lock()) v->SetInert(inert);
  };
  rec.unmountFn = [weakView]() {
    if (const auto v = weakView.lock()) v->Unmount();
  };
  rec.reloadFn = [this, weakView, sourceStr, parentEntity](const std::shared_ptr<MountedViewRecord>& r) {
    return ::ReloadMountedView(this, weakView.lock(), sourceStr, parentEntity, r ? &r->watchedFiles : nullptr,
                               r ? &r->lastWriteTimes : nullptr);
  };
}

sol::object UIRuntime::Mount(const sol::object& sourceObj, const sol::optional<sol::table>& opts) {
  if (!sourceObj.is<std::string>()) {
    Logger::Error("ui.mount: source must be a .oxml path/id or XML string");
    return sol::lua_nil;
  }

  const std::string sourceStr = sourceObj.as<std::string>();
  std::string xmlContent;
  std::string resolvedMainPath;
  bool isInlineXml = false;
  if (!LoadOxmlMountContent(*this, sourceStr, xmlContent, resolvedMainPath, isInlineXml)) {
    return sol::lua_nil;
  }

  const OxmlParseResult parsed = OxmlParser::Parse(xmlContent);
  if (!parsed.ok || parsed.document.roots.empty()) {
    const std::string err = parsed.error.empty() ? "OXML document contains no root elements" : parsed.error;
    Logger::Error("ui.mount: OXML parse error at line " + std::to_string(parsed.errorLine) + ": " + err);
    RecordDiagnosticsError(isInlineXml ? "<inline>" : (resolvedMainPath.empty() ? sourceStr : resolvedMainPath),
                           parsed.errorLine, err);
    return sol::lua_nil;
  }

  std::vector<std::string> extraStyleSources = ExtractExtraStyleSources(opts);
  LoadedViewResources resources = LoadResourcesForDocument(this, parsed.document, extraStyleSources, resolvedMainPath);

  auto view = std::make_shared<UIViewInstance>();
  view->runtime = this;
  view->refsTable = lua_->create_table();
  view->viewTable = lua_->create_table();
  view->extraStyleSources = std::move(extraStyleSources);

  std::optional<Entity> parentEntity;
  ApplyMountOptions(opts, view, parentEntity);
  SeedParamsFromDocument(view, parsed.document, *lua_);
  view->stylesheet = std::move(resources.stylesheet);
  view->importedUses = std::move(resources.uses);
#ifndef OCTARINE_SHIPPED
  view->document = parsed.document;
#endif
  view->acquiredAssetIds = AcquireViewCatalogAssets(this, parsed.document, view->stylesheet, view->importedUses);

  const sol::table rootScope = view->MakeScope();
  {
    const UIViewInstance::BuildingTreeGuard guard(*view);
    view->rootEntity = InstantiateNode(view, parsed.document.roots.front(), parentEntity, rootScope, nullptr, nullptr);
  }
  PopulateMountViewTable(view);
  sol::table resultTable = view->viewTable;

  view->viewId = RegisterMountedViewWatch(sourceStr, std::move(resources.watchedPaths), view, parentEntity);
  PinMountedView(lua_, view->viewId, view);
  view->InvokeController("on_mount");

  if (resources.ok) {
    RecordDiagnosticsSuccess(resources.watchedPaths, /*isReload=*/false);
  } else {
    RecordDiagnosticsError(resources.errorFile, resources.errorLine, resources.error);
  }

  return resultTable;
}

uint64_t UIRuntime::RegisterMountedViewWatch(const std::string& sourceStr, std::vector<std::string> watchedPaths,
                                             const std::shared_ptr<void>& viewErased,
                                             const std::optional<Entity>& parentEntity) {
  std::erase_if(mounted_views_, [](const auto& r) {
    if (!r || !r->isMountedFn || !r->isMountedFn()) {
      if (r && r->unmountFn) r->unmountFn();
      return true;
    }
    return false;
  });
  auto rec = std::make_shared<MountedViewRecord>();
  rec->id = next_view_id_++;
  rec->sourcePath = sourceStr;
  SyncRecordWatchTimes(rec->watchedFiles, rec->lastWriteTimes, std::move(watchedPaths));

  WireMountedViewLifecycleCallbacks(*rec, viewErased, sourceStr, parentEntity);
#ifndef OCTARINE_SHIPPED
  WireMountedViewPreviewCallbacks(*rec, viewErased);
#endif
  const uint64_t id = rec->id;
  mounted_views_.push_back(std::move(rec));
  return id;
}

namespace {
bool HasWatchedViewFilesChanged(const std::vector<std::string>& watchedFiles,
                                std::unordered_map<std::string, fs::file_time_type>& lastWriteTimes) {
  bool changed = false;
  std::error_code ec;
  for (const auto& file : watchedFiles) {
    if (!fs::exists(file, ec)) continue;
    const auto mtime = fs::last_write_time(file, ec);
    if (ec) continue;
    if (const auto it = lastWriteTimes.find(file); it == lastWriteTimes.end() || it->second != mtime) {
      lastWriteTimes[file] = mtime;
      changed = true;
    }
  }
  return changed;
}
}  // namespace

void UIRuntime::PollHotReload(const float deltaTime, const float pollIntervalSeconds) {
  if (pollIntervalSeconds > 0.0f) {
    hot_reload_accum_ += deltaTime;
    if (hot_reload_accum_ < pollIntervalSeconds) {
      return;
    }
    hot_reload_accum_ = 0.0f;
  }
  std::erase_if(mounted_views_, [](const auto& r) {
    if (!r || !r->isMountedFn || !r->isMountedFn()) {
      if (r && r->unmountFn) r->unmountFn();
      return true;
    }
    return false;
  });
  const auto snapshot = mounted_views_;
  for (const auto& rec : snapshot) {
    if (!rec->watchedFiles.empty() && HasWatchedViewFilesChanged(rec->watchedFiles, rec->lastWriteTimes) &&
        rec->reloadFn) {
      Logger::Info("UIRuntime: hot-reloading UI view '" + rec->sourcePath + "'");
      rec->reloadFn(rec);
    }
  }
}

std::vector<UIRuntime::MountedViewDebugInfo> UIRuntime::GetMountedViewsDebugInfo() const {
  std::vector<MountedViewDebugInfo> list;
  list.reserve(mounted_views_.size());
  for (const auto& rec : mounted_views_) {
    if (!rec) continue;
    list.push_back({
        .id = rec->id,
        .sourcePath = rec->sourcePath,
        .watchedFiles = rec->watchedFiles,
        .isMounted = rec->isMountedFn ? rec->isMountedFn() : false,
    });
  }
  return list;
}

std::optional<uint64_t> UIRuntime::FindViewIdByRoot(const Entity rootEntity) const {
  if (registry_ == nullptr || !registry_->IsAlive(rootEntity)) return std::nullopt;
  for (const auto& rec : mounted_views_) {
    if (!rec || !rec->isMountedFn || !rec->isMountedFn()) continue;
    if (rec->rootEntityFn && rec->rootEntityFn().id == rootEntity.id) {
      return rec->id;
    }
  }
  return std::nullopt;
}

bool UIRuntime::IsViewMounted(const uint64_t id) const {
  if (id == 0) return false;
  for (const auto& rec : mounted_views_) {
    if (rec && rec->id == id) {
      return rec->isMountedFn && rec->isMountedFn();
    }
  }
  return false;
}

Entity UIRuntime::GetViewRoot(const uint64_t id) const {
  if (id == 0) return {};
  for (const auto& rec : mounted_views_) {
    if (rec && rec->id == id && rec->isMountedFn && rec->isMountedFn() && rec->rootEntityFn) {
      return rec->rootEntityFn();
    }
  }
  return {};
}

void UIRuntime::UnmountView(const uint64_t id) {
  if (id == 0) return;
  for (const auto& rec : mounted_views_) {
    if (rec && rec->id == id && rec->unmountFn) {
      rec->unmountFn();
      break;
    }
  }
  std::erase_if(mounted_views_, [id](const auto& r) {
    if (!r || r->id == id || !r->isMountedFn || !r->isMountedFn()) {
      if (r && r->id != id && r->unmountFn) r->unmountFn();
      return true;
    }
    return false;
  });
}

void UIRuntime::UnmountAllViews() {
  const auto snapshot = std::move(mounted_views_);
  mounted_views_.clear();
  for (const auto& rec : snapshot) {
    if (rec && rec->unmountFn) {
      rec->unmountFn();
    }
  }
  if (lua_ != nullptr) {
    sol::table reg = lua_->registry();
    sol::object tblObj = reg.raw_get<sol::object>(kMountedViewsRegKey);
    if (tblObj.is<sol::table>()) {
      tblObj.as<sol::table>().clear();
    }
  }
}

void UIRuntime::ClearCustomWidgets() { custom_widgets_.clear(); }

std::vector<std::pair<uint64_t, bool>> UIRuntime::SetNonPreviewViewsInert(const uint64_t previewViewId,
                                                                          const bool inert) {
  std::vector<std::pair<uint64_t, bool>> prevStates;
  for (const auto& rec : mounted_views_) {
    if (!rec || rec->id == previewViewId || !rec->isMountedFn || !rec->isMountedFn()) continue;
    const bool wasInert = rec->isInertFn ? rec->isInertFn() : false;
    prevStates.emplace_back(rec->id, wasInert);
    if (rec->setInertFn) {
      rec->setInertFn(inert);
    }
  }
  return prevStates;
}

void UIRuntime::RestoreViewsInert(const std::vector<std::pair<uint64_t, bool>>& prevStates) {
  for (const auto& [viewId, wasInert] : prevStates) {
    const auto it =
        std::ranges::find_if(mounted_views_, [viewId](const auto& rec) { return rec && rec->id == viewId; });
    if (it != mounted_views_.end() && *it && (*it)->isMountedFn && (*it)->isMountedFn() && (*it)->setInertFn) {
      (*it)->setInertFn(wasInert);
    }
  }
}

void UIRuntime::RecordDiagnosticsError(std::string file, const int line, std::string error) {
  diagnostics_.lastParseOk = false;
  diagnostics_.lastErrorFile = std::move(file);
  diagnostics_.lastErrorLine = line;
  diagnostics_.lastError = std::move(error);
}

void UIRuntime::RecordDiagnosticsSuccess(std::vector<std::string> watchedFiles, const bool isReload) {
  diagnostics_.lastParseOk = true;
  diagnostics_.lastError.clear();
  diagnostics_.lastErrorFile.clear();
  diagnostics_.lastErrorLine = 0;
  diagnostics_.watchedFiles = std::move(watchedFiles);
  if (isReload) {
    ++diagnostics_.reloadCount;
  }
}

bool UIRuntime::ReloadMountedView(const uint64_t id) {
  const auto it =
      std::ranges::find_if(mounted_views_, [id](const auto& rec) { return rec && rec->id == id && rec->reloadFn; });
  if (it == mounted_views_.end()) return false;
  return (*it)->reloadFn(*it);
}

void UIRuntime::ReloadAllMountedViews() {
  const auto snapshot = mounted_views_;
  for (const auto& rec : snapshot) {
    if (rec && rec->reloadFn) {
      rec->reloadFn(rec);
    }
  }
}

#ifndef OCTARINE_SHIPPED
bool UIRuntime::InspectMountedViewContract(const uint64_t id, OxmlContract& outContract) const {
  if (id == 0) return false;
  for (const auto& rec : mounted_views_) {
    if (rec && rec->id == id && rec->inspectContractFn) {
      return rec->inspectContractFn(outContract);
    }
  }
  return false;
}

bool UIRuntime::SetMountedViewParam(const uint64_t id, const std::string& name, const std::string& value) {
  if (id == 0 || name.empty()) return false;
  for (const auto& rec : mounted_views_) {
    if (rec && rec->id == id && rec->setParamFn) {
      return rec->setParamFn(name, value);
    }
  }
  return false;
}

sol::object UIRuntime::GetMountedViewParams(const uint64_t id) const {
  if (id == 0) return sol::lua_nil;
  for (const auto& rec : mounted_views_) {
    if (rec && rec->id == id && rec->getParamsFn) {
      return rec->getParamsFn();
    }
  }
  return sol::lua_nil;
}

bool UIRuntime::InvokeMountedViewController(const uint64_t id, const std::string& methodName) {
  if (id == 0 || methodName.empty()) return false;
  for (const auto& rec : mounted_views_) {
    if (rec && rec->id == id && rec->invokeControllerFn) {
      return rec->invokeControllerFn(methodName);
    }
  }
  return false;
}
#endif

namespace {
bool IsUiRefResolvable(const AssetManager& assetManager, const std::string& refPathOrId) {
  if (refPathOrId.empty() || refPathOrId.front() == '<' || refPathOrId.find('{') != std::string::npos) {
    return true;
  }
  if (assetManager.GetCatalog().Contains(refPathOrId)) return true;
  const std::string filename = fs::path(refPathOrId).filename().string();
  if (!filename.empty() && assetManager.GetCatalog().Contains(filename)) return true;
  const std::string fullPath = assetManager.GetFullPath(refPathOrId);
  std::error_code ec;
  return !fullPath.empty() && fs::exists(fullPath, ec);
}

int LogOxmlRefFailure(const std::string& id, std::string_view kind, const std::string& ref) {
  std::string msg = "Bake UI validation: OXML '";
  msg.append(id).append("' references ").append(kind).append(" '").append(ref).push_back('\'');
  Logger::Error(msg);
  return 1;
}

int CheckOxmlFileListRefs(const AssetManager& assetManager, const std::string& id,
                          const std::vector<std::string>& sources, std::string_view kind) {
  int failures = 0;
  for (const auto& src : sources) {
    if (!IsUiRefResolvable(assetManager, src)) {
      failures += LogOxmlRefFailure(id, kind, src);
    }
  }
  return failures;
}

int CheckOxmlCatalogListRefs(const AssetManager& assetManager, const std::string& id,
                             const std::vector<std::string>& ids, std::string_view kind,
                             std::string_view allowedDefault = "") {
  int failures = 0;
  for (const auto& assetId : ids) {
    if (assetId != allowedDefault && !assetManager.GetCatalog().Contains(assetId)) {
      failures += LogOxmlRefFailure(id, kind, assetId);
    }
  }
  return failures;
}

int CheckOxmlAssetRefs(const AssetManager& assetManager, const std::string& id, const OxmlAssetRefs& refs) {
  int failures = 0;
  failures += CheckOxmlFileListRefs(assetManager, id, refs.styleSources, "missing stylesheet");
  failures += CheckOxmlFileListRefs(assetManager, id, refs.useSources, "missing <Use> asset");
  failures += CheckOxmlCatalogListRefs(assetManager, id, refs.spriteIds, "unknown sprite");
  failures += CheckOxmlCatalogListRefs(assetManager, id, refs.fontIds, "unknown font", "default-16");
  failures += CheckOxmlCatalogListRefs(assetManager, id, refs.audioIds, "unknown audio");
  return failures;
}
}  // namespace

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
    failures += CheckOxmlAssetRefs(assetManager, id, OxmlParser::CollectStaticAssetRefs(res.document));
  }
  return failures;
}
