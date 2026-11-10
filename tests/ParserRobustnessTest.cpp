#include <string>
#include <vector>

#include "General/IniParser.h"
#include "TestHarness.h"
#include "Tilemap/LdtkParser.h"
#include "UI/OssParser.h"
#include "UI/OxmlParser.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

void TestIniParserRobustness() {
  std::cout << "[Parser: IniParser robustness]\n";

  // 1. Valid INI with mixed sections, comments, types, and escapes
  const std::string validIni = R"(
    # Global comment
    title = "Octarine Engine"
    fps = 60
    scale = 1.5
    fullscreen = true

    [audio]
    ; Audio settings
    volume = 0.8
    muted = false

    [paths]
    asset_dir = "assets/data\nnewline"
  )";

  std::vector<std::string> warnings;
  auto entries = octarine::IniParser::ParseEntries(validIni, &warnings);
  Check(warnings.empty(), "Valid INI produces 0 warnings");
  CheckEq(entries.size(), 7UL, "Parsed 7 valid entries");

  auto flatMap = octarine::IniParser::ParseFlatMap(validIni);
  CheckEq(octarine::IniParser::UnescapeQuotedString(flatMap["title"]), std::string("Octarine Engine"),
          "Unescaped quoted string matches");
  CheckEq(flatMap["fps"], std::string("60"), "Integer matches");
  CheckEq(flatMap["volume"], std::string("0.8"), "Float matches");

  // 2. Malformed lines
  const std::string malformedIni = R"(
    valid_key = 1
    invalid_line_no_equals
    = missing_key
    [unclosed_section
    another_valid = 2
  )";

  warnings.clear();
  auto malformedEntries = octarine::IniParser::ParseEntries(malformedIni, &warnings);
  CheckEq(malformedEntries.size(), 2UL, "Only valid entries parsed from malformed text");
  Check(!warnings.empty(), "Warnings recorded for malformed lines");

  // 3. Type parsing edge cases
  int64_t intVal = 0;
  Check(octarine::IniParser::TryParseInt64("  12345  ", intVal), "TryParseInt64 trims whitespace");
  CheckEq(intVal, 12345LL, "Parsed int value correct");
  Check(!octarine::IniParser::TryParseInt64("123abc", intVal), "TryParseInt64 rejects trailing characters");

  double dblVal = 0.0;
  Check(octarine::IniParser::TryParseDouble("3.14159", dblVal), "TryParseDouble succeeds");
  Check(!octarine::IniParser::TryParseDouble("3.14.15", dblVal), "TryParseDouble rejects invalid double");

  Check(octarine::IniParser::ParseBool("true"), "ParseBool true");
  Check(octarine::IniParser::ParseBool("1"), "ParseBool 1");
  Check(octarine::IniParser::ParseBool("yes"), "ParseBool yes");
  Check(octarine::IniParser::ParseBool("on"), "ParseBool on");
  Check(!octarine::IniParser::ParseBool("false"), "ParseBool false");
  Check(!octarine::IniParser::ParseBool("0"), "ParseBool 0");
  Check(!octarine::IniParser::ParseBool("invalid"), "ParseBool invalid defaults to false");
}

void TestOxmlParserRobustness() {
  std::cout << "[Parser: OxmlParser robustness]\n";

  // 1. Valid OXML
  const std::string validOxml = R"(
    <Style src="theme.oss"/>
    <Use name="Button" src="button.oxml"/>
    <view id="root" class="container row" active="true">
      <text id="lblTitle">Hello World</text>
      <button id="btnPlay" class="primary"/>
    </view>
  )";

  auto resValid = OxmlParser::Parse(validOxml);
  Check(resValid.ok, "Valid OXML parsed successfully");
  CheckEq(resValid.document.styles.size(), 1UL, "Parsed 1 style import");
  CheckEq(resValid.document.uses.size(), 1UL, "Parsed 1 use import");
  CheckEq(resValid.document.roots.size(), 1UL, "Parsed 1 root node");
  CheckEq(resValid.document.roots[0].tag, std::string("view"), "Root tag is view");
  CheckEq(resValid.document.roots[0].children.size(), 2UL, "Root has 2 children");

  // 2. Malformed OXML (mismatched tags, unclosed tags, stray closing tag)
  const std::string unclosedTag = "<view><text>unclosed";
  auto resUnclosed = OxmlParser::Parse(unclosedTag);
  Check(!resUnclosed.ok, "Unclosed tag fails parse");
  Check(resUnclosed.errorLine > 0, "Error line reported for unclosed tag");

  const std::string mismatchedTag = "<view><text>text</view></text>";
  auto resMismatched = OxmlParser::Parse(mismatchedTag);
  Check(!resMismatched.ok, "Mismatched closing tag fails parse");

  const std::string strayClosing = "</stray>";
  auto resStray = OxmlParser::Parse(strayClosing);
  Check(!resStray.ok, "Stray closing tag fails parse");

  const std::string emptyInput;
  auto resEmpty = OxmlParser::Parse(emptyInput);
  Check(!resEmpty.ok, "Empty input rejected (requires at least one root element)");
  CheckEq(resEmpty.document.roots.size(), 0UL, "Empty document has 0 roots");
}

void TestOssParserRobustness() {
  std::cout << "[Parser: OssParser robustness]\n";

  // 1. Valid OSS
  const std::string validOss = R"(
    view {
      padding: 10px;
      background-color: #ff0000;
    }
    .button:hover {
      background-color: #00ff00;
      opacity: 0.8;
    }
    #main_menu {
      width: 100%;
      height: 100%;
    }
  )";

  auto res = OssParser::Parse(validOss);
  Check(res.ok, "Valid OSS parsed cleanly");
  CheckEq(res.stylesheet.rules.size(), 3UL, "Parsed 3 rules");

  // Specificity check
  Check(res.stylesheet.rules[2].selector.Specificity() > res.stylesheet.rules[1].selector.Specificity(),
        "#id specificity > .class specificity");
  Check(res.stylesheet.rules[1].selector.Specificity() > res.stylesheet.rules[0].selector.Specificity(),
        ".class specificity > tag specificity");

  // 2. Malformed OSS (missing closing brace, stray characters)
  const std::string malformedOss = R"(
    view {
      padding: 10px;
    #stray_without_closing {
      width: 50px;
  )";

  auto resMalformed = OssParser::Parse(malformedOss);
  Check(!resMalformed.ok, "Malformed OSS returns ok = false");
  Check(!resMalformed.error.empty(), "Error message recorded for malformed OSS");
}

void TestLdtkParserRobustness() {
  std::cout << "[Parser: LdtkParser robustness]\n";

  // 1. Invalid / malformed JSON strings should return false / nullopt safely
  auto resEmpty = octarine::tilemap::LdtkParser::ParseFromJsonString("");
  Check(!resEmpty.has_value(), "Empty string rejected");

  auto resBad = octarine::tilemap::LdtkParser::ParseFromJsonString("{ this is not valid json }");
  Check(!resBad.has_value(), "Invalid JSON rejected without throwing");

  auto resArray = octarine::tilemap::LdtkParser::ParseFromJsonString("[]");
  Check(!resArray.has_value(), "Non-object JSON rejected without throwing");

  // 2. Valid minimal mock LDtk JSON
  const std::string minimalLdtk = R"({
    "jsonVersion": "1.5.3",
    "worldGridWidth": 256,
    "worldGridHeight": 256,
    "defs": {
      "layers": [],
      "entities": [],
      "tilesets": []
    },
    "levels": [
      {
        "identifier": "Level_0",
        "uid": 1,
        "pxWid": 256,
        "pxHei": 256,
        "layerInstances": []
      }
    ]
  })";

  auto resValid = octarine::tilemap::LdtkParser::ParseFromJsonString(minimalLdtk);
  Check(resValid.has_value(), "Minimal valid LDtk JSON parses cleanly");
}

}  // namespace

int main() {
  TestIniParserRobustness();
  TestOxmlParserRobustness();
  TestOssParserRobustness();
  TestLdtkParserRobustness();
  return octarine::test::ReportSummary("OctarineParserRobustnessTest");
}
