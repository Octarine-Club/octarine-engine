#pragma once

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "Components/SquarePrimitiveComponent.h"
#include "Components/UIAnchorComponent.h"
#include "Components/UICanvasComponent.h"
#include "Components/UIFlexComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/UIZIndexComponent.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"

class UILayoutSystem {
 public:
  void operator()(const ContextFacade& ctx) {
    auto* registry = ctx.GetRegistry();
    EnsureInitialized(registry);
    canvasQuery_->Update();

    const auto& gameConfig = registry->Get<GameConfig>();

    canvasQuery_->ForEach([&](const Entity canvasEntity, const UICanvasComponent& canvas) {
      const float cw = (canvas.width > 0.f) ? canvas.width : static_cast<float>(gameConfig.windowWidth);
      const float ch = (canvas.height > 0.f) ? canvas.height : static_cast<float>(gameConfig.windowHeight);
      const UIRectComponent canvasRect{0.f, 0.f, cw, ch, canvas.baseLayer};
      WriteRect(registry, canvasEntity, canvasRect);
      ResolveChildren(registry, canvasEntity, canvasRect);
    });
  }

 private:
  using CanvasQuery = ComponentQuery<UICanvasComponent>;

  void EnsureInitialized(Registry* registry) {
    if (canvasQuery_) return;
    canvasQuery_ = registry->CreateQuery<UICanvasComponent>();
  }

  static void WriteRect(Registry* registry, const Entity entity, const UIRectComponent& rect) {
    if (registry->HasComponent<UIRectComponent>(entity)) {
      registry->GetComponent<UIRectComponent>(entity) = rect;
    } else {
      registry->AddComponent(entity, rect);
    }
  }

  static float DesiredCrossSize(Registry* registry, const Entity child, const bool isRow, const float availCross) {
    if (registry->HasComponent<UIAnchorComponent>(child)) {
      const auto& a = registry->GetComponent<UIAnchorComponent>(child);
      const float span = isRow ? (a.offsetBottom - a.offsetTop) : (a.offsetRight - a.offsetLeft);
      if (span > 0.0f) return span;
    }
    if (registry->HasComponent<SquarePrimitiveComponent>(child)) {
      const auto& s = registry->GetComponent<SquarePrimitiveComponent>(child);
      const float dim = isRow ? s.height : s.width;
      if (dim > 0.0f) return dim;
    }
    return availCross;
  }

  static float DesiredMainSize(Registry* registry, const Entity child, const bool isRow) {
    if (registry->HasComponent<UIAnchorComponent>(child)) {
      const auto& a = registry->GetComponent<UIAnchorComponent>(child);
      const float frac = isRow ? (a.anchorRight - a.anchorLeft) : (a.anchorBottom - a.anchorTop);
      const float span = isRow ? (a.offsetRight - a.offsetLeft) : (a.offsetBottom - a.offsetTop);
      if (frac == 0.0f && span > 0.0f) return span;
    }
    if (registry->HasComponent<SquarePrimitiveComponent>(child)) {
      const auto& s = registry->GetComponent<SquarePrimitiveComponent>(child);
      const float dim = isRow ? s.width : s.height;
      if (dim > 0.0f) return dim;
    }
    return 0.0f;  // 0 indicates flexible item (shares remaining main-axis space equally)
  }

  static void ResolveFlexChildren(Registry* registry, const Entity parent, const UIRectComponent& parentRect,
                                  const UIFlexComponent& flex,
                                  std::vector<std::pair<Entity, UIRectComponent>>& pending) {
    std::vector<Entity> children;
    registry->ForEachChild(parent, [&](const Entity child) { children.push_back(child); });
    if (children.empty()) return;

    const bool isRow = (flex.direction == FlexDirection::Row);
    const float innerLeft = parentRect.left + flex.paddingLeft;
    const float innerTop = parentRect.top + flex.paddingTop;
    const float innerRight = std::max(innerLeft, parentRect.right - flex.paddingRight);
    const float innerBottom = std::max(innerTop, parentRect.bottom - flex.paddingBottom);
    const float availW = innerRight - innerLeft;
    const float availH = innerBottom - innerTop;
    const float availMain = isRow ? availW : availH;
    const float availCross = isRow ? availH : availW;

    const size_t n = children.size();
    const float totalGaps = (n > 1) ? flex.gap * static_cast<float>(n - 1) : 0.0f;

    float fixedSum = 0.0f;
    size_t flexCount = 0;
    std::vector<float> mainSizes(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
      const float d = DesiredMainSize(registry, children[i], isRow);
      if (d > 0.0f) {
        mainSizes[i] = d;
        fixedSum += d;
      } else {
        ++flexCount;
      }
    }

    const float remaining = std::max(0.0f, availMain - fixedSum - totalGaps);
    const float flexShare = (flexCount > 0) ? (remaining / static_cast<float>(flexCount)) : 0.0f;
    for (size_t i = 0; i < n; ++i) {
      if (mainSizes[i] <= 0.0f) mainSizes[i] = flexShare;
    }

    float usedMain = totalGaps;
    for (const float s : mainSizes) usedMain += s;
    const float extraMain = std::max(0.0f, availMain - usedMain);
    float cursor = (isRow ? innerLeft : innerTop);
    if (flexCount == 0) {
      if (flex.justify == FlexAlign::Center)
        cursor += extraMain * 0.5f;
      else if (flex.justify == FlexAlign::End)
        cursor += extraMain;
    }

    for (size_t i = 0; i < n; ++i) {
      const Entity child = children[i];
      const float mainSpan = mainSizes[i];
      float crossStart = isRow ? innerTop : innerLeft;
      float crossSpan = availCross;
      if (flex.align != FlexAlign::Stretch) {
        crossSpan = std::min(availCross, DesiredCrossSize(registry, child, isRow, availCross));
        const float slack = availCross - crossSpan;
        if (flex.align == FlexAlign::Center)
          crossStart += slack * 0.5f;
        else if (flex.align == FlexAlign::End)
          crossStart += slack;
      }

      UIRectComponent childRect = parentRect;
      if (isRow) {
        childRect.left = cursor;
        childRect.right = cursor + mainSpan;
        childRect.top = crossStart;
        childRect.bottom = crossStart + crossSpan;
      } else {
        childRect.top = cursor;
        childRect.bottom = cursor + mainSpan;
        childRect.left = crossStart;
        childRect.right = crossStart + crossSpan;
      }
      if (registry->HasComponent<UIZIndexComponent>(child)) {
        childRect.layer += registry->GetComponent<UIZIndexComponent>(child).z;
      }
      WriteRect(registry, child, childRect);
      pending.emplace_back(child, childRect);
      cursor += mainSpan + flex.gap;
    }
  }

  void ResolveChildren(Registry* registry, const Entity root, const UIRectComponent& rootRect) const {
    std::vector<std::pair<Entity, UIRectComponent>> pending;
    pending.emplace_back(root, rootRect);
    while (!pending.empty()) {
      auto [parent, parentRect] = pending.back();
      pending.pop_back();
      if (registry->HasComponent<UIFlexComponent>(parent)) {
        ResolveFlexChildren(registry, parent, parentRect, registry->GetComponent<UIFlexComponent>(parent), pending);
        continue;
      }
      registry->ForEachChild(parent, [&](const Entity child) {
        UIRectComponent childRect = parentRect;  // inherit layer from parent
        bool modified = false;
        if (registry->HasComponent<UIAnchorComponent>(child)) {
          const auto& anchor = registry->GetComponent<UIAnchorComponent>(child);
          const float pw = parentRect.Width();
          const float ph = parentRect.Height();
          childRect.left = parentRect.left + pw * anchor.anchorLeft + anchor.offsetLeft;
          childRect.top = parentRect.top + ph * anchor.anchorTop + anchor.offsetTop;
          childRect.right = parentRect.left + pw * anchor.anchorRight + anchor.offsetRight;
          childRect.bottom = parentRect.top + ph * anchor.anchorBottom + anchor.offsetBottom;
          modified = true;
        }
        if (registry->HasComponent<UIZIndexComponent>(child)) {
          childRect.layer += registry->GetComponent<UIZIndexComponent>(child).z;
          modified = true;
        }
        if (modified) {
          WriteRect(registry, child, childRect);
        }
        pending.emplace_back(child, childRect);
      });
    }
  }

  std::unique_ptr<CanvasQuery> canvasQuery_;
};
