#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "Systems/UILayoutMetrics.h"

class UILayoutSystem {
 public:
  void operator()(const ContextFacade& ctx) {
    auto* registry = ctx.GetRegistry();
    EnsureInitialized(registry);
    canvasQuery_->Update();

    const auto& gameConfig = registry->Get<GameConfig>();

    canvases_scratch_.clear();
    canvasQuery_->ForEach([&](const Entity canvasEntity, const UICanvasComponent& canvas) {
      const float cw = (canvas.width > 0.f) ? canvas.width : static_cast<float>(gameConfig.windowWidth);
      const float ch = (canvas.height > 0.f) ? canvas.height : static_cast<float>(gameConfig.windowHeight);
      UIRectComponent canvasRect{0.f, 0.f, cw, ch, canvas.baseLayer};
      canvasRect.visible = IsEntityVisible(registry, canvasEntity);
      canvases_scratch_.emplace_back(canvasEntity, canvasRect);
    });

    for (const auto& [canvasEntity, canvasRect] : canvases_scratch_) {
      WriteRect(registry, canvasEntity, canvasRect);
      if (canvasRect.visible) {
        ResolveChildren(registry, canvasEntity, canvasRect);
      }
    }
  }

 private:
  using CanvasQuery = ComponentQuery<UICanvasComponent>;

  void EnsureInitialized(Registry* registry) {
    if (canvasQuery_) return;
    canvasQuery_ = registry->CreateQuery<UICanvasComponent>();
  }

  static bool IsUILayoutParticipant(Registry* registry, const Entity entity) {
    return registry->HasComponent<UILayoutBoxComponent>(entity) || registry->HasComponent<UIAnchorComponent>(entity) ||
           registry->HasComponent<UIFlexComponent>(entity) || registry->HasComponent<TextLabelComponent>(entity) ||
           registry->HasComponent<SquarePrimitiveComponent>(entity) ||
           registry->HasComponent<SpriteComponent>(entity) || registry->HasComponent<UIButtonComponent>(entity) ||
           registry->HasComponent<UICanvasComponent>(entity);
  }

  static bool IsEntityVisible(Registry* registry, const Entity entity) {
    if (!IsUILayoutParticipant(registry, entity)) {
      return false;
    }
    if (registry->HasComponent<UILayoutBoxComponent>(entity)) {
      return registry->GetComponent<UILayoutBoxComponent>(entity).visible;
    }
    return true;
  }

  static void WriteRect(Registry* registry, const Entity entity, const UIRectComponent& rect) {
    if (registry->HasComponent<UIRectComponent>(entity)) {
      auto& dst = registry->GetComponent<UIRectComponent>(entity);
      const bool keepBlocksPointer = dst.blocksPointer;
      dst = rect;
      dst.blocksPointer = keepBlocksPointer;
    } else {
      registry->AddComponent(entity, rect);
    }
  }

  void MarkSubtreeHidden(Registry* registry, const Entity root) const {
    if (!IsUILayoutParticipant(registry, root)) return;
    hidden_stack_scratch_.clear();
    hidden_stack_scratch_.push_back(root);
    while (!hidden_stack_scratch_.empty()) {
      const Entity current = hidden_stack_scratch_.back();
      hidden_stack_scratch_.pop_back();
      if (!IsUILayoutParticipant(registry, current)) continue;
      UIRectComponent hiddenRect{};
      hiddenRect.visible = false;
      WriteRect(registry, current, hiddenRect);
      registry->ForEachChild(current, [&](const Entity child) { hidden_stack_scratch_.push_back(child); });
    }
  }

  static float ApplyMinMax(const float val, const float minVal, const UIUnit minUnit, const float maxVal,
                           const UIUnit maxUnit, const float avail) {
    static constexpr float kPercentScale = 0.01f;
    float out = val;
    if (minUnit == UIUnit::Px && minVal > 0.0f) out = std::max(out, minVal);
    if (minUnit == UIUnit::Percent && minVal > 0.0f) out = std::max(out, avail * minVal * kPercentScale);
    if (maxUnit == UIUnit::Px && maxVal > 0.0f) out = std::min(out, maxVal);
    if (maxUnit == UIUnit::Percent && maxVal > 0.0f) out = std::min(out, avail * maxVal * kPercentScale);
    return out;
  }

  static float ClampBoxAxis(Registry* registry, const Entity entity, const bool isHorizontal, const float size,
                            const float avail) {
    if (!registry->HasComponent<UILayoutBoxComponent>(entity)) return size;
    const auto& box = registry->GetComponent<UILayoutBoxComponent>(entity);
    return isHorizontal ? ApplyMinMax(size, box.minWidth, box.minWidthUnit, box.maxWidth, box.maxWidthUnit, avail)
                        : ApplyMinMax(size, box.minHeight, box.minHeightUnit, box.maxHeight, box.maxHeightUnit, avail);
  }

  static float MarginMain(Registry* registry, const Entity entity, const bool isRow) {
    if (!registry->HasComponent<UILayoutBoxComponent>(entity)) return 0.0f;
    const auto& b = registry->GetComponent<UILayoutBoxComponent>(entity);
    return isRow ? (b.marginLeft + b.marginRight) : (b.marginTop + b.marginBottom);
  }

  static float MarginCross(Registry* registry, const Entity entity, const bool isRow) {
    if (!registry->HasComponent<UILayoutBoxComponent>(entity)) return 0.0f;
    const auto& b = registry->GetComponent<UILayoutBoxComponent>(entity);
    return isRow ? (b.marginTop + b.marginBottom) : (b.marginLeft + b.marginRight);
  }

  static float ExplicitBoxWidthPx(Registry* registry, const Entity entity) {
    if (!registry->HasComponent<UILayoutBoxComponent>(entity)) return 0.0f;
    const auto& box = registry->GetComponent<UILayoutBoxComponent>(entity);
    if (box.widthUnit == UIUnit::Px && box.width > 0.0f) {
      return ClampBoxAxis(registry, entity, true, box.width, box.width);
    }
    if (box.maxWidthUnit == UIUnit::Px && box.maxWidth > 0.0f) {
      return box.maxWidth;
    }
    return 0.0f;
  }

  static float KnownElementWidthPx(Registry* registry, const Entity entity) {
    if (registry->HasComponent<UIRectComponent>(entity)) {
      const float rw = registry->GetComponent<UIRectComponent>(entity).Width();
      if (rw > 0.0f) return rw;
    }
    return ExplicitBoxWidthPx(registry, entity);
  }

  static int ResolveLabelWrapWidth(Registry* registry, const Entity entity, const TextLabelComponent& lbl) {
    if (lbl.wrapWidth > 0) return lbl.wrapWidth;
    if (!lbl.wrapToRect) return 0;

    const float ownW = KnownElementWidthPx(registry, entity);
    if (ownW > 0.0f) return static_cast<int>(ownW);

    float horizInset = MarginMain(registry, entity, true);
    std::optional<Entity> cur = registry->GetParent(entity);
    while (cur.has_value() && registry->IsAlive(*cur)) {
      if (registry->HasComponent<UIFlexComponent>(*cur)) {
        const auto& fl = registry->GetComponent<UIFlexComponent>(*cur);
        horizInset += fl.paddingLeft + fl.paddingRight;
      }
      const float ancW = KnownElementWidthPx(registry, *cur);
      if (ancW > 0.0f) return std::max(1, static_cast<int>(ancW - horizInset));
      horizInset += MarginMain(registry, *cur, true);
      cur = registry->GetParent(*cur);
    }
    return 0;
  }

  static float MeasureLeafIntrinsicAxis(Registry* registry, const Entity entity, const bool isHorizontal) {
    if (registry->HasComponent<TextLabelComponent>(entity)) {
      const auto& lbl = registry->GetComponent<TextLabelComponent>(entity);
      const int wrapW = ResolveLabelWrapWidth(registry, entity, lbl);
      const glm::vec2 textSz = octarine::ui::MeasureTextLabel(registry, lbl, wrapW);
      return isHorizontal ? textSz.x : textSz.y;
    }
    if (registry->HasComponent<SpriteComponent>(entity)) {
      const auto& sp = registry->GetComponent<SpriteComponent>(entity);
      const float dim = isHorizontal ? sp.width : sp.height;
      if (dim > 0.0f) return dim;
    }
    if (registry->HasComponent<SquarePrimitiveComponent>(entity)) {
      const auto& sq = registry->GetComponent<SquarePrimitiveComponent>(entity);
      const float dim = isHorizontal ? sq.width : sq.height;
      if (dim > 0.0f) return dim;
    }
    return 0.0f;
  }

  static float ResolveWrapContainerMain(Registry* registry, const Entity entity, const UIFlexComponent& flex,
                                        const bool isRow) {
    const float pad = isRow ? (flex.paddingLeft + flex.paddingRight) : (flex.paddingTop + flex.paddingBottom);
    if (registry->HasComponent<UIRectComponent>(entity)) {
      const float rSpan = isRow ? registry->GetComponent<UIRectComponent>(entity).Width()
                                : registry->GetComponent<UIRectComponent>(entity).Height();
      if (rSpan > pad) return rSpan - pad;
    }
    if (registry->HasComponent<UILayoutBoxComponent>(entity)) {
      const auto& box = registry->GetComponent<UILayoutBoxComponent>(entity);
      const UIUnit unit = isRow ? box.widthUnit : box.heightUnit;
      const float val = isRow ? box.width : box.height;
      if (unit == UIUnit::Px && val > pad) return val - pad;
    }
    return 0.0f;
  }

  // Recursive measurement of fixed-column grid cross axis during intrinsic auto-sizing.
  // NOLINTNEXTLINE(misc-no-recursion)
  static float MeasureGridColumnsCross(Registry* registry, const Entity entity, const UIFlexComponent& flex,
                                       const bool isRow, size_t& outVisCount) {
    const auto cols = static_cast<size_t>(std::max<std::uint16_t>(1, flex.columns));
    float totalCross = 0.0f;
    float lineMax = 0.0f;
    size_t colIdx = 0;
    size_t lines = 0;
    outVisCount = 0;
    registry->ForEachChild(entity, [&](const Entity child) {
      if (!IsEntityVisible(registry, child)) return;
      ++outVisCount;
      const float cCross = MeasureNodeNaturalAxis(registry, child, !isRow) + MarginCross(registry, child, isRow);
      lineMax = std::max(lineMax, cCross);
      ++colIdx;
      if (colIdx == cols) {
        totalCross += lineMax;
        ++lines;
        lineMax = 0.0f;
        colIdx = 0;
      }
    });
    if (colIdx > 0) {
      totalCross += lineMax;
      ++lines;
    }
    if (lines > 1) totalCross += flex.EffectiveCrossGap() * static_cast<float>(lines - 1);
    return totalCross;
  }

  // Recursive traversal over shallow UI element trees during intrinsic auto-sizing.
  // NOLINTNEXTLINE(misc-no-recursion)
  static float MeasureWrappedCrossIntrinsic(Registry* registry, const Entity entity, const UIFlexComponent& flex,
                                            const bool isRow, size_t& outVisCount) {
    if (flex.columns > 0) {
      return MeasureGridColumnsCross(registry, entity, flex, isRow, outVisCount);
    }

    const float crossGap = flex.EffectiveCrossGap();
    const float availMain = ResolveWrapContainerMain(registry, entity, flex, isRow);
    float totalCross = 0.0f;
    float curMain = 0.0f;
    float lineMaxCross = 0.0f;
    size_t lines = 0;
    outVisCount = 0;
    registry->ForEachChild(entity, [&](const Entity child) {
      if (!IsEntityVisible(registry, child)) return;
      ++outVisCount;
      const float cMain = MeasureNodeNaturalAxis(registry, child, isRow) + MarginMain(registry, child, isRow);
      const float cCross = MeasureNodeNaturalAxis(registry, child, !isRow) + MarginCross(registry, child, isRow);
      const float needed = (curMain > 0.0f) ? (curMain + flex.gap + cMain) : cMain;
      if (availMain > 0.0f && curMain > 0.0f && needed > availMain) {
        totalCross += lineMaxCross;
        ++lines;
        curMain = cMain;
        lineMaxCross = cCross;
      } else {
        curMain = needed;
        lineMaxCross = std::max(lineMaxCross, cCross);
      }
    });
    if (outVisCount == 0) return 0.0f;
    if (lineMaxCross > 0.0f || curMain > 0.0f) {
      totalCross += lineMaxCross;
      ++lines;
    }
    if (lines > 1) totalCross += crossGap * static_cast<float>(lines - 1);
    return totalCross;
  }

  // Recursive measurement of flex container children along either the main or cross axis.
  // NOLINTNEXTLINE(misc-no-recursion)
  static float MeasureFlexChildrenIntrinsic(Registry* registry, const Entity entity, const UIFlexComponent& flex,
                                            const bool isHorizontal, size_t& outVisCount) {
    const bool isRow = (flex.direction == FlexDirection::Row);
    const bool sameAxis = (isRow == isHorizontal);
    if (!sameAxis && (flex.wrap || flex.columns > 0)) {
      return MeasureWrappedCrossIntrinsic(registry, entity, flex, isRow, outVisCount);
    }
    outVisCount = 0;
    float accum = 0.0f;
    registry->ForEachChild(entity, [&](const Entity child) {
      if (!IsEntityVisible(registry, child)) return;
      ++outVisCount;
      const float childSize =
          MeasureNodeNaturalAxis(registry, child, isHorizontal) + MarginMain(registry, child, isHorizontal);
      accum = sameAxis ? (accum + childSize) : std::max(accum, childSize);
    });
    if (sameAxis && outVisCount > 1) {
      accum += flex.gap * static_cast<float>(outVisCount - 1);
    }
    return accum;
  }

  // Recursive intrinsic content measurement for auto-sized UI nodes.
  // NOLINTNEXTLINE(misc-no-recursion)
  static float MeasureIntrinsicAxis(Registry* registry, const Entity entity, const bool isHorizontal) {
    float pad = 0.0f;
    if (registry->HasComponent<UIFlexComponent>(entity)) {
      const auto& flex = registry->GetComponent<UIFlexComponent>(entity);
      pad = isHorizontal ? (flex.paddingLeft + flex.paddingRight) : (flex.paddingTop + flex.paddingBottom);
      size_t visCount = 0;
      const float childrenSize = MeasureFlexChildrenIntrinsic(registry, entity, flex, isHorizontal, visCount);
      if (visCount > 0) return childrenSize + pad;
    }
    return MeasureLeafIntrinsicAxis(registry, entity, isHorizontal) + pad;
  }

  static float LegacyFixedAxisSize(Registry* registry, const Entity entity, const bool isHorizontal) {
    if (registry->HasComponent<UIAnchorComponent>(entity)) {
      const auto& a = registry->GetComponent<UIAnchorComponent>(entity);
      const float frac = isHorizontal ? (a.anchorRight - a.anchorLeft) : (a.anchorBottom - a.anchorTop);
      const float span = isHorizontal ? (a.offsetRight - a.offsetLeft) : (a.offsetBottom - a.offsetTop);
      if (frac == 0.0f && span > 0.0f) return span;
    }
    if (registry->HasComponent<SquarePrimitiveComponent>(entity)) {
      const auto& s = registry->GetComponent<SquarePrimitiveComponent>(entity);
      const float dim = isHorizontal ? s.width : s.height;
      if (dim > 0.0f) return dim;
    }
    return 0.0f;
  }

  // Recursive natural size resolution falling back to MeasureIntrinsicAxis when no explicit size is set.
  // NOLINTNEXTLINE(misc-no-recursion)
  static float MeasureNodeNaturalAxis(Registry* registry, const Entity entity, const bool isHorizontal) {
    if (registry->HasComponent<UILayoutBoxComponent>(entity)) {
      const auto& box = registry->GetComponent<UILayoutBoxComponent>(entity);
      const UIUnit unit = isHorizontal ? box.widthUnit : box.heightUnit;
      const float val = isHorizontal ? box.width : box.height;
      if (unit == UIUnit::Px && val > 0.0f) return val;
    }
    const float legacy = LegacyFixedAxisSize(registry, entity, isHorizontal);
    if (legacy > 0.0f) return legacy;
    return MeasureIntrinsicAxis(registry, entity, isHorizontal);
  }

  static float ResolveChildFlexWeight(Registry* registry, const Entity child, const bool isRow) {
    if (registry->HasComponent<UILayoutBoxComponent>(child)) {
      const auto& box = registry->GetComponent<UILayoutBoxComponent>(child);
      const UIUnit unit = isRow ? box.widthUnit : box.heightUnit;
      const float val = isRow ? box.width : box.height;
      if (unit == UIUnit::Fr) return std::max(0.0f, val > 0.0f ? val : 1.0f);
      return 0.0f;
    }
    return (DesiredMainSize(registry, child, isRow, 0.0f) <= 0.0f) ? 1.0f : 0.0f;
  }

  static float ResolveBoxAxisSize(Registry* registry, const Entity child, const bool isHorizontal, const float avail,
                                  const float autoFallback) {
    static constexpr float kPercentScale = 0.01f;
    const auto& box = registry->GetComponent<UILayoutBoxComponent>(child);
    const UIUnit unit = isHorizontal ? box.widthUnit : box.heightUnit;
    const float val = isHorizontal ? box.width : box.height;
    float resolved = autoFallback;
    if (unit == UIUnit::Px) {
      resolved = val;
    } else if (unit == UIUnit::Percent) {
      resolved = avail * val * kPercentScale;
    } else if (unit == UIUnit::Auto) {
      const float intrinsic = MeasureIntrinsicAxis(registry, child, isHorizontal);
      resolved = (intrinsic > 0.0f) ? intrinsic : autoFallback;
    }
    return ClampBoxAxis(registry, child, isHorizontal, resolved, avail);
  }

  static float DesiredCrossSize(Registry* registry, const Entity child, const bool isRow, const float availCross) {
    const bool isHorizontal = !isRow;
    if (registry->HasComponent<UILayoutBoxComponent>(child)) {
      return ResolveBoxAxisSize(registry, child, isHorizontal, availCross, availCross);
    }
    const float legacy = LegacyFixedAxisSize(registry, child, isHorizontal);
    return (legacy > 0.0f) ? legacy : availCross;
  }

  static float DesiredMainSize(Registry* registry, const Entity child, const bool isRow, const float availMain) {
    if (registry->HasComponent<UILayoutBoxComponent>(child)) {
      const auto& box = registry->GetComponent<UILayoutBoxComponent>(child);
      const UIUnit unit = isRow ? box.widthUnit : box.heightUnit;
      if (unit == UIUnit::Fr) return 0.0f;
      return ResolveBoxAxisSize(registry, child, isRow, availMain, 0.0f);
    }
    return LegacyFixedAxisSize(registry, child, isRow);
  }

  static void ApplyChildClipAndLayer(Registry* registry, const Entity child, const UIRectComponent& parentRect,
                                     UIRectComponent& childRect) {
    childRect.layer = parentRect.layer;
    childRect.clipId = parentRect.clipId;
    if (registry->HasComponent<UIZIndexComponent>(child)) {
      childRect.layer += registry->GetComponent<UIZIndexComponent>(child).z;
    }
    if (!registry->HasComponent<UIClipComponent>(child)) return;
    auto& clip = registry->GetComponent<UIClipComponent>(child);
    if (!clip.enabled) return;

    SDL_Rect selfRect{static_cast<int>(std::floor(childRect.left)), static_cast<int>(std::floor(childRect.top)),
                      std::max(0, static_cast<int>(std::ceil(childRect.Width()))),
                      std::max(0, static_cast<int>(std::ceil(childRect.Height())))};
    if (auto* rq = registry->TryGet<RenderQueue>()) {
      if (parentRect.clipId != 0) {
        if (const SDL_Rect* parentClip = rq->GetClipRect(parentRect.clipId)) {
          selfRect = octarine::ui::IntersectRects(*parentClip, selfRect);
        }
      }
      clip.resolvedClip = selfRect;
      clip.clipId = rq->RegisterClipRect(selfRect);
      childRect.clipId = clip.clipId;
    } else {
      clip.resolvedClip = selfRect;
    }
    childRect.layer += 1;
  }

  static void ComputeMainStartAndStep(const UIFlexComponent& flex, const size_t count, const float availMain,
                                      const float usedMain, const float innerStart, const bool hasFlex,
                                      float& outCursor, float& outGap) {
    static constexpr float kHalf = 0.5f;
    outCursor = innerStart;
    outGap = flex.gap;
    if (hasFlex || count == 0) return;

    const float extraMain = std::max(0.0f, availMain - usedMain);
    if (flex.justify == FlexAlign::Center) {
      outCursor += extraMain * kHalf;
    } else if (flex.justify == FlexAlign::End) {
      outCursor += extraMain;
    } else if (flex.justify == FlexAlign::SpaceBetween && count > 1) {
      outGap = flex.gap + extraMain / static_cast<float>(count - 1);
    } else if (flex.justify == FlexAlign::SpaceAround && count > 0) {
      const float slot = extraMain / static_cast<float>(count);
      outCursor += slot * kHalf;
      outGap = flex.gap + slot;
    }
  }

  static bool HasExplicitCrossSize(Registry* registry, const Entity child, const bool isRow) {
    if (!registry->HasComponent<UILayoutBoxComponent>(child)) return false;
    const auto& box = registry->GetComponent<UILayoutBoxComponent>(child);
    const UIUnit unit = isRow ? box.heightUnit : box.widthUnit;
    return unit == UIUnit::Px || unit == UIUnit::Percent;
  }

  static UIRectComponent BuildFlexChildRect(Registry* registry, const Entity child, const UIRectComponent& parentRect,
                                            const UIFlexComponent& flex, const bool isRow, const float cursor,
                                            const float mainSpan, const float crossOrigin, const float availCross) {
    static constexpr float kHalf = 0.5f;
    float mLeft = 0.0f;
    float mTop = 0.0f;
    float mRight = 0.0f;
    float mBottom = 0.0f;
    if (registry->HasComponent<UILayoutBoxComponent>(child)) {
      const auto& b = registry->GetComponent<UILayoutBoxComponent>(child);
      mLeft = b.marginLeft;
      mTop = b.marginTop;
      mRight = b.marginRight;
      mBottom = b.marginBottom;
    }
    const float crossMargin = isRow ? (mTop + mBottom) : (mLeft + mRight);
    const float effAvailCross = std::max(0.0f, availCross - crossMargin);
    float crossStart = crossOrigin + (isRow ? mTop : mLeft);
    float crossSpan = effAvailCross;

    if (flex.align != FlexAlign::Stretch) {
      crossSpan = std::min(effAvailCross, DesiredCrossSize(registry, child, isRow, effAvailCross));
      const float slack = effAvailCross - crossSpan;
      if (flex.align == FlexAlign::Center) {
        crossStart += slack * kHalf;
      } else if (flex.align == FlexAlign::End) {
        crossStart += slack;
      }
    } else if (HasExplicitCrossSize(registry, child, isRow)) {
      crossSpan = DesiredCrossSize(registry, child, isRow, effAvailCross);
    } else {
      crossSpan = ClampBoxAxis(registry, child, !isRow, effAvailCross, effAvailCross);
    }

    UIRectComponent childRect = parentRect;
    if (isRow) {
      childRect.left = cursor + mLeft;
      childRect.right = childRect.left + mainSpan;
      childRect.top = crossStart;
      childRect.bottom = crossStart + crossSpan;
    } else {
      childRect.top = cursor + mTop;
      childRect.bottom = childRect.top + mainSpan;
      childRect.left = crossStart;
      childRect.right = crossStart + crossSpan;
    }
    ApplyChildClipAndLayer(registry, child, parentRect, childRect);
    return childRect;
  }

  static void ComputeFlexMainSizes(Registry* registry, const std::vector<Entity>& children, const bool isRow,
                                   const float availMain, const float totalGaps, std::vector<float>& mainSizes,
                                   std::vector<float>& weightsScratch, float& outUsedMain, bool& outHasFlex) {
    const size_t n = children.size();
    float fixedSum = 0.0f;
    float totalWeight = 0.0f;
    weightsScratch.assign(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
      const float mSpan = MarginMain(registry, children[i], isRow);
      weightsScratch[i] = ResolveChildFlexWeight(registry, children[i], isRow);
      if (weightsScratch[i] > 0.0f) {
        totalWeight += weightsScratch[i];
        fixedSum += mSpan;
      } else {
        mainSizes[i] = DesiredMainSize(registry, children[i], isRow, availMain);
        fixedSum += mainSizes[i] + mSpan;
      }
    }

    const float remaining = std::max(0.0f, availMain - fixedSum - totalGaps);
    outUsedMain = totalGaps;
    for (size_t i = 0; i < n; ++i) {
      if (weightsScratch[i] > 0.0f && totalWeight > 0.0f) {
        const float rawShare = remaining * (weightsScratch[i] / totalWeight);
        mainSizes[i] = ClampBoxAxis(registry, children[i], isRow, rawShare, availMain);
      }
      outUsedMain += mainSizes[i] + MarginMain(registry, children[i], isRow);
    }
    outHasFlex = (totalWeight > 0.0f);
  }

  static void UpdateFlexScrollOffsets(Registry* registry, const Entity parent, const bool isRow, const float vpW,
                                      const float vpH, const float usedMain, float& scrollOffsetX,
                                      float& scrollOffsetY) {
    if (!registry->HasComponent<UIScrollComponent>(parent)) return;
    auto& scroll = registry->GetComponent<UIScrollComponent>(parent);
    scroll.contentWidth = isRow ? usedMain : vpW;
    scroll.contentHeight = isRow ? vpH : usedMain;
    scroll.ClampToViewport(vpW, vpH);
    scrollOffsetX = scroll.scrollHorizontal ? scroll.scrollX : 0.0f;
    scrollOffsetY = scroll.scrollVertical ? scroll.scrollY : 0.0f;
  }

  struct WrappedChildPlacement {
    Entity child{};
    float mainStart = 0.0f;
    float mainSpan = 0.0f;
    float crossStart = 0.0f;
    float lineCrossSpan = 0.0f;
  };

  static float ResolveWrappedChildCrossSpan(Registry* registry, const Entity child, const bool isRow,
                                            const float availCross) {
    const float natural = MeasureNodeNaturalAxis(registry, child, !isRow);
    if (natural > 0.0f) return natural;
    return DesiredCrossSize(registry, child, isRow, availCross);
  }

  static void BuildGridPlacements(Registry* registry, const std::vector<Entity>& children, const UIFlexComponent& flex,
                                  const bool isRow, const float availMain, const float availCross,
                                  std::vector<WrappedChildPlacement>& outPlacements, float& outTotalCross) {
    const auto cols = static_cast<size_t>(std::max<std::uint16_t>(1, flex.columns));
    const float totalMainGaps = (cols > 1) ? flex.gap * static_cast<float>(cols - 1) : 0.0f;
    const float cellMain = std::max(0.0f, (availMain - totalMainGaps) / static_cast<float>(cols));
    const float crossGap = flex.EffectiveCrossGap();

    float crossCursor = 0.0f;
    for (size_t i = 0; i < children.size(); i += cols) {
      const size_t end = std::min(children.size(), i + cols);
      float lineMaxCross = 0.0f;
      for (size_t j = i; j < end; ++j) {
        const float cCross = ResolveWrappedChildCrossSpan(registry, children[j], isRow, availCross) +
                             MarginCross(registry, children[j], isRow);
        lineMaxCross = std::max(lineMaxCross, cCross);
      }
      for (size_t j = i; j < end; ++j) {
        const size_t colIdx = j - i;
        const Entity child = children[j];
        const float mSpan = MarginMain(registry, child, isRow);
        const float desired = DesiredMainSize(registry, child, isRow, cellMain);
        const float childMain = (desired > 0.0f) ? desired : std::max(0.0f, cellMain - mSpan);
        const float mainPos = static_cast<float>(colIdx) * (cellMain + flex.gap);
        outPlacements.push_back(WrappedChildPlacement{child, mainPos, childMain, crossCursor, lineMaxCross});
      }
      crossCursor += lineMaxCross;
      if (end < children.size()) crossCursor += crossGap;
    }
    outTotalCross = crossCursor;
  }

  static void BuildFlexWrapPlacements(Registry* registry, const std::vector<Entity>& children,
                                      const UIFlexComponent& flex, const bool isRow, const float availMain,
                                      const float availCross, std::vector<WrappedChildPlacement>& outPlacements,
                                      float& outTotalCross) {
    const float crossGap = flex.EffectiveCrossGap();
    float crossCursor = 0.0f;
    size_t idx = 0;
    while (idx < children.size()) {
      size_t lineEnd = idx;
      float lineMain = 0.0f;
      float lineMaxCross = 0.0f;
      while (lineEnd < children.size()) {
        const Entity ch = children[lineEnd];
        const float cMain = DesiredMainSize(registry, ch, isRow, availMain);
        const float mMain = MarginMain(registry, ch, isRow);
        const float step = (lineEnd > idx ? flex.gap : 0.0f) + cMain + mMain;
        if (lineEnd > idx && availMain > 0.0f && (lineMain + step) > availMain) {
          break;
        }
        lineMain += step;
        const float cCross =
            ResolveWrappedChildCrossSpan(registry, ch, isRow, availCross) + MarginCross(registry, ch, isRow);
        lineMaxCross = std::max(lineMaxCross, cCross);
        ++lineEnd;
      }

      float mainCursor = 0.0f;
      float stepGap = flex.gap;
      ComputeMainStartAndStep(flex, lineEnd - idx, availMain, lineMain, 0.0f, false, mainCursor, stepGap);
      for (size_t j = idx; j < lineEnd; ++j) {
        const Entity ch = children[j];
        const float cMain = DesiredMainSize(registry, ch, isRow, availMain);
        outPlacements.push_back(WrappedChildPlacement{ch, mainCursor, cMain, crossCursor, lineMaxCross});
        mainCursor += cMain + MarginMain(registry, ch, isRow) + stepGap;
      }
      crossCursor += lineMaxCross;
      if (lineEnd < children.size()) crossCursor += crossGap;
      idx = lineEnd;
    }
    outTotalCross = crossCursor;
  }

  struct FlexInnerViewport {
    float innerLeft = 0.0f;
    float innerTop = 0.0f;
    float vpW = 0.0f;
    float vpH = 0.0f;
    float availMain = 0.0f;
    float availCross = 0.0f;
    float mainStart = 0.0f;
    float crossStart = 0.0f;
  };

  static FlexInnerViewport ComputeFlexInnerViewport(const UIRectComponent& parentRect, const UIFlexComponent& flex,
                                                    const bool isRow) {
    const float innerLeft = parentRect.left + flex.paddingLeft;
    const float innerTop = parentRect.top + flex.paddingTop;
    const float innerRight = std::max(innerLeft, parentRect.right - flex.paddingRight);
    const float innerBottom = std::max(innerTop, parentRect.bottom - flex.paddingBottom);
    const float vpW = innerRight - innerLeft;
    const float vpH = innerBottom - innerTop;
    return FlexInnerViewport{
        innerLeft,
        innerTop,
        vpW,
        vpH,
        isRow ? vpW : vpH,
        isRow ? vpH : vpW,
        isRow ? innerLeft : innerTop,
        isRow ? innerTop : innerLeft,
    };
  }

  void CollectVisibleFlexChildren(Registry* registry, const Entity parent, std::vector<Entity>& outChildren) const {
    registry->ForEachChild(parent, [&](const Entity child) {
      if (IsEntityVisible(registry, child)) {
        outChildren.push_back(child);
      } else {
        MarkSubtreeHidden(registry, child);
      }
    });
  }

  void ResolveWrappedOrGridChildren(Registry* registry, const Entity parent, const UIRectComponent& parentRect,
                                    const UIFlexComponent& flex, const std::vector<Entity>& children,
                                    std::vector<std::pair<Entity, UIRectComponent>>& pending) const {
    const bool isRow = (flex.direction == FlexDirection::Row);
    const FlexInnerViewport vp = ComputeFlexInnerViewport(parentRect, flex, isRow);

    wrapped_placements_scratch_.clear();
    wrapped_placements_scratch_.reserve(children.size());
    const auto buildFn =
        (flex.columns > 0) ? &UILayoutSystem::BuildGridPlacements : &UILayoutSystem::BuildFlexWrapPlacements;
    float totalCross = 0.0f;
    buildFn(registry, children, flex, isRow, vp.availMain, vp.availCross, wrapped_placements_scratch_, totalCross);

    float scrollOffsetX = 0.0f;
    float scrollOffsetY = 0.0f;
    UpdateFlexScrollOffsets(registry, parent, !isRow, vp.vpW, vp.vpH, totalCross, scrollOffsetX, scrollOffsetY);

    const float mainOrigin = vp.mainStart - (isRow ? scrollOffsetX : scrollOffsetY);
    const float crossOrigin = vp.crossStart - (isRow ? scrollOffsetY : scrollOffsetX);
    for (const auto& pl : wrapped_placements_scratch_) {
      const UIRectComponent childRect =
          BuildFlexChildRect(registry, pl.child, parentRect, flex, isRow, mainOrigin + pl.mainStart, pl.mainSpan,
                             crossOrigin + pl.crossStart, pl.lineCrossSpan);
      WriteRect(registry, pl.child, childRect);
      pending.emplace_back(pl.child, childRect);
    }
  }

  void ResolveFlexChildren(Registry* registry, const Entity parent, const UIRectComponent& parentRect,
                           const UIFlexComponent& flex,
                           std::vector<std::pair<Entity, UIRectComponent>>& pending) const {
    flex_children_scratch_.clear();
    CollectVisibleFlexChildren(registry, parent, flex_children_scratch_);
    if (flex_children_scratch_.empty()) return;
    if (flex.wrap || flex.columns > 0) {
      ResolveWrappedOrGridChildren(registry, parent, parentRect, flex, flex_children_scratch_, pending);
      return;
    }

    const bool isRow = (flex.direction == FlexDirection::Row);
    const FlexInnerViewport vp = ComputeFlexInnerViewport(parentRect, flex, isRow);

    const size_t n = flex_children_scratch_.size();
    const float totalGaps = (n > 1) ? flex.gap * static_cast<float>(n - 1) : 0.0f;

    flex_sizes_scratch_.assign(n, 0.0f);
    float usedMain = 0.0f;
    bool hasFlex = false;
    ComputeFlexMainSizes(registry, flex_children_scratch_, isRow, vp.availMain, totalGaps, flex_sizes_scratch_,
                         flex_weights_scratch_, usedMain, hasFlex);

    float cursor = 0.0f;
    float stepGap = flex.gap;
    ComputeMainStartAndStep(flex, n, vp.availMain, usedMain, vp.mainStart, hasFlex, cursor, stepGap);

    float scrollOffsetX = 0.0f;
    float scrollOffsetY = 0.0f;
    UpdateFlexScrollOffsets(registry, parent, isRow, vp.vpW, vp.vpH, usedMain, scrollOffsetX, scrollOffsetY);

    const float mainScroll = isRow ? scrollOffsetX : scrollOffsetY;
    const float effCrossOrigin = vp.crossStart - (isRow ? scrollOffsetY : scrollOffsetX);
    for (size_t i = 0; i < n; ++i) {
      const Entity child = flex_children_scratch_[i];
      const UIRectComponent childRect =
          BuildFlexChildRect(registry, child, parentRect, flex, isRow, cursor - mainScroll, flex_sizes_scratch_[i],
                             effCrossOrigin, vp.availCross);
      WriteRect(registry, child, childRect);
      pending.emplace_back(child, childRect);
      cursor += flex_sizes_scratch_[i] + MarginMain(registry, child, isRow) + stepGap;
    }
  }

  static void ApplyNonFlexScrollOffset(Registry* registry, const Entity parent, UIRectComponent& childRect) {
    if (!registry->HasComponent<UIScrollComponent>(parent)) return;
    const auto& scroll = registry->GetComponent<UIScrollComponent>(parent);
    const float sx = scroll.scrollHorizontal ? scroll.scrollX : 0.0f;
    const float sy = scroll.scrollVertical ? scroll.scrollY : 0.0f;
    childRect.left -= sx;
    childRect.right -= sx;
    childRect.top -= sy;
    childRect.bottom -= sy;
  }

  static UIRectComponent ResolveNonFlexChildRect(Registry* registry, const Entity parent, const Entity child,
                                                 const UIRectComponent& parentRect) {
    UIRectComponent childRect = parentRect;
    const float pw = parentRect.Width();
    const float ph = parentRect.Height();

    if (registry->HasComponent<UIAnchorComponent>(child)) {
      const auto& anchor = registry->GetComponent<UIAnchorComponent>(child);
      childRect.left = parentRect.left + pw * anchor.anchorLeft + anchor.offsetLeft;
      childRect.top = parentRect.top + ph * anchor.anchorTop + anchor.offsetTop;
      childRect.right = parentRect.left + pw * anchor.anchorRight + anchor.offsetRight;
      childRect.bottom = parentRect.top + ph * anchor.anchorBottom + anchor.offsetBottom;
    }
    if (registry->HasComponent<UILayoutBoxComponent>(child)) {
      const auto& box = registry->GetComponent<UILayoutBoxComponent>(child);
      if (box.widthUnit != UIUnit::Auto || box.marginLeft != 0.0f) {
        const float w = DesiredMainSize(registry, child, true, pw);
        childRect.left = parentRect.left + box.marginLeft;
        if (w > 0.0f) childRect.right = childRect.left + w;
      }
      if (box.heightUnit != UIUnit::Auto || box.marginTop != 0.0f) {
        const float h = DesiredMainSize(registry, child, false, ph);
        childRect.top = parentRect.top + box.marginTop;
        if (h > 0.0f) childRect.bottom = childRect.top + h;
      }
    }
    ApplyNonFlexScrollOffset(registry, parent, childRect);
    ApplyChildClipAndLayer(registry, child, parentRect, childRect);
    return childRect;
  }

  void ResolveChildren(Registry* registry, const Entity root, const UIRectComponent& rootRect) const {
    pending_scratch_.clear();
    pending_scratch_.emplace_back(root, rootRect);
    while (!pending_scratch_.empty()) {
      auto [parent, parentRect] = pending_scratch_.back();
      pending_scratch_.pop_back();
      if (registry->HasComponent<UIFlexComponent>(parent)) {
        ResolveFlexChildren(registry, parent, parentRect, registry->GetComponent<UIFlexComponent>(parent),
                            pending_scratch_);
        continue;
      }
      registry->ForEachChild(parent, [&](const Entity child) {
        if (!IsEntityVisible(registry, child)) {
          MarkSubtreeHidden(registry, child);
          return;
        }
        const UIRectComponent childRect = ResolveNonFlexChildRect(registry, parent, child, parentRect);
        WriteRect(registry, child, childRect);
        pending_scratch_.emplace_back(child, childRect);
      });
    }
  }

  std::unique_ptr<CanvasQuery> canvasQuery_{};
  mutable std::vector<std::pair<Entity, UIRectComponent>> canvases_scratch_{};
  mutable std::vector<std::pair<Entity, UIRectComponent>> pending_scratch_{};
  mutable std::vector<Entity> hidden_stack_scratch_{};
  mutable std::vector<Entity> flex_children_scratch_{};
  mutable std::vector<float> flex_sizes_scratch_{};
  mutable std::vector<float> flex_weights_scratch_{};
  mutable std::vector<WrappedChildPlacement> wrapped_placements_scratch_{};
};
