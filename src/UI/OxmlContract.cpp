#include "UI/OxmlContract.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string_view>
#include <unordered_set>

#include "UI/OssParser.h"

namespace {

constexpr int kMaxUseNestingDepth = 32;

int TypeRank(const ContractFieldType t) {
  switch (t) {
    case ContractFieldType::Unknown:
      return 0;
    case ContractFieldType::String:
      return 1;
    case ContractFieldType::Boolean:
      return 2;
    case ContractFieldType::Number:
      return 3;
    case ContractFieldType::Table:
      return 4;
    case ContractFieldType::Array:
      return 5;
  }
  return 0;
}

ContractFieldType PromoteFieldType(const ContractFieldType a, const ContractFieldType b) {
  return TypeRank(a) >= TypeRank(b) ? a : b;
}

std::string TrimCopy(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) {
    sv.remove_suffix(1);
  }
  return std::string(sv);
}

bool IsIdentStart(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalpha(uc) != 0 || c == '_';
}

bool IsIdentChar(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) != 0 || c == '_';
}

bool IsBuiltInOxmlTag(std::string_view tag) {
  static const std::unordered_set<std::string_view> kBuiltInTags = {
      "Canvas", "Panel", "Box",    "Row",         "Column", "Grid",        "ScrollView", "RadioGroup",
      "Label",  "Image", "Button", "ProgressBar", "Toggle", "RadioButton", "Slider",     "Slot",
  };
  return kBuiltInTags.contains(tag);
}

bool IsColorAttrName(std::string_view attr) {
  static const std::unordered_set<std::string_view> kColorAttrs = {
      "color",      "background-color", "bg-color",     "background",    "border-color",
      "text-color", "track-color",      "shadow-color", "outline-color", "text-outline-color",
  };
  return kColorAttrs.contains(attr);
}

std::string DetectUnsupportedEventAttr(std::string_view attr) {
  if (attr == "on-click" || attr == "on-change" || attr == "on-focus") {
    return "";
  }
  if (attr.starts_with("on-")) {
    return "Use supported event attributes: on-click, on-change, or on-focus";
  }
  static const std::unordered_set<std::string_view> kTypoEvents = {
      "onclick", "onchange", "onfocus", "onblur", "onsubmit", "onhover",
      "onClick", "onChange", "onFocus", "onBlur", "onSubmit", "onHover",
  };
  if (kTypoEvents.contains(attr)) {
    return "Use hyphenated event attributes: on-click, on-change, or on-focus";
  }
  return "";
}

bool IsLuaKeywordOrBuiltin(std::string_view path) {
  static const std::unordered_set<std::string_view> kSkip = {
      "true",     "false",    "nil",   "and",    "or",   "not",    "math", "string", "table",
      "tostring", "tonumber", "pairs", "ipairs", "next", "select", "type", "unpack",
  };
  const std::size_t dot = path.find('.');
  const std::string_view head = dot == std::string_view::npos ? path : path.substr(0, dot);
  return kSkip.contains(head);
}

enum class ExprTokKind { Ident, Number, String, Hash, Op, Cmp, BoolOp, NotOp, OpenParen, Other };

struct ExprTok {
  ExprTokKind kind{ExprTokKind::Other};
  std::string text{};
};

void SkipQuotedLiteral(std::string_view expr, std::size_t& i, const char quote) {
  ++i;
  while (i < expr.size()) {
    if (expr[i] == '\\' && i + 1 < expr.size()) {
      i += 2;
      continue;
    }
    if (expr[i] == quote) {
      ++i;
      return;
    }
    ++i;
  }
}

bool TryReadMultiCharOp(std::string_view expr, std::size_t& i, ExprTok& tok) {
  if (i + 1 >= expr.size()) return false;
  const std::string_view two = expr.substr(i, 2);
  if (two == "&&" || two == "||") {
    tok = {ExprTokKind::BoolOp, std::string(two)};
    i += 2;
    return true;
  }
  if (two == "==" || two == "!=" || two == "~=" || two == "<=" || two == ">=") {
    tok = {ExprTokKind::Cmp, std::string(two)};
    i += 2;
    return true;
  }
  if (two == "..") {
    tok = {ExprTokKind::Other, ".."};
    i += 2;
    return true;
  }
  return false;
}

ExprTokKind ClassifyIdentToken(std::string_view word) {
  if (word == "and" || word == "or") return ExprTokKind::BoolOp;
  if (word == "not") return ExprTokKind::NotOp;
  return ExprTokKind::Ident;
}

std::string ReadDottedIdent(std::string_view expr, std::size_t& i) {
  const std::size_t start = i;
  while (i < expr.size() && IsIdentChar(expr[i])) ++i;
  while (i + 1 < expr.size() && expr[i] == '.' && IsIdentStart(expr[i + 1])) {
    ++i;
    while (i < expr.size() && IsIdentChar(expr[i])) ++i;
  }
  return std::string(expr.substr(start, i - start));
}

void ReadNumberTok(std::string_view expr, std::size_t& i, std::vector<ExprTok>& out) {
  const std::size_t start = i;
  while (i < expr.size() && (std::isdigit(static_cast<unsigned char>(expr[i])) != 0 || expr[i] == '.')) {
    ++i;
  }
  out.push_back({ExprTokKind::Number, std::string(expr.substr(start, i - start))});
}

void ReadSingleCharTok(std::string_view expr, std::size_t& i, std::vector<ExprTok>& out) {
  const char c = expr[i++];
  if (c == '#') {
    out.push_back({ExprTokKind::Hash, "#"});
  } else if (c == '!') {
    out.push_back({ExprTokKind::NotOp, "!"});
  } else if (c == '<' || c == '>') {
    out.push_back({ExprTokKind::Cmp, std::string(1, c)});
  } else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%') {
    out.push_back({ExprTokKind::Op, std::string(1, c)});
  } else if (c == '(') {
    out.push_back({ExprTokKind::OpenParen, "("});
  } else {
    out.push_back({ExprTokKind::Other, std::string(1, c)});
  }
}

std::vector<ExprTok> TokenizeExpr(std::string_view expr) {
  std::vector<ExprTok> out;
  std::size_t i = 0;
  while (i < expr.size()) {
    if (std::isspace(static_cast<unsigned char>(expr[i])) != 0) {
      ++i;
      continue;
    }
    if (expr[i] == '"' || expr[i] == '\'') {
      SkipQuotedLiteral(expr, i, expr[i]);
      out.push_back({ExprTokKind::String, ""});
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(expr[i])) != 0) {
      ReadNumberTok(expr, i, out);
      continue;
    }
    if (IsIdentStart(expr[i])) {
      std::string word = ReadDottedIdent(expr, i);
      out.push_back({ClassifyIdentToken(word), std::move(word)});
      continue;
    }
    ExprTok twoTok;
    if (TryReadMultiCharOp(expr, i, twoTok)) {
      out.push_back(std::move(twoTok));
      continue;
    }
    ReadSingleCharTok(expr, i, out);
  }
  return out;
}

struct UseMapEntry {
  std::string src{};
  std::shared_ptr<const OxmlDocument> doc{};
};

struct InspectScope {
  std::unordered_map<std::string, std::string> aliasMap{};
  std::unordered_set<std::string> ignoredVars{};
  bool inRootDoc{true};
  std::unordered_set<std::string> activeUseSources{};
  std::shared_ptr<const InspectScope> callerScope{};
  std::vector<const OxmlNode*> slotCandidates{};
  std::unordered_map<std::string, UseMapEntry> useMap{};
};

struct TraversalFrame {
  const OxmlNode* node{nullptr};
  std::shared_ptr<const InspectScope> scope{};
  int useDepth{0};
};

struct ExprFieldMeta {
  ContractFieldType type{ContractFieldType::String};
  std::string sampleHint{};
  bool isTwoWay{false};
  bool conditionalOnly{false};
  float minVal{0.0f};
  float maxVal{1.0f};
  float step{0.0f};
  bool hasSliderRange{false};
  std::vector<std::string> radioOptions{};
  int line{0};
};

struct ContractBuilder {
  OxmlContract out{};
  std::unordered_set<std::string> rootParams{};
  std::unordered_map<std::string, std::size_t> fieldIndex{};
  std::unordered_set<std::string> seenCallbacks{};
  std::unordered_set<std::string> seenRefs{};
  std::unordered_set<std::string> seenSlots{};
  std::unordered_map<std::string, std::shared_ptr<const OxmlDocument>> docCache{};
  OxmlResolveUseFn resolveUse{};

  void AddField(const std::string& path, const ExprFieldMeta& meta) {
    if (path.empty()) return;
    const auto it = fieldIndex.find(path);
    if (it == fieldIndex.end()) {
      fieldIndex[path] = out.fields.size();
      ContractField f;
      f.path = path;
      f.inferredType = meta.type;
      f.conditionalOnly = meta.conditionalOnly;
      f.isTwoWay = meta.isTwoWay;
      f.sampleHint = meta.sampleHint;
      f.minVal = meta.minVal;
      f.maxVal = meta.maxVal;
      f.step = meta.step;
      f.hasSliderRange = meta.hasSliderRange;
      f.radioOptions = meta.radioOptions;
      f.firstLine = meta.line;
      out.fields.push_back(std::move(f));
      return;
    }
    ContractField& f = out.fields[it->second];
    f.inferredType = PromoteFieldType(f.inferredType, meta.type);
    f.conditionalOnly = f.conditionalOnly && meta.conditionalOnly;
    f.isTwoWay = f.isTwoWay || meta.isTwoWay;
    if (f.sampleHint == "disabled" && !meta.conditionalOnly && meta.sampleHint != "disabled") {
      f.sampleHint = meta.sampleHint;
    } else if (f.sampleHint.empty() && !meta.sampleHint.empty()) {
      f.sampleHint = meta.sampleHint;
    }
    if (meta.hasSliderRange && !f.hasSliderRange) {
      f.hasSliderRange = true;
      f.minVal = meta.minVal;
      f.maxVal = meta.maxVal;
      f.step = meta.step;
    }
    for (const auto& opt : meta.radioOptions) {
      if (std::ranges::find(f.radioOptions, opt) == f.radioOptions.end()) {
        f.radioOptions.push_back(opt);
      }
    }
  }

  void AddCallback(const std::string& attr, const std::string& rawExpr, const int line) {
    std::string clean = TrimCopy(rawExpr);
    if (clean.empty()) return;
    std::string name = clean;
    if (const std::size_t paren = name.find('('); paren != std::string::npos) {
      name = TrimCopy(name.substr(0, paren));
    }
    if (name.empty() || !seenCallbacks.insert(name).second) return;
    out.callbacks.push_back(ContractCallback{std::move(name), std::move(clean), attr, line});
  }

  void AddRef(const std::string& refName) {
    if (!refName.empty() && seenRefs.insert(refName).second) {
      out.refs.push_back(refName);
    }
  }

  void AddSlot(const std::string& slotName) {
    const std::string label = slotName.empty() ? "(default)" : slotName;
    if (seenSlots.insert(label).second) {
      out.slots.push_back(label);
    }
  }

  std::shared_ptr<const OxmlDocument> LoadUseDoc(const std::string& useName, const std::string& useSrc,
                                                 const std::unordered_map<std::string, OxmlDocument>& preloadedUses) {
    if (!useSrc.empty()) {
      if (const auto cit = docCache.find(useSrc); cit != docCache.end()) {
        return cit->second;
      }
    }
    if (const auto pit = preloadedUses.find(useName); pit != preloadedUses.end()) {
      auto ptr = std::make_shared<OxmlDocument>(pit->second);
      if (!useSrc.empty()) docCache[useSrc] = ptr;
      return ptr;
    }
    if (resolveUse && !useSrc.empty()) {
      if (auto loaded = resolveUse(useSrc)) {
        auto ptr = std::make_shared<OxmlDocument>(std::move(*loaded));
        docCache[useSrc] = ptr;
        return ptr;
      }
    }
    return nullptr;
  }
};

std::optional<std::string> ResolveContractPath(std::string_view rawPath, const InspectScope& scope,
                                               const std::unordered_set<std::string>& rootParams,
                                               const bool allowRootParamAsContext) {
  if (rawPath.empty()) return std::nullopt;
  const std::size_t dot = rawPath.find('.');
  const std::string head(dot == std::string_view::npos ? rawPath : rawPath.substr(0, dot));
  const std::string_view rest = dot == std::string_view::npos ? std::string_view{} : rawPath.substr(dot);

  if (scope.ignoredVars.contains(head)) {
    return std::nullopt;
  }
  if (const auto ait = scope.aliasMap.find(head); ait != scope.aliasMap.end()) {
    return ait->second + std::string(rest);
  }
  if (!allowRootParamAsContext && rootParams.contains(head)) {
    return std::nullopt;
  }
  return std::string(rawPath);
}

bool IsNumericComparisonTok(const ExprTok* tok) {
  if (tok == nullptr) return false;
  if (tok->kind == ExprTokKind::Op) return true;
  return tok->kind == ExprTokKind::Cmp && tok->text != "==" && tok->text != "!=" && tok->text != "~=";
}

bool HasAdjacentEqualityKind(const std::vector<ExprTok>& toks, const std::size_t idx, const ExprTokKind rhsKind) {
  const bool leftMatch = idx >= 2 && toks[idx - 1].kind == ExprTokKind::Cmp && toks[idx - 2].kind == rhsKind;
  const bool rightMatch =
      idx + 2 < toks.size() && toks[idx + 1].kind == ExprTokKind::Cmp && toks[idx + 2].kind == rhsKind;
  return leftMatch || rightMatch;
}

std::pair<ContractFieldType, std::string> InferTokenTypeAndHint(const std::vector<ExprTok>& toks, const std::size_t idx,
                                                                const ContractFieldType defaultType,
                                                                const std::string& defaultHint) {
  const ExprTok* prev = idx > 0 ? &toks[idx - 1] : nullptr;
  const ExprTok* next = idx + 1 < toks.size() ? &toks[idx + 1] : nullptr;

  if (prev != nullptr && prev->kind == ExprTokKind::Hash) return {ContractFieldType::Array, ""};
  if (prev != nullptr && prev->text == "/") return {ContractFieldType::Number, "divided_den"};
  if (next != nullptr && next->text == "/") return {ContractFieldType::Number, "divided_num"};
  if (IsNumericComparisonTok(prev) || IsNumericComparisonTok(next) ||
      HasAdjacentEqualityKind(toks, idx, ExprTokKind::Number)) {
    return {ContractFieldType::Number, defaultHint};
  }
  const bool isBoolAdjacent =
      (prev != nullptr && (prev->kind == ExprTokKind::NotOp || prev->kind == ExprTokKind::BoolOp)) ||
      (next != nullptr && next->kind == ExprTokKind::BoolOp);
  if (isBoolAdjacent) return {ContractFieldType::Boolean, defaultHint};
  if (HasAdjacentEqualityKind(toks, idx, ExprTokKind::String)) {
    return {ContractFieldType::String, defaultHint};
  }
  return {defaultType, defaultHint};
}

void RecordExpressionBindings(ContractBuilder& builder, std::string_view expr, const InspectScope& scope,
                              const ContractFieldType defaultType, const std::string& defaultHint,
                              const bool conditionalOnly, const int line, const bool allowRootParam = false) {
  const std::vector<ExprTok> toks = TokenizeExpr(expr);
  for (std::size_t i = 0; i < toks.size(); ++i) {
    if (toks[i].kind != ExprTokKind::Ident) continue;
    if (i + 1 < toks.size() && toks[i + 1].kind == ExprTokKind::OpenParen) continue;
    if (IsLuaKeywordOrBuiltin(toks[i].text)) continue;

    const auto resolved = ResolveContractPath(toks[i].text, scope, builder.rootParams, allowRootParam);
    if (!resolved.has_value()) continue;

    const auto [inferred, hint] = InferTokenTypeAndHint(toks, i, defaultType, defaultHint);
    ExprFieldMeta meta;
    meta.type = inferred;
    meta.sampleHint = hint;
    meta.conditionalOnly = conditionalOnly;
    meta.line = line;
    builder.AddField(*resolved, meta);
  }
}

void RecordInterpolatedBindings(ContractBuilder& builder, std::string_view rawVal, const InspectScope& scope,
                                const ContractFieldType defaultType, const std::string& defaultHint,
                                const bool conditionalOnly, const int line) {
  std::size_t pos = 0;
  while (pos < rawVal.size()) {
    const std::size_t open = rawVal.find('{', pos);
    if (open == std::string_view::npos) break;
    const std::size_t close = rawVal.find('}', open + 1);
    if (close == std::string_view::npos) break;
    RecordExpressionBindings(builder, rawVal.substr(open + 1, close - open - 1), scope, defaultType, defaultHint,
                             conditionalOnly, line);
    pos = close + 1;
  }
}

void RecordInlineStyleBindings(ContractBuilder& builder, std::string_view rawStyle, const InspectScope& scope,
                               const int line) {
  std::size_t pos = 0;
  while (pos < rawStyle.size()) {
    const std::size_t open = rawStyle.find('{', pos);
    if (open == std::string_view::npos) break;
    const std::size_t close = rawStyle.find('}', open + 1);
    if (close == std::string_view::npos) break;

    const std::size_t semi = rawStyle.rfind(';', open);
    const std::size_t segStart = (semi == std::string_view::npos) ? 0 : semi + 1;
    const std::size_t colon = rawStyle.find(':', segStart);
    std::string hint;
    if (colon != std::string_view::npos && colon < open) {
      const std::string prop = TrimCopy(rawStyle.substr(segStart, colon - segStart));
      if (IsColorAttrName(prop)) hint = "color";
    }
    RecordExpressionBindings(builder, rawStyle.substr(open + 1, close - open - 1), scope, ContractFieldType::String,
                             hint, false, line);
    pos = close + 1;
  }
}

std::vector<std::string> CollectRadioOptions(const OxmlNode& radioGroupNode) {
  std::vector<std::string> opts;
  std::vector<const OxmlNode*> stack;
  for (auto it = radioGroupNode.children.rbegin(); it != radioGroupNode.children.rend(); ++it) {
    stack.push_back(&(*it));
  }
  while (!stack.empty()) {
    const OxmlNode* cur = stack.back();
    stack.pop_back();
    if (cur->tag == "RadioGroup") continue;
    if (cur->tag == "RadioButton") {
      const std::string opt = cur->GetAttr("value", cur->GetAttr("text"));
      if (!opt.empty() && !OxmlParser::ContainsBinding(opt) && std::ranges::find(opts, opt) == opts.end()) {
        opts.push_back(opt);
      }
    }
    for (auto it = cur->children.rbegin(); it != cur->children.rend(); ++it) {
      stack.push_back(&(*it));
    }
  }
  return opts;
}

void PopulateSliderRangeMeta(const OxmlNode& node, ExprFieldMeta& meta) {
  meta.type = ContractFieldType::Number;
  meta.sampleHint = "slider";
  meta.hasSliderRange = true;
  OssParser::ParseFloat(node.GetAttr("min", "0"), meta.minVal);
  OssParser::ParseFloat(node.GetAttr("max", "1"), meta.maxVal);
  OssParser::ParseFloat(node.GetAttr("step", "0"), meta.step);
}

void InspectBindValueAttr(ContractBuilder& builder, const OxmlNode& node, const InspectScope& scope) {
  const std::string bindPath = TrimCopy(node.GetAttr("bind-value"));
  if (bindPath.empty()) return;
  const auto resolved = ResolveContractPath(bindPath, scope, builder.rootParams, /*allowRootParam=*/true);
  if (!resolved.has_value()) return;

  ExprFieldMeta meta;
  meta.isTwoWay = true;
  meta.conditionalOnly = false;
  meta.line = node.line;
  if (node.tag == "Toggle") {
    meta.type = ContractFieldType::Boolean;
  } else if (node.tag == "Slider") {
    PopulateSliderRangeMeta(node, meta);
  } else if (node.tag == "RadioGroup") {
    meta.type = ContractFieldType::String;
    meta.sampleHint = "radio";
    meta.radioOptions = CollectRadioOptions(node);
  } else {
    meta.type = ContractFieldType::String;
  }
  builder.AddField(*resolved, meta);
}

void InspectSliderValueAttr(ContractBuilder& builder, const OxmlNode& node, const std::string& val,
                            const InspectScope& scope) {
  ExprFieldMeta meta;
  meta.line = node.line;
  PopulateSliderRangeMeta(node, meta);

  std::string pureExpr;
  if (OxmlParser::IsPureBinding(val, pureExpr)) {
    const std::vector<ExprTok> toks = TokenizeExpr(pureExpr);
    for (std::size_t i = 0; i < toks.size(); ++i) {
      if (toks[i].kind != ExprTokKind::Ident || IsLuaKeywordOrBuiltin(toks[i].text)) continue;
      if (i + 1 < toks.size() && toks[i + 1].kind == ExprTokKind::OpenParen) continue;
      if (const auto res = ResolveContractPath(toks[i].text, scope, builder.rootParams, false)) {
        builder.AddField(*res, meta);
      }
    }
    return;
  }
  RecordInterpolatedBindings(builder, val, scope, ContractFieldType::Number, "slider", false, node.line);
}

bool TryInspectCommonControlAttr(ContractBuilder& builder, const std::string& attr, const std::string& val,
                                 const int line, const InspectScope& scope) {
  if (attr == "ref") {
    builder.AddRef(val);
    return true;
  }
  if (attr == "if") {
    std::string unwrapped;
    if (!OxmlParser::IsPureBinding(val, unwrapped)) unwrapped = val;
    RecordExpressionBindings(builder, unwrapped, scope, ContractFieldType::Boolean, "", true, line);
    return true;
  }
  if (attr == "visible" || attr == "disabled" || attr == "checked") {
    if (OxmlParser::ContainsBinding(val)) {
      const bool condOnly = (attr != "checked");
      const std::string hint = (attr == "disabled") ? "disabled" : "";
      RecordInterpolatedBindings(builder, val, scope, ContractFieldType::Boolean, hint, condOnly, line);
    }
    return true;
  }
  return false;
}

void InspectSingleBuiltInAttr(ContractBuilder& builder, const OxmlNode& node, const std::string& attr,
                              const std::string& val, const InspectScope& scope) {
  if (attr == "each" || attr == "item-key" || attr == "id" || attr == "slot" || attr == "name") return;
  if (TryInspectCommonControlAttr(builder, attr, val, node.line, scope)) return;
  if (const std::string sug = DetectUnsupportedEventAttr(attr); !sug.empty()) {
    builder.out.unsupportedAttrs.push_back(UnsupportedAttrRef{node.tag, attr, sug, node.line});
    return;
  }
  if (attr == "on-click" || attr == "on-change" || attr == "on-focus") {
    builder.AddCallback(attr, val, node.line);
    return;
  }
  if (attr == "bind-value") {
    InspectBindValueAttr(builder, node, scope);
    return;
  }
  if (!OxmlParser::ContainsBinding(val)) return;
  if (attr == "style") {
    RecordInlineStyleBindings(builder, val, scope, node.line);
    return;
  }
  if (node.tag == "ProgressBar" && attr == "value") {
    RecordInterpolatedBindings(builder, val, scope, ContractFieldType::Number, "progress", false, node.line);
    return;
  }
  if (node.tag == "Slider" && attr == "value") {
    InspectSliderValueAttr(builder, node, val, scope);
    return;
  }
  const std::string hint = (attr == "sprite" || attr == "icon") ? "sprite" : (IsColorAttrName(attr) ? "color" : "");
  RecordInterpolatedBindings(builder, val, scope, ContractFieldType::String, hint, false, node.line);
}

void InspectBuiltInNodeAttrs(ContractBuilder& builder, const OxmlNode& node, const InspectScope& scope) {
  for (const auto& [attr, val] : node.attributes) {
    InspectSingleBuiltInAttr(builder, node, attr, val, scope);
  }
}

void InspectCallSiteAttrs(ContractBuilder& builder, const OxmlNode& node, const InspectScope& scope) {
  for (const auto& [attr, val] : node.attributes) {
    if (attr == "each" || attr == "item-key" || attr == "slot") continue;
    if (TryInspectCommonControlAttr(builder, attr, val, node.line, scope)) continue;
    if (OxmlParser::ContainsBinding(val)) {
      RecordInterpolatedBindings(builder, val, scope, ContractFieldType::String, "", false, node.line);
    }
  }
}

bool IsSimpleDottedIdent(std::string_view sv) {
  if (sv.empty() || !IsIdentStart(sv.front())) return false;
  for (std::size_t i = 0; i < sv.size(); ++i) {
    const char c = sv[i];
    if (c == '.') {
      if (i + 1 >= sv.size() || !IsIdentStart(sv[i + 1])) return false;
      continue;
    }
    if (!IsIdentChar(c)) return false;
  }
  return true;
}

std::shared_ptr<const InspectScope> ApplyEachScopeIfPresent(ContractBuilder& builder, const OxmlNode& node,
                                                            const std::shared_ptr<const InspectScope>& outerScope) {
  if (!node.HasAttr("each")) return outerScope;
  const OxmlEachClause clause = OxmlParser::ParseEachClause(node.GetAttr("each"));
  if (!clause.valid) return outerScope;

  RecordExpressionBindings(builder, clause.listExpr, *outerScope, ContractFieldType::Array, "", false, node.line);

  auto inner = std::make_shared<InspectScope>(*outerScope);
  const std::string listTrimmed = TrimCopy(clause.listExpr);
  if (IsSimpleDottedIdent(listTrimmed)) {
    if (const auto resolvedList = ResolveContractPath(listTrimmed, *outerScope, builder.rootParams, false)) {
      inner->aliasMap[clause.itemVar] = *resolvedList + "[]";
      inner->ignoredVars.erase(clause.itemVar);
    } else {
      inner->ignoredVars.insert(clause.itemVar);
    }
  } else {
    inner->ignoredVars.insert(clause.itemVar);
  }
  if (!clause.indexVar.empty()) {
    inner->ignoredVars.insert(clause.indexVar);
  }
  return inner;
}

std::unordered_map<std::string, UseMapEntry> BuildUseMapForDoc(
    ContractBuilder& builder, const OxmlDocument& doc,
    const std::unordered_map<std::string, OxmlDocument>& preloadedUses) {
  std::unordered_map<std::string, UseMapEntry> out;
  for (const auto& imp : doc.uses) {
    if (auto ptr = builder.LoadUseDoc(imp.name, imp.src, preloadedUses)) {
      out[imp.name] = UseMapEntry{imp.src, std::move(ptr)};
    }
  }
  return out;
}

void PushNodeChildren(std::vector<TraversalFrame>& stack, const OxmlNode& node,
                      const std::shared_ptr<const InspectScope>& scope, const int useDepth) {
  for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
    stack.push_back(TraversalFrame{&(*it), scope, useDepth});
  }
}

void HandleSlotNode(ContractBuilder& builder, const OxmlNode& node, const std::shared_ptr<const InspectScope>& scope,
                    const int useDepth, std::vector<TraversalFrame>& stack) {
  const std::string slotName = node.GetAttr("name");
  builder.AddSlot(slotName);

  std::vector<const OxmlNode*> matched;
  for (const OxmlNode* candidate : scope->slotCandidates) {
    if (candidate != nullptr && candidate->GetAttr("slot") == slotName) {
      matched.push_back(candidate);
    }
  }
  if (!matched.empty()) {
    auto projScope = scope->callerScope ? std::make_shared<InspectScope>(*scope->callerScope)
                                        : std::make_shared<InspectScope>(*scope);
    projScope->slotCandidates.clear();
    for (auto it = matched.rbegin(); it != matched.rend(); ++it) {
      stack.push_back(TraversalFrame{*it, projScope, useDepth});
    }
    return;
  }
  PushNodeChildren(stack, node, scope, useDepth);
}

void HandleUseComponentNode(ContractBuilder& builder, const OxmlNode& node,
                            const std::shared_ptr<const InspectScope>& effScope, const int useDepth,
                            const UseMapEntry& useEntry, std::vector<TraversalFrame>& stack) {
  InspectCallSiteAttrs(builder, node, *effScope);
  if (useDepth >= kMaxUseNestingDepth || !useEntry.doc || useEntry.doc->roots.empty()) return;

  const std::string useKey = useEntry.src.empty() ? node.tag : useEntry.src;
  if (effScope->activeUseSources.contains(useKey)) return;

  auto tplScope = std::make_shared<InspectScope>();
  tplScope->inRootDoc = false;
  tplScope->activeUseSources = effScope->activeUseSources;
  tplScope->activeUseSources.insert(useKey);
  tplScope->callerScope = effScope;
  for (const auto& ch : node.children) {
    tplScope->slotCandidates.push_back(&ch);
  }
  for (const auto& p : useEntry.doc->params) {
    tplScope->ignoredVars.insert(p.name);
  }
  for (const auto& [attr, _] : node.attributes) {
    if (attr != "if" && attr != "visible" && attr != "disabled" && attr != "checked" && attr != "ref" &&
        attr != "slot" && attr != "each" && attr != "item-key") {
      tplScope->ignoredVars.insert(attr);
    }
  }
  static const std::unordered_map<std::string, OxmlDocument> kEmptyUses;
  tplScope->useMap = BuildUseMapForDoc(builder, *useEntry.doc, kEmptyUses);

  stack.push_back(TraversalFrame{&useEntry.doc->roots.front(), std::move(tplScope), useDepth + 1});
}

}  // namespace

const char* ContractFieldTypeName(const ContractFieldType type) {
  switch (type) {
    case ContractFieldType::Unknown:
      return "unknown";
    case ContractFieldType::String:
      return "string";
    case ContractFieldType::Boolean:
      return "bool";
    case ContractFieldType::Number:
      return "number";
    case ContractFieldType::Array:
      return "array";
    case ContractFieldType::Table:
      return "table";
  }
  return "string";
}

OxmlContract InspectOxmlContract(const OxmlDocument& doc,
                                 const std::unordered_map<std::string, OxmlDocument>& preloadedUses,
                                 const OxmlResolveUseFn& resolveUse) {
  ContractBuilder builder;
  builder.resolveUse = resolveUse;
  for (const auto& p : doc.params) {
    builder.out.params.push_back(ContractParam{p.name, p.defaultValue, p.line});
    builder.rootParams.insert(p.name);
  }

  auto rootScope = std::make_shared<InspectScope>();
  rootScope->inRootDoc = true;
  rootScope->useMap = BuildUseMapForDoc(builder, doc, preloadedUses);

  std::vector<TraversalFrame> stack;
  for (auto it = doc.roots.rbegin(); it != doc.roots.rend(); ++it) {
    stack.push_back(TraversalFrame{&(*it), rootScope, 0});
  }

  while (!stack.empty()) {
    const TraversalFrame frame = std::move(stack.back());
    stack.pop_back();
    if (frame.node == nullptr) continue;

    const OxmlNode& node = *frame.node;
    const auto effScope = ApplyEachScopeIfPresent(builder, node, frame.scope);

    if (const auto useIt = effScope->useMap.find(node.tag); useIt != effScope->useMap.end()) {
      HandleUseComponentNode(builder, node, effScope, frame.useDepth, useIt->second, stack);
      continue;
    }
    if (node.tag == "Slot") {
      HandleSlotNode(builder, node, effScope, frame.useDepth, stack);
      continue;
    }
    if (IsBuiltInOxmlTag(node.tag)) {
      InspectBuiltInNodeAttrs(builder, node, *effScope);
    } else {
      InspectCallSiteAttrs(builder, node, *effScope);
    }
    PushNodeChildren(stack, node, effScope, frame.useDepth);
  }

  return std::move(builder.out);
}
