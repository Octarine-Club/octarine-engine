#include "UI/UIPreviewerMockSource.h"

#ifndef OCTARINE_SHIPPED

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace octarine::ui_preview_detail {
namespace {

struct TableEntrySpan {
  std::string key{};
  std::size_t valStart{0};
  std::size_t valEnd{0};
  std::size_t entryEnd{0};
};

struct TextInsertEdit {
  std::size_t pos{0};
  std::string text{};
};

bool IsWordStartChar(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalpha(uc) != 0 || c == '_';
}

bool IsWordChar(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) != 0 || c == '_';
}

std::string_view TrimAscii(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) sv.remove_prefix(1);
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) sv.remove_suffix(1);
  return sv;
}

std::string_view TrimTrailingAscii(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) sv.remove_suffix(1);
  return sv;
}

bool SkipLongBracket(std::string_view src, std::size_t& pos) {
  if (pos >= src.size() || src[pos] != '[') return false;
  std::size_t eqCount = 0;
  std::size_t p = pos + 1;
  while (p < src.size() && src[p] == '=') {
    ++eqCount;
    ++p;
  }
  if (p >= src.size() || src[p] != '[') return false;
  ++p;

  const std::string closeDelim = "]" + std::string(eqCount, '=') + "]";
  const std::size_t found = src.find(closeDelim, p);
  pos = (found == std::string_view::npos) ? src.size() : (found + closeDelim.size());
  return true;
}

void SkipQuotedString(std::string_view src, std::size_t& pos) {
  const char quote = src[pos];
  ++pos;
  while (pos < src.size()) {
    if (src[pos] == '\\') {
      pos += (pos + 1 < src.size()) ? 2 : 1;
      continue;
    }
    if (src[pos] == quote) {
      ++pos;
      return;
    }
    ++pos;
  }
}

void SkipLuaTrivia(std::string_view src, std::size_t& pos) {
  while (pos < src.size()) {
    if (std::isspace(static_cast<unsigned char>(src[pos])) != 0) {
      ++pos;
      continue;
    }
    if (pos + 1 < src.size() && src[pos] == '-' && src[pos + 1] == '-') {
      pos += 2;
      if (!SkipLongBracket(src, pos)) {
        const std::size_t nl = src.find('\n', pos);
        pos = (nl == std::string_view::npos) ? src.size() : nl + 1;
      }
      continue;
    }
    break;
  }
}

void UpdateBlockStack(std::string_view word, std::vector<char>& blockStack) {
  if (word == "function" || word == "do" || word == "if") {
    blockStack.push_back('E');
  } else if (word == "repeat") {
    blockStack.push_back('R');
  } else if (word == "end") {
    if (!blockStack.empty() && blockStack.back() == 'E') blockStack.pop_back();
  } else if (word == "until") {
    if (!blockStack.empty() && blockStack.back() == 'R') blockStack.pop_back();
  }
}

// Advances one Lua token (comment, whitespace, string, long bracket, word, or punctuation).
// Updates braceDepth and blockStack while tracking the previous non-trivia token character.
void AdvanceLuaToken(std::string_view src, std::size_t& pos, int& braceDepth, std::vector<char>& blockStack,
                     char& prevSigChar) {
  const char ch = src[pos];
  if (ch == '\'' || ch == '"') {
    SkipQuotedString(src, pos);
    prevSigChar = ch;
    return;
  }
  if (ch == '[' && SkipLongBracket(src, pos)) {
    prevSigChar = ']';
    return;
  }
  if (IsWordStartChar(ch)) {
    const std::size_t start = pos;
    while (pos < src.size() && IsWordChar(src[pos])) ++pos;
    if (prevSigChar != '.' && prevSigChar != ':') {
      UpdateBlockStack(src.substr(start, pos - start), blockStack);
    }
    prevSigChar = 'A';
    return;
  }
  if (ch == '{' || ch == '(' || ch == '[') {
    ++braceDepth;
  } else if ((ch == '}' || ch == ')' || ch == ']') && braceDepth > 0) {
    --braceDepth;
  }
  prevSigChar = ch;
  ++pos;
}

bool FindTopLevelReturn(std::string_view src, std::size_t& outReturnPos, std::size_t& outExprStart) {
  std::size_t pos = 0;
  int braceDepth = 0;
  std::vector<char> blockStack;
  char prevSigChar = '\0';

  while (pos < src.size()) {
    SkipLuaTrivia(src, pos);
    if (pos >= src.size()) break;
    if (braceDepth == 0 && blockStack.empty() && prevSigChar != '.' && prevSigChar != ':' &&
        IsWordStartChar(src[pos])) {
      const std::size_t start = pos;
      std::size_t end = pos;
      while (end < src.size() && IsWordChar(src[end])) ++end;
      if (src.substr(start, end - start) == "return") {
        pos = end;
        SkipLuaTrivia(src, pos);
        outReturnPos = start;
        outExprStart = pos;
        return true;
      }
    }
    AdvanceLuaToken(src, pos, braceDepth, blockStack, prevSigChar);
  }
  return false;
}

std::size_t FindMatchingBraceEnd(std::string_view src, const std::size_t openBracePos) {
  if (openBracePos >= src.size() || src[openBracePos] != '{') return std::string_view::npos;
  std::size_t pos = openBracePos + 1;
  int braceDepth = 1;
  std::vector<char> blockStack;
  char prevSigChar = '{';

  while (pos < src.size()) {
    SkipLuaTrivia(src, pos);
    if (pos >= src.size()) break;
    if (src[pos] == '}' && braceDepth == 1 && blockStack.empty()) {
      return pos;
    }
    AdvanceLuaToken(src, pos, braceDepth, blockStack, prevSigChar);
  }
  return std::string_view::npos;
}

bool ReadQuotedStringLiteral(std::string_view src, std::size_t& cur, std::string& out) {
  if (cur >= src.size() || (src[cur] != '"' && src[cur] != '\'')) return false;
  const char quote = src[cur++];
  out.clear();
  while (cur < src.size() && src[cur] != quote) {
    if (src[cur] == '\\' && cur + 1 < src.size()) {
      ++cur;
    }
    out.push_back(src[cur++]);
  }
  if (cur >= src.size() || src[cur] != quote) return false;
  ++cur;
  return true;
}

bool ParseBracketStringKey(std::string_view src, std::size_t& pos, std::string& outKey) {
  if (pos >= src.size() || src[pos] != '[') return false;
  std::size_t cur = pos + 1;
  SkipLuaTrivia(src, cur);
  std::string decoded;
  if (!ReadQuotedStringLiteral(src, cur, decoded)) return false;
  SkipLuaTrivia(src, cur);
  if (cur >= src.size() || src[cur] != ']') return false;
  ++cur;
  SkipLuaTrivia(src, cur);
  if (cur >= src.size() || src[cur] != '=') return false;
  pos = cur + 1;
  outKey = std::move(decoded);
  return true;
}

bool ParseTableEntryKey(std::string_view src, std::size_t& pos, std::string& outKey) {
  SkipLuaTrivia(src, pos);
  if (pos >= src.size()) return false;
  if (src[pos] == '[') return ParseBracketStringKey(src, pos, outKey);
  if (!IsWordStartChar(src[pos])) return false;

  std::size_t cur = pos;
  while (cur < src.size() && IsWordChar(src[cur])) ++cur;
  const std::string_view ident = src.substr(pos, cur - pos);
  SkipLuaTrivia(src, cur);
  if (cur >= src.size() || src[cur] != '=' || (cur + 1 < src.size() && src[cur + 1] == '=')) {
    return false;
  }
  pos = cur + 1;
  outKey = std::string(ident);
  return true;
}

void AdvancePastEntryValue(std::string_view src, std::size_t& pos, const std::size_t closeBracePos) {
  int braceDepth = 0;
  std::vector<char> blockStack;
  char prevSigChar = '\0';

  while (pos < closeBracePos) {
    SkipLuaTrivia(src, pos);
    if (pos >= closeBracePos) break;
    if (braceDepth == 0 && blockStack.empty() && (src[pos] == ',' || src[pos] == ';')) {
      return;
    }
    AdvanceLuaToken(src, pos, braceDepth, blockStack, prevSigChar);
  }
}

std::vector<TableEntrySpan> ScanTableEntries(std::string_view src, const std::size_t openBracePos,
                                             const std::size_t closeBracePos) {
  std::vector<TableEntrySpan> entries;
  std::size_t pos = openBracePos + 1;
  while (pos < closeBracePos) {
    SkipLuaTrivia(src, pos);
    if (pos >= closeBracePos) break;
    if (src[pos] == ',' || src[pos] == ';') {
      ++pos;
      continue;
    }

    std::string key;
    const bool hasKey = ParseTableEntryKey(src, pos, key);
    SkipLuaTrivia(src, pos);
    const std::size_t valStart = pos;
    AdvancePastEntryValue(src, pos, closeBracePos);
    const std::size_t valEnd = valStart + TrimTrailingAscii(src.substr(valStart, pos - valStart)).size();
    if (hasKey) {
      entries.push_back(TableEntrySpan{std::move(key), valStart, valEnd, pos});
    }
    if (pos < closeBracePos && (src[pos] == ',' || src[pos] == ';')) {
      ++pos;
    }
  }
  return entries;
}

void ExtractPresetControllers(std::string_view src, const std::size_t presetsOpenBrace,
                              std::unordered_map<std::string, std::string>& outPresets) {
  const std::size_t presetsClose = FindMatchingBraceEnd(src, presetsOpenBrace);
  if (presetsClose == std::string_view::npos) return;

  for (const auto& presetEntry : ScanTableEntries(src, presetsOpenBrace, presetsClose)) {
    if (presetEntry.valStart >= presetEntry.valEnd || src[presetEntry.valStart] != '{') continue;
    const std::size_t pClose = FindMatchingBraceEnd(src, presetEntry.valStart);
    if (pClose == std::string_view::npos) continue;
    for (const auto& subEntry : ScanTableEntries(src, presetEntry.valStart, pClose)) {
      if (subEntry.key == "controller" && subEntry.valEnd > subEntry.valStart) {
        outPresets[presetEntry.key] =
            std::string(TrimAscii(src.substr(subEntry.valStart, subEntry.valEnd - subEntry.valStart)));
        break;
      }
    }
  }
}

TextInsertEdit BuildControllerInsertEdit(std::string_view src, const std::size_t openBracePos,
                                         const std::size_t closeBracePos, const std::vector<TableEntrySpan>& entries,
                                         const std::string_view controllerExpr, const std::size_t indentSpaces) {
  const std::string indent(indentSpaces, ' ');
  const std::string kv = indent + "[\"controller\"] = " + std::string(controllerExpr);
  if (entries.empty()) {
    return TextInsertEdit{openBracePos + 1,
                          "\n" + kv + "\n" + std::string(indentSpaces >= 2 ? indentSpaces - 2 : 0, ' ')};
  }
  const std::size_t insertPos = entries.back().valEnd;
  // Re-check whether the table ends inside one line vs multi-line.
  const bool multiLine = src.substr(openBracePos, closeBracePos - openBracePos).find('\n') != std::string_view::npos;
  const std::string prefix = multiLine ? ",\n" : ", ";
  return TextInsertEdit{insertPos, prefix + kv};
}

void CollectPresetControllerEdits(std::string_view body, const std::vector<TableEntrySpan>& rootEntries,
                                  const std::unordered_map<std::string, std::string>& presetCtrls,
                                  std::vector<TextInsertEdit>& edits) {
  if (presetCtrls.empty()) return;
  for (const auto& entry : rootEntries) {
    if (entry.key != "presets" || entry.valStart >= entry.valEnd || body[entry.valStart] != '{') continue;
    const std::size_t presetsClose = FindMatchingBraceEnd(body, entry.valStart);
    if (presetsClose == std::string::npos) break;
    for (const auto& presetEntry : ScanTableEntries(body, entry.valStart, presetsClose)) {
      const auto it = presetCtrls.find(presetEntry.key);
      if (it == presetCtrls.end() || presetEntry.valStart >= presetEntry.valEnd || body[presetEntry.valStart] != '{') {
        continue;
      }
      const std::size_t pClose = FindMatchingBraceEnd(body, presetEntry.valStart);
      if (pClose == std::string::npos) continue;
      const auto pEntries = ScanTableEntries(body, presetEntry.valStart, pClose);
      edits.push_back(BuildControllerInsertEdit(body, presetEntry.valStart, pClose, pEntries, it->second, 6));
    }
  }
}

}  // namespace

bool ExtractMockControllerSnippets(const std::string_view sourceText, MockControllerSnippets& out) {
  out = MockControllerSnippets{};
  std::size_t returnPos = 0;
  std::size_t exprStart = 0;
  if (!FindTopLevelReturn(sourceText, returnPos, exprStart)) return false;
  if (exprStart >= sourceText.size() || sourceText[exprStart] != '{') return false;

  const std::size_t closeBrace = FindMatchingBraceEnd(sourceText, exprStart);
  if (closeBrace == std::string_view::npos) return false;

  out.preamble = std::string(TrimTrailingAscii(sourceText.substr(0, returnPos)));
  for (const auto& entry : ScanTableEntries(sourceText, exprStart, closeBrace)) {
    if (entry.key == "controller" && entry.valEnd > entry.valStart) {
      out.rootControllerExpr = std::string(TrimAscii(sourceText.substr(entry.valStart, entry.valEnd - entry.valStart)));
    } else if (entry.key == "presets" && entry.valStart < entry.valEnd && sourceText[entry.valStart] == '{') {
      ExtractPresetControllers(sourceText, entry.valStart, out.presetControllerExprs);
    }
  }
  return true;
}

std::string InjectMockControllerSnippets(const std::string_view serializedTableBody,
                                         const MockControllerSnippets& snippets) {
  std::string body(serializedTableBody);
  const std::size_t rootOpen = body.find('{');
  const std::size_t rootClose =
      (rootOpen != std::string::npos) ? FindMatchingBraceEnd(body, rootOpen) : std::string::npos;

  if (rootOpen != std::string::npos && rootClose != std::string::npos) {
    const auto rootEntries = ScanTableEntries(body, rootOpen, rootClose);
    std::vector<TextInsertEdit> edits;
    if (!snippets.rootControllerExpr.empty()) {
      edits.push_back(
          BuildControllerInsertEdit(body, rootOpen, rootClose, rootEntries, snippets.rootControllerExpr, 2));
    }
    CollectPresetControllerEdits(body, rootEntries, snippets.presetControllerExprs, edits);

    std::sort(edits.begin(), edits.end(),
              [](const TextInsertEdit& a, const TextInsertEdit& b) { return a.pos > b.pos; });
    for (const auto& edit : edits) {
      body.insert(edit.pos, edit.text);
    }
  }

  std::string out;
  if (!snippets.preamble.empty()) {
    out += snippets.preamble;
    out += "\n\n";
  }
  out += "return ";
  out += body;
  out += "\n";
  return out;
}

}  // namespace octarine::ui_preview_detail

#endif  // !OCTARINE_SHIPPED
