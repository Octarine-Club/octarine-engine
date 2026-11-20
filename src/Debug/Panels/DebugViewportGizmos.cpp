#include "Debug/Panels/DebugViewportGizmos.h"

#ifdef OCTARINE_WITH_IMGUI
#include <cstdio>

#include "Components/BoxColliderComponent.h"
#include "Components/CameraComponents.h"
#include "Components/CircleColliderComponent.h"
#include "Components/PositionComponent.h"
#include "Components/SpriteComponent.h"
#include "Components/UIClipComponent.h"
#include "Components/UIFocusComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/ViewportInfo.h"
#include "Debug/DebugMenuRegistry.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "Systems/UILayoutMetrics.h"
#ifndef OCTARINE_SHIPPED
#include "UI/UIPreviewer.h"
#endif
#include "imgui.h"

namespace octarine::debug {

namespace {

ImVec2 WorldToScreen(const glm::vec2& worldPos, const octarine::Rect& camView, const ViewportInfo& vp) {
  if (camView.w <= 0.0f || camView.h <= 0.0f) return {worldPos.x, worldPos.y};
  const float sx = vp.x + (worldPos.x - camView.x) * (vp.width / camView.w);
  const float sy = vp.y + (worldPos.y - camView.y) * (vp.height / camView.h);
  return {sx, sy};
}

void CheckEntityHit(Registry& registry, Entity ent, const glm::vec2& worldPos, Entity& hitEntity) {
  if (!registry.HasComponent<PositionComponent>(ent)) return;
  const glm::vec2 pos = registry.GetComponent<PositionComponent>(ent).value;

  if (registry.HasComponent<BoxColliderComponent>(ent)) {
    const auto& box = registry.GetComponent<BoxColliderComponent>(ent);
    const float minX = pos.x + box.offset.x;
    const float minY = pos.y + box.offset.y;
    const float maxX = minX + static_cast<float>(box.width);
    const float maxY = minY + static_cast<float>(box.height);
    if (worldPos.x >= minX && worldPos.x <= maxX && worldPos.y >= minY && worldPos.y <= maxY) {
      hitEntity = ent;
    }
  } else if (registry.HasComponent<CircleColliderComponent>(ent)) {
    const auto& circle = registry.GetComponent<CircleColliderComponent>(ent);
    const glm::vec2 center = pos + circle.offset;
    if (glm::distance(worldPos, center) <= circle.radius) {
      hitEntity = ent;
    }
  } else if (registry.HasComponent<SpriteComponent>(ent)) {
    const auto& spr = registry.GetComponent<SpriteComponent>(ent);
    if (worldPos.x >= pos.x && worldPos.x <= pos.x + spr.width && worldPos.y >= pos.y &&
        worldPos.y <= pos.y + spr.height) {
      hitEntity = ent;
    }
  }
}

void HandleWorldPicking(Registry& registry, DebugMenuRegistry& debugReg, const octarine::Rect& camView,
                        const ViewportInfo& vp) {
  ImGuiIO& io = ImGui::GetIO();
  const bool pickingActive = debugReg.IsWorldPickerEnabled() || io.KeyAlt;
  if (!pickingActive) return;

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse) {
    const ImVec2 mousePos = io.MousePos;
    if (mousePos.x >= vp.x && mousePos.x <= vp.x + vp.width && mousePos.y >= vp.y && mousePos.y <= vp.y + vp.height) {
      const float normX = (mousePos.x - vp.x) / vp.width;
      const float normY = (mousePos.y - vp.y) / vp.height;
      const glm::vec2 worldPos{camView.x + normX * camView.w, camView.y + normY * camView.h};

      Entity hitEntity{};
      for (const auto ent : registry.GetUserEntities()) {
        CheckEntityHit(registry, ent, worldPos, hitEntity);
      }

      if (registry.IsAlive(hitEntity)) {
        debugReg.SelectEntity(hitEntity);
        debugReg.SetPanelOpen("engine.entity_inspector", true);
      }
    }
  }
}

void DrawSelectedEntityGizmo(Registry& registry, Entity selected, const octarine::Rect& camView, const ViewportInfo& vp,
                             ImDrawList* drawList) {
  if (!registry.IsAlive(selected) || !registry.HasComponent<PositionComponent>(selected)) return;
  const glm::vec2 pos = registry.GetComponent<PositionComponent>(selected).value;
  const ImVec2 screenPos = WorldToScreen(pos, camView, vp);

  constexpr ImU32 kCrosshairColor = IM_COL32(0, 255, 255, 255);
  drawList->AddLine(ImVec2(screenPos.x - 8.0f, screenPos.y), ImVec2(screenPos.x + 8.0f, screenPos.y), kCrosshairColor,
                    2.0f);
  drawList->AddLine(ImVec2(screenPos.x, screenPos.y - 8.0f), ImVec2(screenPos.x, screenPos.y + 8.0f), kCrosshairColor,
                    2.0f);

  if (registry.HasComponent<BoxColliderComponent>(selected)) {
    const auto& box = registry.GetComponent<BoxColliderComponent>(selected);
    const ImVec2 p1 = WorldToScreen(pos + box.offset, camView, vp);
    const ImVec2 p2 = WorldToScreen(
        pos + box.offset + glm::vec2(static_cast<float>(box.width), static_cast<float>(box.height)), camView, vp);
    drawList->AddRect(p1, p2, kCrosshairColor, 0.0f, 0, 2.0f);
  } else if (registry.HasComponent<SpriteComponent>(selected)) {
    const auto& spr = registry.GetComponent<SpriteComponent>(selected);
    const ImVec2 p2 = WorldToScreen(pos + glm::vec2(spr.width, spr.height), camView, vp);
    drawList->AddRect(screenPos, p2, kCrosshairColor, 0.0f, 0, 2.0f);
  }
}

void DrawUIOverlays(Registry& registry, const DebugMenuRegistry& debugReg, const ViewportInfo& vp,
                    ImDrawList* drawList) {
  if (!debugReg.ShowUILayoutBoxes() && !debugReg.ShowUIClipRects() && !debugReg.HighlightUIFocus()) {
    return;
  }
  const auto* config = registry.TryGet<GameConfig>();
  if (config == nullptr || config->windowWidth <= 0 || config->windowHeight <= 0) return;
  const float scaleX = vp.width / static_cast<float>(config->windowWidth);
  const float scaleY = vp.height / static_cast<float>(config->windowHeight);

  auto query = registry.CreateQuery<UIRectComponent>();
  query->ForEach([&](const Entity ent, const UIRectComponent& r) {
    const ImVec2 p1{vp.x + r.left * scaleX, vp.y + r.top * scaleY};
    const ImVec2 p2{vp.x + r.right * scaleX, vp.y + r.bottom * scaleY};

    if (debugReg.ShowUILayoutBoxes()) {
      drawList->AddRect(p1, p2, IM_COL32(0, 255, 255, 180), 0.0f, 0, 1.0f);
    }
    if (debugReg.ShowUIClipRects() && registry.HasComponent<UIClipComponent>(ent)) {
      drawList->AddRect(p1, p2, IM_COL32(255, 0, 255, 220), 0.0f, 0, 2.0f);
    }
    if (debugReg.HighlightUIFocus() && registry.HasComponent<UIFocusComponent>(ent)) {
      if (registry.GetComponent<UIFocusComponent>(ent).isFocused) {
        drawList->AddRect(p1, p2, IM_COL32(255, 215, 0, 255), 0.0f, 0, 2.5f);
      }
    }
  });
}

#ifndef OCTARINE_SHIPPED
void DrawPreviewViewportAndSafeAreaGuides(const Registry& registry, const ViewportInfo& vp, ImDrawList* drawList) {
  const auto* previewer = registry.TryGet<UIPreviewer>();
  if (previewer == nullptr) return;
  const bool hasSim = previewer->HasSimulatedViewport();
  const bool showGuides = previewer->ShowSafeAreaGuides();
  if (!hasSim && !showGuides) return;

  const ImVec2 p0{vp.x, vp.y};
  const ImVec2 p1{vp.x + vp.width, vp.y + vp.height};
  if (hasSim) {
    drawList->AddRect(p0, p1, IM_COL32(120, 210, 255, 220), 0.0f, 0, 1.5f);
    const auto* cfg = registry.TryGet<GameConfig>();
    const float uiScale = cfg != nullptr ? octarine::ui::ResolveUiScale(*cfg) : 1.0f;
    char badge[96];
    std::snprintf(badge, sizeof(badge), "Sim %dx%d | %.2fx UI", previewer->SimulatedWidth(),
                  previewer->SimulatedHeight(), static_cast<double>(uiScale));
    const ImVec2 textSz = ImGui::CalcTextSize(badge);
    const ImVec2 b0{vp.x + 6.0f, vp.y + 6.0f};
    const ImVec2 b1{b0.x + textSz.x + 10.0f, b0.y + textSz.y + 6.0f};
    drawList->AddRectFilled(b0, b1, IM_COL32(16, 20, 28, 200), 3.0f);
    drawList->AddRect(b0, b1, IM_COL32(120, 210, 255, 190), 3.0f);
    drawList->AddText(ImVec2(b0.x + 5.0f, b0.y + 3.0f), IM_COL32(220, 240, 255, 255), badge);
  }
  if (!showGuides) return;

  auto drawSafeRect = [&](const float insetRatio, const ImU32 col) {
    const float ix = vp.width * insetRatio;
    const float iy = vp.height * insetRatio;
    drawList->AddRect(ImVec2(vp.x + ix, vp.y + iy), ImVec2(vp.x + vp.width - ix, vp.y + vp.height - iy), col, 0.0f, 0,
                      1.0f);
  };
  drawSafeRect(0.035f, IM_COL32(56, 189, 248, 150));  // Action-safe 93%
  drawSafeRect(0.050f, IM_COL32(250, 204, 21, 175));  // Title-safe 90%
  const float cx = vp.x + vp.width * 0.5f;
  const float cy = vp.y + vp.height * 0.5f;
  drawList->AddLine(ImVec2(cx - 10.0f, cy), ImVec2(cx + 10.0f, cy), IM_COL32(255, 255, 255, 95), 1.0f);
  drawList->AddLine(ImVec2(cx, cy - 10.0f), ImVec2(cx, cy + 10.0f), IM_COL32(255, 255, 255, 95), 1.0f);
}
#endif

}  // namespace

void DrawDebugViewportGizmos(Registry& registry) {
  auto* debugReg = registry.TryGet<DebugMenuRegistry>();
  if (debugReg == nullptr) return;

  const auto* vp = registry.TryGet<ViewportInfo>();
  if (vp == nullptr || vp->width <= 0.0f || vp->height <= 0.0f) return;

  octarine::Rect camView{0, 0, vp->width, vp->height};
  if (const auto* cam = registry.TryGet<CameraComponent>()) {
    camView = cam->viewport;
  }

  HandleWorldPicking(registry, *debugReg, camView, *vp);

  ImDrawList* drawList = ImGui::GetBackgroundDrawList();
  if (debugReg->HighlightSelectedEntity()) {
    DrawSelectedEntityGizmo(registry, debugReg->GetSelectedEntity(), camView, *vp, drawList);
  }

  DrawUIOverlays(registry, *debugReg, *vp, drawList);
#ifndef OCTARINE_SHIPPED
  DrawPreviewViewportAndSafeAreaGuides(registry, *vp, drawList);
#endif
}

}  // namespace octarine::debug

#endif  // OCTARINE_WITH_IMGUI
