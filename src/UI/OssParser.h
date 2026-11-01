#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Components/UILayoutBoxComponent.h"
#include "Components/UIStyleStateComponent.h"
#include "General/Color.h"

enum class OssPseudoState : uint8_t {
  None = 0,
  Hover = 1,
  Press = 2,
  Focus = 3,
  Checked = 4,
  Disabled = 5,
};

struct OssSelector {
  std::string tag{};
  std::string className{};
  std::string id{};
  OssPseudoState pseudo = OssPseudoState::None;

  [[nodiscard]] int Specificity() const {
    int score = 0;
    if (!id.empty()) score += 100;
    if (!className.empty()) {
      score += 10;
      for (const char c : className) {
        if (c == '.') score += 10;
      }
    }
    if (!tag.empty()) score += 1;
    return score;
  }

  [[nodiscard]] bool MatchesElement(std::string_view elemTag, const std::vector<std::string>& elemClasses,
                                    std::string_view elemId) const;
};

struct OssRule {
  OssSelector selector{};
  std::map<std::string, std::string> properties{};
  int order = 0;
};

struct OssResolvedStyle {
  std::map<std::string, std::string> base{};
  std::map<std::string, std::string> hover{};
  std::map<std::string, std::string> press{};
  std::map<std::string, std::string> focus{};
  std::map<std::string, std::string> checked{};
  std::map<std::string, std::string> disabled{};

  [[nodiscard]] std::string Get(const std::string& key, const std::string& fallback = "") const {
    const auto it = base.find(key);
    return it != base.end() ? it->second : fallback;
  }

  [[nodiscard]] bool Has(const std::string& key) const { return base.find(key) != base.end(); }

  [[nodiscard]] bool HasPseudoStates() const {
    return !hover.empty() || !press.empty() || !focus.empty() || !checked.empty() || !disabled.empty();
  }

  [[nodiscard]] UIStyleStateComponent BuildStyleStateComponent() const;
};

class OssStylesheet {
 public:
  std::map<std::string, std::string> variables{};
  std::vector<OssRule> rules{};

  void MergeFrom(const OssStylesheet& other);

  [[nodiscard]] std::string ResolveVariables(std::string_view rawValue) const;

  [[nodiscard]] OssResolvedStyle Resolve(std::string_view tag, const std::vector<std::string>& classes,
                                         std::string_view id,
                                         const std::map<std::string, std::string>& inlineProps = {}) const;
};

struct OssParseResult {
  bool ok = false;
  std::string error{};
  int errorLine = 0;
  OssStylesheet stylesheet{};
};

class OssParser {
 public:
  [[nodiscard]] static OssParseResult Parse(std::string_view source);

  [[nodiscard]] static std::vector<std::string> SplitClassList(std::string_view classAttr);
  [[nodiscard]] static std::map<std::string, std::string> ParseInlineStyle(std::string_view styleAttr);

  static bool ParseColor(std::string_view str, octarine::Color& outColor);
  static bool ParseDimension(std::string_view str, float& outValue, UIUnit& outUnit);
  static bool ParseFloat(std::string_view str, float& outVal);
  static bool ParseEdges(std::string_view str, float& top, float& right, float& bottom, float& left);
  static bool ParseSlice4(std::string_view str, float& left, float& top, float& right, float& bottom);
};
