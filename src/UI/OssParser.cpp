#include "UI/OssParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string_view Trim(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) {
    sv.remove_suffix(1);
  }
  return sv;
}

std::string ToLower(std::string_view sv) {
  std::string out(sv);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

bool ParseHexByte(char hi, char lo, uint8_t& out) {
  const int h = HexDigit(hi);
  const int l = HexDigit(lo);
  if (h < 0 || l < 0) return false;
  out = static_cast<uint8_t>((h << 4) | l);
  return true;
}

bool ParseShortHexColor(std::string_view hex, octarine::Color& out) {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
  if (!ParseHexByte(hex[0], hex[0], r) || !ParseHexByte(hex[1], hex[1], g) || !ParseHexByte(hex[2], hex[2], b)) {
    return false;
  }
  if (hex.size() == 4 && !ParseHexByte(hex[3], hex[3], a)) {
    return false;
  }
  out = octarine::Color{r, g, b, a};
  return true;
}

bool ParseFullHexColor(std::string_view hex, octarine::Color& out) {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
  if (!ParseHexByte(hex[0], hex[1], r) || !ParseHexByte(hex[2], hex[3], g) || !ParseHexByte(hex[4], hex[5], b)) {
    return false;
  }
  if (hex.size() == 8 && !ParseHexByte(hex[6], hex[7], a)) {
    return false;
  }
  out = octarine::Color{r, g, b, a};
  return true;
}

bool ParseHexColor(std::string_view hex, octarine::Color& out) {
  if (hex.empty() || hex.front() != '#') return false;
  hex.remove_prefix(1);
  if (hex.size() == 3 || hex.size() == 4) return ParseShortHexColor(hex, out);
  if (hex.size() == 6 || hex.size() == 8) return ParseFullHexColor(hex, out);
  return false;
}

std::vector<std::string_view> SplitByChar(std::string_view sv, char delim) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (start <= sv.size()) {
    const std::size_t pos = sv.find(delim, start);
    if (pos == std::string_view::npos) {
      parts.push_back(Trim(sv.substr(start)));
      break;
    }
    parts.push_back(Trim(sv.substr(start, pos - start)));
    start = pos + 1;
  }
  return parts;
}

bool ParseAlphaChannel(std::string_view part, uint8_t& outAlpha) {
  float af = 1.0f;
  if (!OssParser::ParseFloat(part, af)) {
    return false;
  }
  const bool normalized = af <= 1.0f && (part.find('.') != std::string_view::npos || af == 0.0f || af == 1.0f);
  const float scaled = normalized ? (af * 255.0f) : af;
  outAlpha = static_cast<uint8_t>(std::clamp(std::round(scaled), 0.0f, 255.0f));
  return true;
}

bool ParseRgbaColor(std::string_view lower, octarine::Color& out) {
  const bool isRgba = lower.starts_with("rgba(");
  const bool isRgb = lower.starts_with("rgb(");
  if ((!isRgba && !isRgb) || lower.back() != ')') {
    return false;
  }
  const std::size_t prefixLen = isRgba ? 5 : 4;
  const std::string_view inner = lower.substr(prefixLen, lower.size() - prefixLen - 1);
  const auto parts = SplitByChar(inner, ',');
  if (parts.size() != (isRgba ? 4u : 3u)) {
    return false;
  }
  float rf = 0.0f;
  float gf = 0.0f;
  float bf = 0.0f;
  if (!OssParser::ParseFloat(parts[0], rf) || !OssParser::ParseFloat(parts[1], gf) ||
      !OssParser::ParseFloat(parts[2], bf)) {
    return false;
  }
  uint8_t a = 255;
  if (isRgba && !ParseAlphaChannel(parts[3], a)) {
    return false;
  }
  out = octarine::Color{
      static_cast<uint8_t>(std::clamp(std::round(rf), 0.0f, 255.0f)),
      static_cast<uint8_t>(std::clamp(std::round(gf), 0.0f, 255.0f)),
      static_cast<uint8_t>(std::clamp(std::round(bf), 0.0f, 255.0f)),
      a,
  };
  return true;
}

bool ParseNamedColor(std::string_view lower, octarine::Color& out) {
  if (lower == "transparent") {
    out = octarine::Color{0, 0, 0, 0};
    return true;
  }
  if (lower == "white") {
    out = octarine::Color{255, 255, 255, 255};
    return true;
  }
  if (lower == "black") {
    out = octarine::Color{0, 0, 0, 255};
    return true;
  }
  if (lower == "red") {
    out = octarine::Color{239, 71, 111, 255};
    return true;
  }
  if (lower == "green") {
    out = octarine::Color{6, 214, 160, 255};
    return true;
  }
  if (lower == "blue") {
    out = octarine::Color{17, 138, 178, 255};
    return true;
  }
  if (lower == "yellow" || lower == "gold") {
    out = octarine::Color{255, 209, 102, 255};
    return true;
  }
  if (lower == "gray" || lower == "grey") {
    out = octarine::Color{140, 140, 150, 255};
    return true;
  }
  return false;
}

void SkipBlockComment(std::string_view src, std::size_t& i, std::string& out) {
  i += 2;
  while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) {
    if (src[i] == '\n') out.push_back('\n');
    ++i;
  }
  ++i;
}

void SkipLineComment(std::string_view src, std::size_t& i, std::string& out) {
  i += 2;
  while (i < src.size() && src[i] != '\n') {
    ++i;
  }
  if (i < src.size()) out.push_back('\n');
}

std::string StripComments(std::string_view src) {
  std::string out;
  out.reserve(src.size());
  for (std::size_t i = 0; i < src.size(); ++i) {
    if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '*') {
      SkipBlockComment(src, i, out);
      continue;
    }
    if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '/') {
      SkipLineComment(src, i, out);
      continue;
    }
    out.push_back(src[i]);
  }
  return out;
}

bool ParsePseudoSuffix(std::string_view suffix, OssPseudoState& outPseudo) {
  if (suffix == "hover") {
    outPseudo = OssPseudoState::Hover;
    return true;
  }
  if (suffix == "press" || suffix == "active") {
    outPseudo = OssPseudoState::Press;
    return true;
  }
  if (suffix == "focus") {
    outPseudo = OssPseudoState::Focus;
    return true;
  }
  if (suffix == "checked") {
    outPseudo = OssPseudoState::Checked;
    return true;
  }
  if (suffix == "disabled") {
    outPseudo = OssPseudoState::Disabled;
    return true;
  }
  return false;
}

bool ParseSingleSelector(std::string_view raw, OssSelector& outSelector) {
  std::string_view s = Trim(raw);
  if (s.empty()) return false;

  const std::size_t colonPos = s.find(':');
  if (colonPos != std::string_view::npos) {
    const std::string_view pseudoPart = Trim(s.substr(colonPos + 1));
    if (!ParsePseudoSuffix(pseudoPart, outSelector.pseudo)) {
      return false;
    }
    s = Trim(s.substr(0, colonPos));
  }

  const std::size_t hashPos = s.find('#');
  if (hashPos != std::string_view::npos) {
    outSelector.id = std::string(Trim(s.substr(hashPos + 1)));
    s = Trim(s.substr(0, hashPos));
  }

  const std::size_t dotPos = s.find('.');
  if (dotPos != std::string_view::npos) {
    outSelector.className = std::string(Trim(s.substr(dotPos + 1)));
    s = Trim(s.substr(0, dotPos));
  }

  if (!s.empty() && s != "*") {
    outSelector.tag = std::string(s);
  }
  return !outSelector.tag.empty() || !outSelector.className.empty() || !outSelector.id.empty();
}

std::map<std::string, std::string> ParseDeclarationsBlock(std::string_view block) {
  std::map<std::string, std::string> props;
  const auto decls = SplitByChar(block, ';');
  for (const std::string_view decl : decls) {
    if (decl.empty()) continue;
    const std::size_t colon = decl.find(':');
    if (colon == std::string_view::npos) continue;
    const std::string key(Trim(decl.substr(0, colon)));
    const std::string val(Trim(decl.substr(colon + 1)));
    if (!key.empty() && !val.empty()) {
      props[key] = val;
    }
  }
  return props;
}

UIStylePatch BuildStylePatch(const std::map<std::string, std::string>& props) {
  UIStylePatch patch;
  for (const auto& [k, v] : props) {
    if (k == "background-color" || k == "bg-color" || k == "background") {
      octarine::Color c{};
      if (OssParser::ParseColor(v, c)) {
        patch.SetBg(c);
      }
    } else if (k == "border-color") {
      octarine::Color c{};
      if (OssParser::ParseColor(v, c)) {
        patch.SetBorder(c);
      }
    } else if (k == "color" || k == "text-color") {
      octarine::Color c{};
      if (OssParser::ParseColor(v, c)) {
        patch.SetText(c);
      }
    } else if (k == "opacity") {
      float op = 1.0f;
      if (OssParser::ParseFloat(v, op)) {
        const float scaled = (op <= 1.0f) ? op * 255.0f : op;
        patch.SetOpacity(static_cast<uint8_t>(std::clamp(std::round(scaled), 0.0f, 255.0f)));
      }
    }
  }
  return patch;
}

std::vector<float> ParseNumberList(std::string_view str) {
  std::vector<float> vals;
  std::size_t i = 0;
  while (i < str.size()) {
    while (i < str.size() && (std::isspace(static_cast<unsigned char>(str[i])) != 0 || str[i] == ',')) {
      ++i;
    }
    if (i >= str.size()) break;
    const std::size_t start = i;
    while (i < str.size() && std::isspace(static_cast<unsigned char>(str[i])) == 0 && str[i] != ',') {
      ++i;
    }
    float v = 0.0f;
    if (OssParser::ParseFloat(str.substr(start, i - start), v)) {
      vals.push_back(v);
    }
  }
  return vals;
}

}  // namespace

bool OssSelector::MatchesElement(std::string_view elemTag, const std::vector<std::string>& elemClasses,
                                 std::string_view elemId) const {
  if (!tag.empty() && tag != elemTag) {
    return false;
  }
  if (!id.empty() && id != elemId) {
    return false;
  }
  if (!className.empty()) {
    if (std::find(elemClasses.begin(), elemClasses.end(), className) == elemClasses.end()) {
      return false;
    }
  }
  return true;
}

UIStyleStateComponent OssResolvedStyle::BuildStyleStateComponent() const {
  UIStyleStateComponent comp;
  comp.hover = BuildStylePatch(hover);
  comp.press = BuildStylePatch(press);
  comp.focus = BuildStylePatch(focus);
  comp.checked = BuildStylePatch(checked);
  comp.disabled = BuildStylePatch(disabled);
  return comp;
}

void OssStylesheet::MergeFrom(const OssStylesheet& other) {
  for (const auto& [k, v] : other.variables) {
    variables[k] = v;
  }
  const int baseOrder = static_cast<int>(rules.size());
  for (std::size_t i = 0; i < other.rules.size(); ++i) {
    OssRule r = other.rules[i];
    r.order = baseOrder + static_cast<int>(i);
    rules.push_back(std::move(r));
  }
}

std::string OssStylesheet::ResolveVariables(std::string_view rawValue) const {
  std::string out(rawValue);
  for (int depth = 0; depth < 8; ++depth) {
    const std::size_t pos = out.find("var(");
    if (pos == std::string::npos) break;
    const std::size_t end = out.find(')', pos + 4);
    if (end == std::string::npos) break;

    const std::string_view inner = Trim(std::string_view(out).substr(pos + 4, end - (pos + 4)));
    std::string varName(inner);
    std::string fallback;
    if (const std::size_t comma = inner.find(','); comma != std::string_view::npos) {
      varName = std::string(Trim(inner.substr(0, comma)));
      fallback = std::string(Trim(inner.substr(comma + 1)));
    }
    const auto it = variables.find(varName);
    const std::string replacement = (it != variables.end()) ? it->second : fallback;
    out.replace(pos, end - pos + 1, replacement);
  }
  return out;
}

OssResolvedStyle OssStylesheet::Resolve(std::string_view tag, const std::vector<std::string>& classes,
                                        std::string_view id,
                                        const std::map<std::string, std::string>& inlineProps) const {
  struct MatchedRule {
    int specificity = 0;
    int order = 0;
    const OssRule* rule = nullptr;
  };

  std::vector<MatchedRule> matched;
  for (const auto& rule : rules) {
    if (rule.selector.MatchesElement(tag, classes, id)) {
      matched.push_back(MatchedRule{rule.selector.Specificity(), rule.order, &rule});
    }
  }

  std::sort(matched.begin(), matched.end(), [](const MatchedRule& a, const MatchedRule& b) {
    if (a.specificity != b.specificity) return a.specificity < b.specificity;
    return a.order < b.order;
  });

  OssResolvedStyle resolved;
  for (const auto& m : matched) {
    std::map<std::string, std::string>* target = &resolved.base;
    switch (m.rule->selector.pseudo) {
      case OssPseudoState::None:
        target = &resolved.base;
        break;
      case OssPseudoState::Hover:
        target = &resolved.hover;
        break;
      case OssPseudoState::Press:
        target = &resolved.press;
        break;
      case OssPseudoState::Focus:
        target = &resolved.focus;
        break;
      case OssPseudoState::Checked:
        target = &resolved.checked;
        break;
      case OssPseudoState::Disabled:
        target = &resolved.disabled;
        break;
    }
    for (const auto& [k, v] : m.rule->properties) {
      (*target)[k] = ResolveVariables(v);
    }
  }

  for (const auto& [k, v] : inlineProps) {
    resolved.base[k] = ResolveVariables(v);
  }
  return resolved;
}

OssParseResult OssParser::Parse(std::string_view source) {
  OssParseResult res;
  const std::string cleaned = StripComments(source);
  const std::string_view sv(cleaned);

  std::size_t pos = 0;
  int ruleOrder = 0;

  while (pos < sv.size()) {
    while (pos < sv.size() && std::isspace(static_cast<unsigned char>(sv[pos])) != 0) {
      ++pos;
    }
    if (pos >= sv.size()) break;

    const std::size_t openBrace = sv.find('{', pos);
    if (openBrace == std::string_view::npos) {
      res.error = "Expected '{' in stylesheet rule";
      res.errorLine = 1;
      return res;
    }
    const std::size_t closeBrace = sv.find('}', openBrace + 1);
    if (closeBrace == std::string_view::npos) {
      res.error = "Unclosed '{' in stylesheet rule";
      res.errorLine = 1;
      return res;
    }

    const std::string_view selectorGroup = Trim(sv.substr(pos, openBrace - pos));
    const std::string_view body = sv.substr(openBrace + 1, closeBrace - openBrace - 1);
    auto props = ParseDeclarationsBlock(body);

    if (selectorGroup == ":root") {
      for (const auto& [k, v] : props) {
        res.stylesheet.variables[k] = v;
      }
    } else {
      const auto selectors = SplitByChar(selectorGroup, ',');
      for (const std::string_view selStr : selectors) {
        OssSelector sel;
        if (!ParseSingleSelector(selStr, sel)) {
          res.error = "Invalid selector '" + std::string(selStr) + "'";
          res.errorLine = 1;
          return res;
        }
        res.stylesheet.rules.push_back(OssRule{std::move(sel), props, ruleOrder++});
      }
    }

    pos = closeBrace + 1;
  }

  res.ok = true;
  return res;
}

std::vector<std::string> OssParser::SplitClassList(std::string_view classAttr) {
  std::vector<std::string> classes;
  std::size_t i = 0;
  while (i < classAttr.size()) {
    while (i < classAttr.size() && std::isspace(static_cast<unsigned char>(classAttr[i])) != 0) {
      ++i;
    }
    if (i >= classAttr.size()) break;
    const std::size_t start = i;
    while (i < classAttr.size() && std::isspace(static_cast<unsigned char>(classAttr[i])) == 0) {
      ++i;
    }
    classes.emplace_back(classAttr.substr(start, i - start));
  }
  return classes;
}

std::map<std::string, std::string> OssParser::ParseInlineStyle(std::string_view styleAttr) {
  return ParseDeclarationsBlock(styleAttr);
}

bool OssParser::ParseColor(std::string_view str, octarine::Color& outColor) {
  const std::string_view trimmed = Trim(str);
  if (trimmed.empty()) return false;
  if (trimmed.front() == '#') {
    return ParseHexColor(trimmed, outColor);
  }
  const std::string lower = ToLower(trimmed);
  if (lower.starts_with("rgb")) {
    return ParseRgbaColor(lower, outColor);
  }
  return ParseNamedColor(lower, outColor);
}

bool OssParser::ParseFloat(std::string_view str, float& outVal) {
  std::string_view s = Trim(str);
  if (s.ends_with("px")) {
    s.remove_suffix(2);
    s = Trim(s);
  }
  if (s.empty()) return false;
  const std::string buf(s);
  char* endPtr = nullptr;
  const float val = std::strtof(buf.c_str(), &endPtr);
  if (endPtr == buf.c_str() || *endPtr != '\0') {
    return false;
  }
  outVal = val;
  return true;
}

bool OssParser::ParseDimension(std::string_view str, float& outValue, UIUnit& outUnit) {
  const std::string lower = ToLower(Trim(str));
  if (lower.empty()) return false;
  if (lower == "auto") {
    outValue = 0.0f;
    outUnit = UIUnit::Auto;
    return true;
  }
  if (lower.ends_with("%")) {
    float v = 0.0f;
    if (!ParseFloat(std::string_view(lower).substr(0, lower.size() - 1), v)) return false;
    outValue = v;
    outUnit = UIUnit::Percent;
    return true;
  }
  if (lower.ends_with("fr")) {
    float v = 0.0f;
    if (!ParseFloat(std::string_view(lower).substr(0, lower.size() - 2), v)) return false;
    outValue = v;
    outUnit = UIUnit::Fr;
    return true;
  }
  float px = 0.0f;
  if (!ParseFloat(lower, px)) return false;
  outValue = px;
  outUnit = UIUnit::Px;
  return true;
}

bool OssParser::ParseEdges(std::string_view str, float& top, float& right, float& bottom, float& left) {
  const auto vals = ParseNumberList(str);
  if (vals.empty() || vals.size() > 4) return false;
  if (vals.size() == 1) {
    top = right = bottom = left = vals[0];
  } else if (vals.size() == 2) {
    top = bottom = vals[0];
    right = left = vals[1];
  } else if (vals.size() == 3) {
    top = vals[0];
    right = left = vals[1];
    bottom = vals[2];
  } else {
    top = vals[0];
    right = vals[1];
    bottom = vals[2];
    left = vals[3];
  }
  return true;
}

bool OssParser::ParseSlice4(std::string_view str, float& left, float& top, float& right, float& bottom) {
  const auto vals = ParseNumberList(str);
  if (vals.empty() || vals.size() > 4) return false;
  if (vals.size() == 1) {
    left = top = right = bottom = vals[0];
    return true;
  }
  if (vals.size() == 4) {
    left = vals[0];
    top = vals[1];
    right = vals[2];
    bottom = vals[3];
    return true;
  }
  return false;
}
