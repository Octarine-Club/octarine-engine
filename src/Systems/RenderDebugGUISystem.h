#pragma once

#include <SDL3/SDL.h>

#include "Components/ScriptComponent.h"
#include "ECS/Registry.h"
#include "General/Logger.h"

#ifdef OCTARINE_WITH_IMGUI

#include "Components/ViewportInfo.h"
#include "imgui.h"
#include "imgui_internal.h"

struct EngineOptions;

// Per-frame ImGui pass for in-game debug overlays (the script onDebugGUI hook,
// entity count, and tilemap inspector).
class RenderDebugGUISystem {
 public:
  void operator()(const ContextFacade& context, ScriptComponent& script) const {
    if (script.onDebugGUIFunction == sol::lua_nil) {
      return;
    }
    if (auto result = script.onDebugGUIFunction(script.script_table, context.GetEntity()); !result.valid()) {
      const sol::error err = result;
      Logger::ErrorLua(std::string(err.what()));
    }
  }

  static void Render(Registry* registry, SDL_Renderer* renderer, float deltaTime);
  static inline void ReturnFocusToGame(Registry* registry) {
    if (!registry) return;
    auto& viewportInfo = registry->Get<ViewportInfo>();

    if (ImGui::GetCurrentContext()) {
      ImGuiIO& io = ImGui::GetIO();
      io.ClearInputKeys();
      io.ClearEventsQueue();
      ImGui::ClearActiveID();

      ImGui::SetWindowFocus(nullptr);
      viewportInfo.isFocused = true;
      io.WantCaptureKeyboard = false;
      io.NavActive = false;
    } else {
      viewportInfo.isFocused = true;
    }
  }
  // True while any recent script error is inside its toast window.
  static bool HasActiveScriptErrorToast();

 private:
  static void DrawDebugOverlays(Registry* registry, const EngineOptions& engineOptions, bool showGameOverlays,
                                bool projectLoaded, float deltaTime);
  static void EntityInfoWindow(const Registry* registry);
  static void ScriptErrorToastWindow();
};
#endif
