#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ECS/Entity.h"

class Registry;

namespace octarine::debug {

struct DebugPanelDescriptor {
  std::string id{};         // Stable ID, e.g. "engine.entity_inspector"
  std::string category{};   // Top menu category: "Engine", "ECS", "World", "Assets", "UI", "Game"
  std::string title{};      // Window title: "Entity Inspector"
  std::string shortcut{};   // Optional shortcut hint in menu
  bool isOpen = false;      // Closed by default; restored from per-project imgui.ini if saved
  bool autoWindow = true;   // Wrap drawCallback in ImGui::Begin(title, &isOpen) / ImGui::End()
  bool isLuaOwned = false;  // Updated in-place on hot-reload; cleared on scene teardown
  std::function<void(Registry&)> drawCallback{};
};

struct DebugMenuActionDescriptor {
  std::string menuPath{};  // e.g. "Game/Cheats/Refill Health"
  std::string category{};  // Parsed top-level menu ("Game")
  std::string label{};     // Parsed item label ("Cheats/Refill Health" or leaf)
  std::string shortcut{};
  bool isLuaOwned = false;
  std::function<void(Registry&)> callback{};
};

struct DebugMenuToggleDescriptor {
  std::string menuPath{};  // e.g. "Game/Cheats/God Mode"
  std::string category{};
  std::string label{};
  bool isLuaOwned = false;
  std::function<bool()> getter{};
  std::function<void(bool)> setter{};
};

struct DebugWatchDescriptor {
  std::string label{};
  bool isLuaOwned = false;
  std::function<std::string()> valueFormatter{};
};

class DebugMenuRegistry {
 public:
  DebugMenuRegistry() = default;

  void RegisterPanel(DebugPanelDescriptor descriptor);
  bool UnregisterPanel(std::string_view id);
  [[nodiscard]] DebugPanelDescriptor* FindPanel(std::string_view id);
  [[nodiscard]] const DebugPanelDescriptor* FindPanel(std::string_view id) const;
  [[nodiscard]] bool IsPanelOpen(std::string_view id) const;
  void SetPanelOpen(std::string_view id, bool open);
  void CloseAllPanels();
  [[nodiscard]] std::vector<DebugPanelDescriptor>& Panels() { return panels_; }
  [[nodiscard]] const std::vector<DebugPanelDescriptor>& Panels() const { return panels_; }

  void RegisterAction(std::string_view menuPath, std::function<void(Registry&)> callback,
                      std::string_view shortcut = "", bool isLuaOwned = false);
  void RegisterToggle(std::string_view menuPath, std::function<bool()> getter, std::function<void(bool)> setter,
                      bool isLuaOwned = false);
  [[nodiscard]] const std::vector<DebugMenuActionDescriptor>& Actions() const { return actions_; }
  [[nodiscard]] const std::vector<DebugMenuToggleDescriptor>& Toggles() const { return toggles_; }

  void RegisterWatch(std::string_view label, std::function<std::string()> formatter, bool isLuaOwned = false);
  bool UnregisterWatch(std::string_view label);
  [[nodiscard]] const std::vector<DebugWatchDescriptor>& Watches() const { return watches_; }

  // Clears Lua-owned actions, toggles, watches, and unbinds Lua panel callbacks on full scene stop
  // while caching their open states in persisted_open_states_ so re-registration restores isOpen.
  void ClearLuaOwned();

  // Per-project persistence serialization (used by ImGuiSettingsHandler for imgui.ini and unit tests).
  void ApplyPersistedOpenState(std::string_view id, bool isOpen);
  [[nodiscard]] std::string SerializeOpenStates() const;
  void DeserializeOpenStates(std::string_view iniText);

  // Shared Inspector & Viewport Overlay State
  void SelectEntity(Entity entity) { selected_entity_ = entity; }
  [[nodiscard]] Entity GetSelectedEntity() const { return selected_entity_; }
  [[nodiscard]] bool IsWorldPickerEnabled() const { return world_picker_enabled_; }
  void SetWorldPickerEnabled(bool enabled) { world_picker_enabled_ = enabled; }
  [[nodiscard]] bool HighlightSelectedEntity() const { return highlight_selected_entity_; }
  void SetHighlightSelectedEntity(bool highlight) { highlight_selected_entity_ = highlight; }

  [[nodiscard]] bool ShowUILayoutBoxes() const { return show_ui_layout_boxes_; }
  void SetShowUILayoutBoxes(bool show) { show_ui_layout_boxes_ = show; }
  [[nodiscard]] bool ShowUIClipRects() const { return show_ui_clip_rects_; }
  void SetShowUIClipRects(bool show) { show_ui_clip_rects_ = show; }
  [[nodiscard]] bool HighlightUIFocus() const { return highlight_ui_focus_; }
  void SetHighlightUIFocus(bool highlight) { highlight_ui_focus_ = highlight; }

  [[nodiscard]] bool BuiltInsInstalled() const { return built_ins_installed_; }
  void MarkBuiltInsInstalled() { built_ins_installed_ = true; }

 private:
  std::vector<DebugPanelDescriptor> panels_{};
  std::vector<DebugMenuActionDescriptor> actions_{};
  std::vector<DebugMenuToggleDescriptor> toggles_{};
  std::vector<DebugWatchDescriptor> watches_{};
  std::unordered_map<std::string, bool> persisted_open_states_{};

  Entity selected_entity_{};
  bool world_picker_enabled_ = false;
  bool highlight_selected_entity_ = true;
  bool show_ui_layout_boxes_ = false;
  bool show_ui_clip_rects_ = false;
  bool highlight_ui_focus_ = true;
  bool built_ins_installed_ = false;
};

#ifdef OCTARINE_WITH_IMGUI
void InstallImGuiSettingsHandler(Registry& registry);
void UninstallImGuiSettingsHandler();
#endif

}  // namespace octarine::debug
