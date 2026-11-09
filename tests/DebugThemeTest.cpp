#include <cstdio>

#include "Debug/DebugTheme.h"
#include "TestHarness.h"
#include "imgui.h"

using octarine::test::Check;

int main() {
#ifdef OCTARINE_WITH_IMGUI
  std::printf("[DebugTheme] Octarine Discworld yellowish-purple palette verification\n");

  IMGUI_CHECKVERSION();
  ImGuiContext* ctx = ImGui::CreateContext();
  ImGuiStyle style;

  octarine::debug::ApplyOctarineTheme(&style);

  // Backgrounds: verify windows have translucent purple hue and menus have solid opacity
  const ImVec4& windowBg = style.Colors[ImGuiCol_WindowBg];
  Check(windowBg.w >= 0.85f && windowBg.w <= 0.90f, "WindowBg has ~86% opacity (10% translucent)");
  Check(windowBg.x > windowBg.y && windowBg.z > windowBg.y, "WindowBg has purple hue (R > G and B > G)");

  const ImVec4& menuBarBg = style.Colors[ImGuiCol_MenuBarBg];
  Check(menuBarBg.w >= 0.95f, "MenuBarBg has solid opacity (>= 0.95)");
  Check(menuBarBg.x > menuBarBg.y && menuBarBg.z > menuBarBg.y, "MenuBarBg has purple hue");

  const ImVec4& popupBg = style.Colors[ImGuiCol_PopupBg];
  Check(popupBg.w >= 0.95f, "PopupBg (menu dropdowns) has solid opacity (>= 0.95)");
  Check(popupBg.x > popupBg.y && popupBg.z > popupBg.y, "PopupBg has purple hue");

  // Octarine Yellow Accents: verify checkmarks, slider grabs, and tab lines are yellowish/golden
  const ImVec4& checkMark = style.Colors[ImGuiCol_CheckMark];
  Check(checkMark.x > 0.75f && checkMark.y > 0.75f && checkMark.z < 0.60f,
        "CheckMark is octarine yellowish (high R & G, lower B)");

  const ImVec4& sliderGrab = style.Colors[ImGuiCol_SliderGrab];
  Check(sliderGrab.x > 0.75f && sliderGrab.y > 0.70f && sliderGrab.z < 0.60f,
        "SliderGrab is octarine yellowish (high R & G, lower B)");

  const ImVec4& tabOverline = style.Colors[ImGuiCol_TabSelectedOverline];
  Check(tabOverline.x > 0.75f && tabOverline.y > 0.75f && tabOverline.z < 0.60f,
        "TabSelectedOverline is octarine yellowish accent");

  // Borders: verify borders are enabled for popup menus and windows
  Check(style.WindowBorderSize >= 1.0f, "WindowBorderSize >= 1.0f for crisp window contrast");
  Check(style.PopupBorderSize >= 1.0f, "PopupBorderSize >= 1.0f for readable menu dropdown edges");

  ImGui::DestroyContext(ctx);
  std::printf("[DebugTheme] All tests passed!\n");
  return 0;
#else
  std::printf("[DebugTheme] Skipped (OCTARINE_WITH_IMGUI off)\n");
  return 0;
#endif
}
