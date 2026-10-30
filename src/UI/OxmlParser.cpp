#include "UI/OxmlParser.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace {

std::string_view TrimView(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) {
    sv.remove_suffix(1);
  }
  return sv;
}

bool IsIdentChar(char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) != 0 || c == '-' || c == '_' || c == ':' || c == '.';
}

std::string DecodeXmlEntities(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] != '&') {
      out.push_back(raw[i]);
      continue;
    }
    const std::string_view rest = raw.substr(i);
    if (rest.starts_with("&amp;")) {
      out.push_back('&');
      i += 4;
    } else if (rest.starts_with("&lt;")) {
      out.push_back('<');
      i += 3;
    } else if (rest.starts_with("&gt;")) {
      out.push_back('>');
      i += 3;
    } else if (rest.starts_with("&quot;")) {
      out.push_back('"');
      i += 5;
    } else if (rest.starts_with("&apos;")) {
      out.push_back('\'');
      i += 5;
    } else {
      out.push_back(raw[i]);
    }
  }
  return out;
}

struct XmlCursor {
  std::string_view src{};
  std::size_t pos = 0;
  int line = 1;

  [[nodiscard]] bool Eof() const { return pos >= src.size(); }
  [[nodiscard]] char Peek() const { return pos < src.size() ? src[pos] : '\0'; }

  [[nodiscard]] bool StartsWith(std::string_view prefix) const { return src.substr(pos).starts_with(prefix); }

  void Advance(std::size_t count = 1) {
    for (std::size_t i = 0; i < count && pos < src.size(); ++i) {
      if (src[pos] == '\n') {
        ++line;
      }
      ++pos;
    }
  }

  void SkipWhitespace() {
    while (!Eof() && std::isspace(static_cast<unsigned char>(Peek())) != 0) {
      Advance();
    }
  }

  bool SkipCommentOrProlog(std::string& outError, int& outErrorLine) {
    if (StartsWith("<!--")) {
      const int startLine = line;
      Advance(4);
      while (!Eof() && !StartsWith("-->")) {
        Advance();
      }
      if (Eof()) {
        outError = "Unterminated XML comment";
        outErrorLine = startLine;
        return false;
      }
      Advance(3);
      return true;
    }
    if (StartsWith("<?")) {
      const int startLine = line;
      Advance(2);
      while (!Eof() && !StartsWith("?>")) {
        Advance();
      }
      if (Eof()) {
        outError = "Unterminated XML processing instruction";
        outErrorLine = startLine;
        return false;
      }
      Advance(2);
      return true;
    }
    return true;
  }

  bool SkipTrivia(std::string& outError, int& outErrorLine) {
    while (!Eof()) {
      SkipWhitespace();
      if (StartsWith("<!--") || StartsWith("<?")) {
        if (!SkipCommentOrProlog(outError, outErrorLine)) {
          return false;
        }
        continue;
      }
      break;
    }
    return true;
  }

  std::string ReadIdent() {
    const std::size_t start = pos;
    while (!Eof() && IsIdentChar(Peek())) {
      Advance();
    }
    return std::string(src.substr(start, pos - start));
  }
};

bool ParseAttributeValue(XmlCursor& cur, std::string& outVal, std::string& outError, int& outErrorLine) {
  const char quote = cur.Peek();
  if (quote != '"' && quote != '\'') {
    outError = "Expected quoted attribute value";
    outErrorLine = cur.line;
    return false;
  }
  const int startLine = cur.line;
  cur.Advance();
  const std::size_t start = cur.pos;
  while (!cur.Eof() && cur.Peek() != quote) {
    cur.Advance();
  }
  if (cur.Eof()) {
    outError = "Unterminated attribute string literal";
    outErrorLine = startLine;
    return false;
  }
  outVal = DecodeXmlEntities(cur.src.substr(start, cur.pos - start));
  cur.Advance();
  return true;
}

bool ParseAttributes(XmlCursor& cur, std::map<std::string, std::string>& attrs, bool& selfClosing,
                     std::string& outError, int& outErrorLine) {
  selfClosing = false;
  while (!cur.Eof()) {
    cur.SkipWhitespace();
    if (cur.StartsWith("/>")) {
      selfClosing = true;
      cur.Advance(2);
      return true;
    }
    if (cur.Peek() == '>') {
      cur.Advance(1);
      return true;
    }
    const std::string key = cur.ReadIdent();
    if (key.empty()) {
      outError = std::string("Unexpected character '") + cur.Peek() + "' in tag attributes";
      outErrorLine = cur.line;
      return false;
    }
    cur.SkipWhitespace();
    if (cur.Peek() != '=') {
      attrs[key] = "true";
      continue;
    }
    cur.Advance(1);
    cur.SkipWhitespace();
    std::string val;
    if (!ParseAttributeValue(cur, val, outError, outErrorLine)) {
      return false;
    }
    attrs[key] = std::move(val);
  }
  outError = "Unexpected end of file inside opening tag";
  outErrorLine = cur.line;
  return false;
}

void AppendInnerText(OxmlNode& node, std::string_view rawSegment) {
  const std::string_view trimmed = TrimView(rawSegment);
  if (trimmed.empty()) {
    return;
  }
  const std::string decoded = DecodeXmlEntities(trimmed);
  if (!node.innerText.empty()) {
    node.innerText.push_back(' ');
  }
  node.innerText += decoded;
}

// Forward declaration for recursive element tree parsing.
bool ParseElement(XmlCursor& cur, OxmlNode& outNode, std::string& outError, int& outErrorLine);

bool ConsumeClosingTag(XmlCursor& cur, OxmlNode& node, std::string& outError, int& outErrorLine) {
  cur.Advance(2);
  cur.SkipWhitespace();
  const std::string closeTag = cur.ReadIdent();
  cur.SkipWhitespace();
  if (closeTag != node.tag || cur.Peek() != '>') {
    outError = "Mismatched closing tag </" + closeTag + "> for <" + node.tag + ">";
    outErrorLine = cur.line;
    return false;
  }
  cur.Advance(1);
  if (!node.innerText.empty() && !node.HasAttr("text")) {
    node.attributes["text"] = node.innerText;
  }
  return true;
}

void ConsumeTextUntilTag(XmlCursor& cur, OxmlNode& node) {
  const std::size_t textStart = cur.pos;
  while (!cur.Eof() && cur.Peek() != '<') {
    cur.Advance();
  }
  AppendInnerText(node, cur.src.substr(textStart, cur.pos - textStart));
}

// NOLINTNEXTLINE(misc-no-recursion)
bool ParseChildrenAndClosingTag(XmlCursor& cur, OxmlNode& node, std::string& outError, int& outErrorLine) {
  while (!cur.Eof()) {
    ConsumeTextUntilTag(cur, node);
    if (cur.Eof()) break;
    if (cur.StartsWith("<!--") || cur.StartsWith("<?")) {
      if (!cur.SkipCommentOrProlog(outError, outErrorLine)) return false;
      continue;
    }
    if (cur.StartsWith("</")) {
      return ConsumeClosingTag(cur, node, outError, outErrorLine);
    }
    OxmlNode child;
    if (!ParseElement(cur, child, outError, outErrorLine)) return false;
    node.children.push_back(std::move(child));
  }
  outError = "Unclosed tag <" + node.tag + ">";
  outErrorLine = node.line;
  return false;
}

// NOLINTNEXTLINE(misc-no-recursion)
bool ParseElement(XmlCursor& cur, OxmlNode& outNode, std::string& outError, int& outErrorLine) {
  if (cur.Peek() != '<') {
    outError = "Expected '<' to start element";
    outErrorLine = cur.line;
    return false;
  }
  outNode.line = cur.line;
  cur.Advance(1);
  cur.SkipWhitespace();
  outNode.tag = cur.ReadIdent();
  if (outNode.tag.empty()) {
    outError = "Expected element tag name after '<'";
    outErrorLine = outNode.line;
    return false;
  }

  bool selfClosing = false;
  if (!ParseAttributes(cur, outNode.attributes, selfClosing, outError, outErrorLine)) {
    return false;
  }
  if (selfClosing) {
    return true;
  }
  return ParseChildrenAndClosingTag(cur, outNode, outError, outErrorLine);
}

bool ExtractHeaderNode(const OxmlNode& node, OxmlDocument& doc, std::string& outError, int& outErrorLine) {
  if (node.tag == "Style") {
    const std::string src = node.GetAttr("src");
    if (src.empty()) {
      outError = "<Style> requires a 'src' attribute";
      outErrorLine = node.line;
      return false;
    }
    doc.styles.push_back(OxmlStyleImport{src, node.line});
    return true;
  }
  if (node.tag == "Use") {
    const std::string name = node.GetAttr("name");
    const std::string src = node.GetAttr("src");
    if (name.empty() || src.empty()) {
      outError = "<Use> requires both 'name' and 'src' attributes";
      outErrorLine = node.line;
      return false;
    }
    doc.uses.push_back(OxmlUseImport{name, src, node.line});
    return true;
  }
  if (node.tag == "Param") {
    const std::string name = node.GetAttr("name");
    if (name.empty()) {
      outError = "<Param> requires a 'name' attribute";
      outErrorLine = node.line;
      return false;
    }
    doc.params.push_back(OxmlParamDecl{name, node.GetAttr("default"), node.line});
    return true;
  }
  doc.roots.push_back(node);
  return true;
}

void AddUniqueRef(std::vector<std::string>& vec, std::string_view rawVal) {
  std::string_view v = TrimView(rawVal);
  if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
    v = TrimView(v.substr(1, v.size() - 2));
  }
  if (v.empty() || OxmlParser::ContainsBinding(v)) {
    return;
  }
  const std::string value(v);
  if (std::find(vec.begin(), vec.end(), value) == vec.end()) {
    vec.push_back(value);
  }
}

// NOLINTNEXTLINE(misc-no-recursion)
void WalkStaticNodeRefs(const OxmlNode& node, OxmlAssetRefs& refs) {
  AddUniqueRef(refs.spriteIds, node.GetAttr("sprite"));
  AddUniqueRef(refs.spriteIds, node.GetAttr("icon"));
  AddUniqueRef(refs.fontIds, node.GetAttr("font"));
  AddUniqueRef(refs.audioIds, node.GetAttr("sound-hover"));
  AddUniqueRef(refs.audioIds, node.GetAttr("sound-click"));
  AddUniqueRef(refs.audioIds, node.GetAttr("sound-press"));
  AddUniqueRef(refs.audioIds, node.GetAttr("sound-focus"));
  for (const auto& child : node.children) {
    WalkStaticNodeRefs(child, refs);
  }
}

}  // namespace

OxmlParseResult OxmlParser::Parse(std::string_view source) {
  OxmlParseResult res;
  XmlCursor cur{source, 0, 1};

  while (!cur.Eof()) {
    if (!cur.SkipTrivia(res.error, res.errorLine)) {
      return res;
    }
    if (cur.Eof()) {
      break;
    }
    OxmlNode topNode;
    if (!ParseElement(cur, topNode, res.error, res.errorLine)) {
      return res;
    }
    if (topNode.tag == "OXML") {
      for (const auto& child : topNode.children) {
        if (!ExtractHeaderNode(child, res.document, res.error, res.errorLine)) {
          return res;
        }
      }
    } else {
      if (!ExtractHeaderNode(topNode, res.document, res.error, res.errorLine)) {
        return res;
      }
    }
  }

  if (res.document.roots.empty() && res.document.styles.empty() && res.document.uses.empty()) {
    res.error = "OXML document contains no root elements";
    res.errorLine = 1;
    return res;
  }

  res.ok = true;
  return res;
}

bool OxmlParser::ContainsBinding(std::string_view value) {
  const std::size_t openPos = value.find('{');
  if (openPos == std::string_view::npos) {
    return false;
  }
  const std::size_t closePos = value.find('}', openPos + 1);
  return closePos != std::string_view::npos;
}

bool OxmlParser::IsPureBinding(std::string_view value, std::string& outExpr) {
  const std::string_view trimmed = TrimView(value);
  if (trimmed.size() < 3 || trimmed.front() != '{' || trimmed.back() != '}') {
    return false;
  }
  const std::string_view inner = trimmed.substr(1, trimmed.size() - 2);
  if (inner.find('{') != std::string_view::npos || inner.find('}') != std::string_view::npos) {
    return false;
  }
  outExpr = std::string(TrimView(inner));
  return !outExpr.empty();
}

OxmlEachClause OxmlParser::ParseEachClause(std::string_view attr) {
  OxmlEachClause clause;
  const std::string_view trimmed = TrimView(attr);
  const std::size_t inPos = trimmed.find(" in ");
  if (inPos == std::string_view::npos) {
    return clause;
  }

  const std::string_view lhs = TrimView(trimmed.substr(0, inPos));
  const std::string_view rhs = TrimView(trimmed.substr(inPos + 4));
  if (lhs.empty() || rhs.empty()) {
    return clause;
  }

  const std::size_t commaPos = lhs.find(',');
  clause.itemVar = std::string(TrimView(lhs.substr(0, commaPos)));
  if (commaPos != std::string_view::npos) {
    clause.indexVar = std::string(TrimView(lhs.substr(commaPos + 1)));
  }

  if (!IsPureBinding(rhs, clause.listExpr)) {
    clause.listExpr = std::string(rhs);
  }

  clause.valid = !clause.itemVar.empty() && !clause.listExpr.empty();
  return clause;
}

OxmlAssetRefs OxmlParser::CollectStaticAssetRefs(const OxmlDocument& doc) {
  OxmlAssetRefs refs;
  for (const auto& s : doc.styles) {
    AddUniqueRef(refs.styleSources, s.src);
  }
  for (const auto& u : doc.uses) {
    AddUniqueRef(refs.useSources, u.src);
  }
  for (const auto& root : doc.roots) {
    WalkStaticNodeRefs(root, refs);
  }
  return refs;
}
