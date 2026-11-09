#pragma once

#ifdef OCTARINE_WITH_IMGUI
struct ImGuiStyle;

namespace octarine::debug {

/// Applies Octarine's signature yellowish-purple Discworld-inspired ImGui theme.
/// Menus, popups, and windows receive solid, deep cosmic purple backgrounds
/// with vibrant greenish-yellow / golden magic accents and high-contrast typography.
void ApplyOctarineTheme(ImGuiStyle* dstStyle = nullptr);

}  // namespace octarine::debug
#endif  // OCTARINE_WITH_IMGUI
