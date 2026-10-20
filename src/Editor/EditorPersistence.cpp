#include "EditorPersistence.h"

#ifndef OCTARINE_SHIPPED

#include <SDL3/SDL_filesystem.h>

#include <cctype>
#include <fstream>
#include <sstream>

#include "General/IniParser.h"
#include "General/Logger.h"

namespace {
constexpr auto kGlobalFileName = "editor_settings.ini";
constexpr auto kProjectFileName = "editor_prefs.ini";

std::string GlobalPath() {
  char* prefPath = SDL_GetPrefPath("Octarine", "Engine");
  if (!prefPath) return {};
  std::string out = std::string(prefPath) + kGlobalFileName;
  SDL_free(prefPath);
  return out;
}

std::string ProjectPath(const std::string& projectAssetPath) {
  if (projectAssetPath.empty()) return {};
  return projectAssetPath + "/" + kProjectFileName;
}

void ApplyProjectSetting(EditorPersistence& prefs, const std::string& key, const std::string& value) {
  if (key == "currentScenePath") {
    prefs.currentScenePath = value;
    return;
  }
  if (key == "showDebugGUI") {
    prefs.showDebugGUI = octarine::IniParser::ParseBool(value);
    return;
  }
  if (key == "drawColliders") {
    prefs.drawColliders = octarine::IniParser::ParseBool(value);
    return;
  }
  if (key == "showFpsCounter") {
    prefs.showFpsCounter = octarine::IniParser::ParseBool(value);
    return;
  }
  if (key == "showEntityInfo") {
    prefs.showEntityInfo = octarine::IniParser::ParseBool(value);
    return;
  }
  for (const auto& [flagKey, member] : EditorPersistence::kWindowFlags) {
    if (key == flagKey) {
      prefs.*member = octarine::IniParser::ParseBool(value);
      return;
    }
  }
}
}  // namespace

bool ParseIniLine(const std::string& line, std::string& key, std::string& value) {
  return octarine::IniParser::ParseLine(line, key, value);
}

void EditorPersistence::SaveGlobal() const {
#ifdef OCTARINE_PLATFORM_MOBILE
  return;
#else
  const auto path = GlobalPath();
  if (path.empty()) {
    Logger::Error("Failed to resolve editor global pref path: " + std::string(SDL_GetError()));
    return;
  }

  std::ofstream file(path);
  if (!file.is_open()) {
    Logger::Error("Failed to open editor global preferences file for writing: " + path);
    return;
  }

  file << "lastProjectPath=" << lastProjectPath << "\n";
  file << "editorFontSize=" << editorFontSize << "\n";
  file << "editorStyleIndex=" << editorStyleIndex << "\n";
  file << "audioMuted=" << (audioMuted ? "true" : "false") << "\n";
  file << "masterVolume=" << masterVolume << "\n";
#endif
}

void EditorPersistence::LoadGlobal() {
#ifdef OCTARINE_PLATFORM_MOBILE
  return;
#else
  const auto path = GlobalPath();
  if (path.empty()) return;

  std::ifstream file(path);
  if (!file.is_open()) return;

  std::string line;
  std::string key;
  std::string value;
  while (std::getline(file, line)) {
    if (!ParseIniLine(line, key, value)) continue;

    if (key == "lastProjectPath") {
      lastProjectPath = value;
    } else if (key == "editorFontSize") {
      try {
        editorFontSize = std::stof(value);
      } catch (...) {
      }
    } else if (key == "editorStyleIndex") {
      try {
        editorStyleIndex = std::stoi(value);
      } catch (...) {
      }
    } else if (key == "audioMuted") {
      audioMuted = (value == "true");
    } else if (key == "masterVolume") {
      try {
        masterVolume = std::stof(value);
      } catch (...) {
      }
    }
  }
#endif
}

void EditorPersistence::SaveProject(const std::string& projectAssetPath) const {
#ifdef OCTARINE_PLATFORM_MOBILE
  // On iOS and Android, assets live in read-only bundles/APKs. Suppress writing editor_prefs.ini
  // to avoid disk write errors on mobile devices.
  (void)projectAssetPath;
  return;
#else
  const auto path = ProjectPath(projectAssetPath);
  if (path.empty()) return;

  std::ofstream file(path);
  if (!file.is_open()) {
    Logger::Error("Failed to open editor project preferences file for writing: " + path);
    return;
  }

  file << "currentScenePath=" << currentScenePath << "\n";
  for (const auto& [key, member] : kWindowFlags) {
    file << key << "=" << (this->*member ? "true" : "false") << "\n";
  }
  file << "showDebugGUI=" << (showDebugGUI ? "true" : "false") << "\n";
  file << "drawColliders=" << (drawColliders ? "true" : "false") << "\n";
  file << "showFpsCounter=" << (showFpsCounter ? "true" : "false") << "\n";
  file << "showEntityInfo=" << (showEntityInfo ? "true" : "false") << "\n";
#endif
}

void EditorPersistence::LoadProject(const std::string& projectAssetPath) {
#ifdef OCTARINE_PLATFORM_MOBILE
  (void)projectAssetPath;
  return;
#else
  const auto path = ProjectPath(projectAssetPath);
  if (path.empty()) return;

  std::ifstream file(path);
  if (!file.is_open()) return;  // Silent — fine if it doesn't exist yet.

  std::string line;
  std::string key;
  std::string value;
  while (std::getline(file, line)) {
    if (ParseIniLine(line, key, value)) {
      ApplyProjectSetting(*this, key, value);
    }
  }
#endif
}

#endif  // OCTARINE_SHIPPED
