#include "General/IniParser.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace octarine {

namespace {
constexpr std::string_view kWhitespace = " \t\r\n\f\v";

void RecordWarning(std::vector<std::string>* warnings, size_t lineNo, std::string_view message) {
  if (warnings == nullptr) return;
  std::ostringstream out;
  out << "line " << lineNo << ": " << message;
  warnings->push_back(out.str());
}
}  // namespace

std::string_view IniParser::Trim(std::string_view text) {
  const size_t first = text.find_first_not_of(kWhitespace);
  if (first == std::string_view::npos) return {};
  const size_t last = text.find_last_not_of(kWhitespace);
  return text.substr(first, last - first + 1);
}

bool IniParser::IsCommentOrBlank(std::string_view line) {
  const std::string_view trimmed = Trim(line);
  return trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';';
}

bool IniParser::TryParseSectionHeader(std::string_view line, std::string& sectionOut) {
  const std::string_view trimmed = Trim(line);
  if (trimmed.empty() || trimmed.front() != '[') return false;
  const size_t closePos = trimmed.find(']', 1);
  if (closePos != std::string_view::npos) {
    sectionOut = std::string(Trim(trimmed.substr(1, closePos - 1)));
  }
  return true;
}

bool IniParser::ParseLine(std::string_view line, std::string& keyOut, std::string& valueOut) {
  const std::string_view trimmed = Trim(line);
  const size_t eq = trimmed.find('=');
  if (eq == std::string_view::npos) return false;

  const std::string_view keySv = Trim(trimmed.substr(0, eq));
  if (keySv.empty()) return false;

  keyOut = std::string(keySv);
  valueOut = std::string(Trim(trimmed.substr(eq + 1)));
  return true;
}

std::vector<IniParser::Entry> IniParser::ParseEntries(std::string_view text, std::vector<std::string>* warnings) {
  std::vector<Entry> entries;
  std::string currentSection;
  size_t pos = 0;
  size_t lineNo = 0;

  while (pos <= text.size()) {
    const size_t nl = text.find('\n', pos);
    const size_t end = (nl == std::string_view::npos) ? text.size() : nl;
    const std::string_view rawLine = text.substr(pos, end - pos);
    pos = end + 1;
    ++lineNo;

    const std::string_view trimmed = Trim(rawLine);
    if (!trimmed.empty() && trimmed.front() != '#' && trimmed.front() != ';' &&
        !TryParseSectionHeader(trimmed, currentSection)) {
      std::string key;
      std::string value;
      if (ParseLine(trimmed, key, value)) {
        entries.push_back(Entry{currentSection, std::move(key), std::move(value), lineNo});
      } else if (trimmed.find('=') == std::string_view::npos) {
        RecordWarning(warnings, lineNo, std::string("malformed (no '='): ") + std::string(trimmed));
      } else {
        RecordWarning(warnings, lineNo, "empty key");
      }
    }

    if (nl == std::string_view::npos) break;
  }

  return entries;
}

std::unordered_map<std::string, std::string> IniParser::ParseFlatMap(std::string_view text,
                                                                     std::vector<std::string>* warnings) {
  std::unordered_map<std::string, std::string> result;
  for (auto& entry : ParseEntries(text, warnings)) {
    result[std::move(entry.key)] = std::move(entry.value);
  }
  return result;
}

bool IniParser::ParseBool(std::string_view value) {
  const std::string_view v = Trim(value);
  return v == "true" || v == "1" || v == "yes" || v == "on";
}

bool IniParser::TryParseInt64(std::string_view raw, int64_t& out) {
  const std::string_view trimmed = Trim(raw);
  if (trimmed.empty()) return false;
  const std::string str(trimmed);
  char* end = nullptr;
  errno = 0;
  const long long val = std::strtoll(str.c_str(), &end, 10);
  if (errno != 0 || end != str.c_str() + str.size()) return false;
  out = static_cast<int64_t>(val);
  return true;
}

bool IniParser::TryParseDouble(std::string_view raw, double& out) {
  const std::string_view trimmed = Trim(raw);
  if (trimmed.empty()) return false;
  if (trimmed.find('.') == std::string_view::npos && trimmed.find('e') == std::string_view::npos &&
      trimmed.find('E') == std::string_view::npos) {
    return false;
  }
  const std::string str(trimmed);
  char* end = nullptr;
  errno = 0;
  const double val = std::strtod(str.c_str(), &end);
  if (errno != 0 || end != str.c_str() + str.size() || !std::isfinite(val)) return false;
  out = val;
  return true;
}

std::string IniParser::EscapeQuotedString(std::string_view str) {
  std::string out = "\"";
  for (const char c : str) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  out.push_back('"');
  return out;
}

std::string IniParser::UnescapeQuotedString(std::string_view quoted) {
  if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"') {
    return std::string(quoted);
  }
  std::string out;
  out.reserve(quoted.size() - 2);
  for (size_t i = 1; i + 1 < quoted.size(); ++i) {
    if (quoted[i] == '\\' && i + 2 < quoted.size()) {
      ++i;
      switch (quoted[i]) {
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        default:
          out.push_back(quoted[i]);
          break;
      }
    } else {
      out.push_back(quoted[i]);
    }
  }
  return out;
}

IniParser::Value IniParser::ParseValue(std::string_view raw) {
  const std::string_view trimmed = Trim(raw);
  if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
    return UnescapeQuotedString(trimmed);
  }
  if (trimmed == "true") return true;
  if (trimmed == "false") return false;

  int64_t intVal = 0;
  if (TryParseInt64(trimmed, intVal)) return intVal;

  double doubleVal = 0.0;
  if (TryParseDouble(trimmed, doubleVal)) return doubleVal;

  return std::string(trimmed);
}

std::string IniParser::FormatValue(const Value& val) {
  if (std::holds_alternative<bool>(val)) {
    return std::get<bool>(val) ? "true" : "false";
  }
  if (std::holds_alternative<int64_t>(val)) {
    return std::to_string(std::get<int64_t>(val));
  }
  if (std::holds_alternative<double>(val)) {
    const double d = std::get<double>(val);
    std::ostringstream ss;
    ss << std::setprecision(15) << d;
    std::string s = ss.str();
    if (std::isfinite(d) && s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find('E') == std::string::npos) {
      s += ".0";
    }
    return s;
  }
  return EscapeQuotedString(std::get<std::string>(val));
}

std::string IniParser::ValueToPlainString(const Value& val) {
  if (std::holds_alternative<std::string>(val)) {
    return std::get<std::string>(val);
  }
  if (std::holds_alternative<bool>(val)) {
    return std::get<bool>(val) ? "true" : "false";
  }
  if (std::holds_alternative<int64_t>(val)) {
    return std::to_string(std::get<int64_t>(val));
  }
  std::ostringstream ss;
  ss << std::get<double>(val);
  return ss.str();
}

std::optional<IniParser::Value> IniParser::CoerceValue(const Value& val, const Value& targetType) {
  if (val.index() == targetType.index()) {
    return val;
  }
  if (std::holds_alternative<double>(targetType) && std::holds_alternative<int64_t>(val)) {
    return static_cast<double>(std::get<int64_t>(val));
  }
  if (std::holds_alternative<int64_t>(targetType) && std::holds_alternative<double>(val)) {
    return static_cast<int64_t>(std::get<double>(val));
  }
  return std::nullopt;
}

}  // namespace octarine
