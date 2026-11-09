#include "Debug/DebugMenuRegistry.h"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace octarine::debug {

namespace {
std::pair<std::string, std::string> SplitMenuPath(std::string_view path) {
  const auto slashPos = path.find('/');
  if (slashPos == std::string_view::npos) {
    return {std::string(path), std::string(path)};
  }
  return {std::string(path.substr(0, slashPos)), std::string(path.substr(slashPos + 1))};
}
}  // namespace

void DebugMenuRegistry::RegisterPanel(DebugPanelDescriptor descriptor) {
  if (auto* existing = FindPanel(descriptor.id)) {
    existing->category = std::move(descriptor.category);
    existing->title = std::move(descriptor.title);
    existing->shortcut = std::move(descriptor.shortcut);
    existing->autoWindow = descriptor.autoWindow;
    existing->isLuaOwned = descriptor.isLuaOwned;
    existing->drawCallback = std::move(descriptor.drawCallback);
    return;
  }

  if (const auto it = persisted_open_states_.find(descriptor.id); it != persisted_open_states_.end()) {
    descriptor.isOpen = it->second;
  }

  panels_.push_back(std::move(descriptor));
}

bool DebugMenuRegistry::UnregisterPanel(std::string_view id) {
  const auto it =
      std::find_if(panels_.begin(), panels_.end(), [id](const DebugPanelDescriptor& p) { return p.id == id; });
  if (it == panels_.end()) return false;
  persisted_open_states_[it->id] = it->isOpen;
  panels_.erase(it);
  return true;
}

DebugPanelDescriptor* DebugMenuRegistry::FindPanel(std::string_view id) {
  const auto it =
      std::find_if(panels_.begin(), panels_.end(), [id](const DebugPanelDescriptor& p) { return p.id == id; });
  return it != panels_.end() ? &(*it) : nullptr;
}

const DebugPanelDescriptor* DebugMenuRegistry::FindPanel(std::string_view id) const {
  const auto it =
      std::find_if(panels_.begin(), panels_.end(), [id](const DebugPanelDescriptor& p) { return p.id == id; });
  return it != panels_.end() ? &(*it) : nullptr;
}

bool DebugMenuRegistry::IsPanelOpen(std::string_view id) const {
  const auto* panel = FindPanel(id);
  if (panel != nullptr) {
    return panel->isOpen;
  }
  const auto it = persisted_open_states_.find(std::string(id));
  return it != persisted_open_states_.end() && it->second;
}

void DebugMenuRegistry::SetPanelOpen(std::string_view id, bool open) {
  persisted_open_states_[std::string(id)] = open;
  if (auto* panel = FindPanel(id)) {
    panel->isOpen = open;
  }
}

void DebugMenuRegistry::CloseAllPanels() {
  for (auto& panel : panels_) {
    panel.isOpen = false;
    persisted_open_states_[panel.id] = false;
  }
}

void DebugMenuRegistry::RegisterAction(std::string_view menuPath, std::function<void(Registry&)> callback,
                                       std::string_view shortcut, bool isLuaOwned) {
  auto [cat, lbl] = SplitMenuPath(menuPath);
  for (auto& a : actions_) {
    if (a.menuPath == menuPath) {
      a.category = std::move(cat);
      a.label = std::move(lbl);
      a.shortcut = std::string(shortcut);
      a.isLuaOwned = isLuaOwned;
      a.callback = std::move(callback);
      return;
    }
  }
  actions_.push_back({
      .menuPath = std::string(menuPath),
      .category = std::move(cat),
      .label = std::move(lbl),
      .shortcut = std::string(shortcut),
      .isLuaOwned = isLuaOwned,
      .callback = std::move(callback),
  });
}

void DebugMenuRegistry::RegisterToggle(std::string_view menuPath, std::function<bool()> getter,
                                       std::function<void(bool)> setter, bool isLuaOwned) {
  auto [cat, lbl] = SplitMenuPath(menuPath);
  for (auto& t : toggles_) {
    if (t.menuPath == menuPath) {
      t.category = std::move(cat);
      t.label = std::move(lbl);
      t.isLuaOwned = isLuaOwned;
      t.getter = std::move(getter);
      t.setter = std::move(setter);
      return;
    }
  }
  toggles_.push_back({
      .menuPath = std::string(menuPath),
      .category = std::move(cat),
      .label = std::move(lbl),
      .isLuaOwned = isLuaOwned,
      .getter = std::move(getter),
      .setter = std::move(setter),
  });
}

void DebugMenuRegistry::RegisterWatch(std::string_view label, std::function<std::string()> formatter, bool isLuaOwned) {
  const auto it = std::find_if(watches_.begin(), watches_.end(),
                               [label](const DebugWatchDescriptor& w) { return w.label == label; });
  if (it != watches_.end()) {
    it->isLuaOwned = isLuaOwned;
    it->valueFormatter = std::move(formatter);
    return;
  }
  watches_.push_back({
      .label = std::string(label),
      .isLuaOwned = isLuaOwned,
      .valueFormatter = std::move(formatter),
  });
}

bool DebugMenuRegistry::UnregisterWatch(std::string_view label) {
  const auto it = std::find_if(watches_.begin(), watches_.end(),
                               [label](const DebugWatchDescriptor& w) { return w.label == label; });
  if (it == watches_.end()) return false;
  watches_.erase(it);
  return true;
}

void DebugMenuRegistry::ClearLuaOwned() {
  for (const auto& panel : panels_) {
    if (panel.isLuaOwned) {
      persisted_open_states_[panel.id] = panel.isOpen;
    }
  }

  std::erase_if(panels_, [](const DebugPanelDescriptor& p) { return p.isLuaOwned; });
  std::erase_if(actions_, [](const DebugMenuActionDescriptor& a) { return a.isLuaOwned; });
  std::erase_if(toggles_, [](const DebugMenuToggleDescriptor& t) { return t.isLuaOwned; });
  std::erase_if(watches_, [](const DebugWatchDescriptor& w) { return w.isLuaOwned; });
}

void DebugMenuRegistry::ApplyPersistedOpenState(std::string_view id, bool isOpen) {
  persisted_open_states_[std::string(id)] = isOpen;
  if (auto* panel = FindPanel(id)) {
    panel->isOpen = isOpen;
  }
}

std::string DebugMenuRegistry::SerializeOpenStates() const {
  auto states = persisted_open_states_;
  for (const auto& panel : panels_) {
    states[panel.id] = panel.isOpen;
  }

  std::ostringstream ss;
  for (const auto& [id, isOpen] : states) {
    ss << id << "=" << (isOpen ? "1" : "0") << "\n";
  }
  return ss.str();
}

void DebugMenuRegistry::DeserializeOpenStates(std::string_view iniText) {
  std::istringstream ss{std::string(iniText)};
  std::string line;
  while (std::getline(ss, line)) {
    if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') {
      continue;
    }
    const auto eq = line.find('=');
    if (eq != std::string::npos) {
      const std::string key = line.substr(0, eq);
      const std::string val = line.substr(eq + 1);
      const bool isOpen = (val == "1" || val == "true");
      ApplyPersistedOpenState(key, isOpen);
    }
  }
}

}  // namespace octarine::debug
