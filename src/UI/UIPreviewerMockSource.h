#pragma once

#ifndef OCTARINE_SHIPPED

#include <string>
#include <string_view>
#include <unordered_map>

namespace octarine::ui_preview_detail {

// Extracted Lua controller expressions and pre-return preamble from an existing .preview.lua file.
struct MockControllerSnippets {
  std::string preamble{};
  std::string rootControllerExpr{};
  std::unordered_map<std::string, std::string> presetControllerExprs{};

  [[nodiscard]] bool HasAnyController() const noexcept {
    return !rootControllerExpr.empty() || !presetControllerExprs.empty();
  }
};

// Scans an existing .preview.lua source string for a literal `return { ... }` table and extracts
// optional pre-return preamble (e.g. local helpers), root `controller = ...`, and per-preset
// `presets.<name>.controller = ...`. Returns false if the top-level return is not a table literal.
[[nodiscard]] bool ExtractMockControllerSnippets(std::string_view sourceText, MockControllerSnippets& out);

// Injects preserved controller expressions and preamble into a serialized Lua data table body
// produced by lua_table_serializer::Serialize (without a leading `return `).
[[nodiscard]] std::string InjectMockControllerSnippets(std::string_view serializedTableBody,
                                                       const MockControllerSnippets& snippets);

}  // namespace octarine::ui_preview_detail

#endif  // !OCTARINE_SHIPPED
