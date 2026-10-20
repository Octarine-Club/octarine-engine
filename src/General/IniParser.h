#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace octarine {

// Generalized INI parser and typed value serializer shared across GameConfig (config.ini),
// ProjectIni (project.ini), UserSettings (user_settings.ini), and EditorPersistence.
class IniParser {
 public:
  using Value = std::variant<bool, int64_t, double, std::string>;

  struct Entry {
    std::string section{};
    std::string key{};
    std::string value{};
    size_t lineNumber = 0;
  };

  // Trims leading and trailing ASCII whitespace (spaces, tabs, CR, LF).
  [[nodiscard]] static std::string_view Trim(std::string_view text);

  // Returns true if the line is empty or starts with a comment character ('#' or ';').
  [[nodiscard]] static bool IsCommentOrBlank(std::string_view line);

  // Parses a `[section]` header line. Returns true if the line starts with '['.
  [[nodiscard]] static bool TryParseSectionHeader(std::string_view line, std::string& sectionOut);

  // Splits a `key = value` line, trimming whitespace around both key and value.
  // Returns false if no '=' is present or if the key is empty.
  [[nodiscard]] static bool ParseLine(std::string_view line, std::string& keyOut, std::string& valueOut);

  // Parses an entire INI document into ordered entries (preserving section context and line numbers).
  // If `warnings` is non-null, malformed lines (missing '=' or empty key) are recorded there.
  [[nodiscard]] static std::vector<Entry> ParseEntries(std::string_view text,
                                                       std::vector<std::string>* warnings = nullptr);

  // Parses an INI document into a flat key -> value map (ignoring section headers).
  [[nodiscard]] static std::unordered_map<std::string, std::string> ParseFlatMap(
      std::string_view text, std::vector<std::string>* warnings = nullptr);

  // Evaluates common INI boolean truthy strings ("true", "1", "yes", "on").
  [[nodiscard]] static bool ParseBool(std::string_view value);

  // Strict integer and floating-point parsers (reject trailing garbage or overflow).
  [[nodiscard]] static bool TryParseInt64(std::string_view raw, int64_t& out);
  [[nodiscard]] static bool TryParseDouble(std::string_view raw, double& out);

  // Escapes and unescapes double-quoted INI string literals ("..." with \\, \", \n, \r, \t).
  [[nodiscard]] static std::string EscapeQuotedString(std::string_view str);
  [[nodiscard]] static std::string UnescapeQuotedString(std::string_view quoted);

  // Parses a raw INI value token into a typed Value (quoted string, bool, int64_t, double, or unquoted string).
  [[nodiscard]] static Value ParseValue(std::string_view raw);

  // Formats a typed Value for writing to an INI file (booleans, numbers, or quoted strings).
  [[nodiscard]] static std::string FormatValue(const Value& val);

  // Converts a typed Value to an unquoted plain string for string-based callers.
  [[nodiscard]] static std::string ValueToPlainString(const Value& val);

  // Coerces `val` to match the variant alternative of `targetType` (allowing int64_t <-> double).
  // Returns nullopt if the types are incompatible.
  [[nodiscard]] static std::optional<Value> CoerceValue(const Value& val, const Value& targetType);
};

}  // namespace octarine
