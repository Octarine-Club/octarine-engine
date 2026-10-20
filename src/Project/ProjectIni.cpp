#include "Project/ProjectIni.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <utility>

#include "General/IniParser.h"

namespace octarine::project {
namespace {
// Mirrors the CMake regex in cmake/OctarinePackage.cmake and the Gradle check in
// android/app/build.gradle. Lowercase reverse-DNS, segments separated by '.'.
const std::regex& PackageIdRegex() {
  static const std::regex re(R"(^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$)");
  return re;
}

void Assign(ProjectIni& ini, const std::string& key, std::string value) {
  if (key == "name")
    ini.name = value;
  else if (key == "package_id")
    ini.package_id = value;
  else if (key == "version_name")
    ini.version_name = value;
  else if (key == "version_code")
    ini.version_code = value;
  else if (key == "vendor")
    ini.vendor = value;
  else if (key == "description")
    ini.description = value;
  else if (key == "orientation")
    ini.orientation = value;
  else if (key == "fullscreen")
    ini.fullscreen = value;
  else if (key == "permissions")
    ini.permissions = value;
  else if (key == "category")
    ini.category = value;
  else if (key == "icon")
    ini.icon = value;
  ini.all[key] = std::move(value);
}
}  // namespace

ProjectIni ProjectIni::Parse(std::string_view text, std::vector<std::string>* parse_warnings) {
  ProjectIni ini;
  for (auto& entry : octarine::IniParser::ParseEntries(text, parse_warnings)) {
    Assign(ini, entry.key, std::move(entry.value));
  }
  return ini;
}

std::optional<ProjectIni> ProjectIni::Load(const std::filesystem::path& project_dir) {
  const std::filesystem::path path = project_dir / kFilename;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return std::nullopt;
  }
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return std::nullopt;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  ProjectIni ini = Parse(ss.str());
  ini.source_path = path;
  return ini;
}

std::vector<std::string> ProjectIni::ValidateForShipping() const {
  std::vector<std::string> errors;
  if (name.empty()) {
    errors.emplace_back("missing required key: name");
  }
  if (version_name.empty()) {
    errors.emplace_back("missing required key: version_name");
  }
  if (package_id.empty()) {
    errors.emplace_back("missing required key: package_id (Android bundle id)");
  } else if (!std::regex_match(package_id, PackageIdRegex())) {
    errors.push_back("package_id '" + package_id + "' must be reverse-DNS (e.g. com.studio.mygame)");
  }
  return errors;
}
}  // namespace octarine::project
