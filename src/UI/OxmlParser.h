#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

struct OxmlStyleImport {
  std::string src{};
  int line = 1;
};

struct OxmlUseImport {
  std::string name{};
  std::string src{};
  int line = 1;
};

struct OxmlParamDecl {
  std::string name{};
  std::string defaultValue{};
  int line = 1;
};

struct OxmlNode {
  std::string tag{};
  std::map<std::string, std::string> attributes{};
  std::string innerText{};
  std::vector<OxmlNode> children{};
  int line = 1;

  [[nodiscard]] std::string GetAttr(const std::string& key, const std::string& fallback = "") const {
    const auto it = attributes.find(key);
    return it != attributes.end() ? it->second : fallback;
  }

  [[nodiscard]] bool HasAttr(const std::string& key) const { return attributes.find(key) != attributes.end(); }
};

struct OxmlDocument {
  std::vector<OxmlStyleImport> styles{};
  std::vector<OxmlUseImport> uses{};
  std::vector<OxmlParamDecl> params{};
  std::vector<OxmlNode> roots{};
};

struct OxmlParseResult {
  bool ok = false;
  std::string error{};
  int errorLine = 0;
  OxmlDocument document{};
};

struct OxmlEachClause {
  std::string itemVar{};
  std::string indexVar{};
  std::string listExpr{};
  bool valid = false;
};

struct OxmlAssetRefs {
  std::vector<std::string> styleSources{};
  std::vector<std::string> useSources{};
  std::vector<std::string> spriteIds{};
  std::vector<std::string> fontIds{};
  std::vector<std::string> audioIds{};
};

class OxmlParser {
 public:
  [[nodiscard]] static OxmlParseResult Parse(std::string_view source);

  [[nodiscard]] static bool ContainsBinding(std::string_view value);
  [[nodiscard]] static bool IsPureBinding(std::string_view value, std::string& outExpr);
  [[nodiscard]] static OxmlEachClause ParseEachClause(std::string_view attr);
  [[nodiscard]] static OxmlAssetRefs CollectStaticAssetRefs(const OxmlDocument& doc);
};
