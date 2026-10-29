#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <glm/glm.hpp>
#include <string>

#include "AssetManager/AssetManager.h"
#include "AssetManager/GlyphAtlas.h"
#include "Components/SpriteComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "Components/TextLabelComponent.h"
#include "Components/UIAnchorComponent.h"
#include "Components/UIButtonComponent.h"
#include "Components/UICanvasComponent.h"
#include "Components/UIClipComponent.h"
#include "Components/UIFlexComponent.h"
#include "Components/UIFocusComponent.h"
#include "Components/UILayoutBoxComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/UIScrollComponent.h"
#include "Components/UIStyleStateComponent.h"
#include "Components/UIZIndexComponent.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "Renderer/RenderQueue.h"

namespace octarine::ui {

inline float ParseFontPixelSize(const std::string& fontId) {
  static constexpr float kDefaultFontSize = 16.0f;
  const auto dash = fontId.rfind('-');
  if (dash == std::string::npos || dash + 1 >= fontId.size()) return kDefaultFontSize;
  char* endPtr = nullptr;
  const float parsed = std::strtof(fontId.c_str() + dash + 1, &endPtr);
  return (endPtr != fontId.c_str() + dash + 1 && parsed > 0.0f) ? parsed : kDefaultFontSize;
}

inline glm::vec2 MeasureFallbackText(const std::string& text, const float fontSize) {
  static constexpr float kCharWidthFactor = 0.55f;
  const float charW = fontSize * kCharWidthFactor;
  float maxLineW = 0.0f;
  float currentLineW = 0.0f;
  int lines = 1;
  for (const char c : text) {
    if (c == '\n') {
      maxLineW = std::max(maxLineW, currentLineW);
      currentLineW = 0.0f;
      ++lines;
    } else {
      currentLineW += charW;
    }
  }
  maxLineW = std::max(maxLineW, currentLineW);
  return {std::ceil(maxLineW), std::ceil(static_cast<float>(lines) * fontSize)};
}

inline glm::vec2 MeasureFromAtlas(const GlyphAtlas& atlas, const std::string& text) {
  float totalW = 0.0f;
  for (const char c : text) {
    const auto cp = static_cast<std::uint32_t>(static_cast<unsigned char>(c));
    const auto* g = atlas.Find(cp);
    if (g == nullptr) return {-1.0f, -1.0f};
    totalW += g->advance;
  }
  return {std::ceil(totalW), static_cast<float>(std::max(1, atlas.LineSkip()))};
}

inline glm::vec2 MeasureTtfFontText(TTF_Font* font, const TextLabelComponent& label, const bool hasNewline) {
  int w = 0;
  int h = 0;
  const bool wrapped = label.wrapWidth > 0 || hasNewline;
  const int wrapLen = label.wrapWidth > 0 ? label.wrapWidth : 0;
  const bool ok = wrapped ? TTF_GetStringSizeWrapped(font, label.text.c_str(), 0, wrapLen, &w, &h)
                          : TTF_GetStringSize(font, label.text.c_str(), 0, &w, &h);
  if (!ok) return {-1.0f, -1.0f};
  return {static_cast<float>(w), static_cast<float>(h)};
}

inline glm::vec2 MeasureTextLabel(Registry* registry, const TextLabelComponent& label) {
  if (label.text.empty()) return {0.0f, 0.0f};
  const auto* assetMgr = registry->TryGet<AssetManager>();
  if (assetMgr == nullptr) {
    return MeasureFallbackText(label.text, ParseFontPixelSize(label.fontId));
  }
  const bool hasNewline = label.text.find('\n') != std::string::npos;
  if (label.wrapWidth <= 0 && !hasNewline) {
    const GlyphAtlas* atlas = assetMgr->GetGlyphAtlas(label.fontId);
    if (atlas != nullptr && atlas->IsLoaded()) {
      const glm::vec2 atlasSize = MeasureFromAtlas(*atlas, label.text);
      if (atlasSize.x >= 0.0f) return atlasSize;
    }
  }
  if (TTF_Font* font = assetMgr->GetFont(label.fontId)) {
    const glm::vec2 ttfSize = MeasureTtfFontText(font, label, hasNewline);
    if (ttfSize.x >= 0.0f) return ttfSize;
  }
  return MeasureFallbackText(label.text, ParseFontPixelSize(label.fontId));
}

inline SDL_Rect IntersectRects(const SDL_Rect& a, const SDL_Rect& b) {
  const int left = std::max(a.x, b.x);
  const int top = std::max(a.y, b.y);
  const int right = std::min(a.x + a.w, b.x + b.w);
  const int bottom = std::min(a.y + a.h, b.y + b.h);
  return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

struct ResolvedVisualColors {
  octarine::Color bg{};
  octarine::Color border{};
  octarine::Color text{};
};

struct InteractiveEntityFlags {
  bool isHovered = false;
  bool isPressed = false;
  bool isFocused = false;
  bool isChecked = false;
  bool isDisabled = false;
};

inline octarine::Color ApplyOpacityMod(octarine::Color c, const std::uint8_t opacity) {
  if (opacity == 255) return c;
  c.a = static_cast<std::uint8_t>((static_cast<unsigned int>(c.a) * opacity) / 255u);
  return c;
}

inline void ApplyStylePatch(const UIStylePatch& patch, ResolvedVisualColors& out, std::uint8_t& opacity) {
  if (patch.HasBg()) out.bg = patch.bgColor;
  if (patch.HasBorder()) out.border = patch.borderColor;
  if (patch.HasText()) out.text = patch.textColor;
  if (patch.HasOpacity()) opacity = patch.opacity;
}

inline InteractiveEntityFlags ReadInteractiveFlags(Registry* registry, const Entity entity, ResolvedVisualColors& out) {
  InteractiveEntityFlags flags{};
  if (registry->HasComponent<UIButtonComponent>(entity)) {
    const auto& btn = registry->GetComponent<UIButtonComponent>(entity);
    flags.isDisabled = !btn.isActive;
    if (btn.isActive) {
      flags.isHovered = btn.isHovered;
      flags.isPressed = btn.isPressed;
      if (flags.isHovered && btn.hasHoverColor) out.bg = btn.hoverColor;
      if (flags.isPressed && btn.hasPressColor) out.bg = btn.pressColor;
    }
  }
  if (registry->HasComponent<UIFocusComponent>(entity)) {
    const auto& foc = registry->GetComponent<UIFocusComponent>(entity);
    flags.isFocused = foc.isFocused;
    flags.isChecked = foc.isChecked;
    if (foc.isDisabled) flags.isDisabled = true;
  }
  return flags;
}

inline ResolvedVisualColors ResolveVisualState(Registry* registry, const Entity entity, const octarine::Color& baseBg,
                                               const octarine::Color& baseBorder, const octarine::Color& baseText) {
  ResolvedVisualColors out{baseBg, baseBorder, baseText};
  if (!registry || !registry->IsAlive(entity)) return out;

  const InteractiveEntityFlags flags = ReadInteractiveFlags(registry, entity, out);
  std::uint8_t opacity = 255;
  if (registry->HasComponent<UIStyleStateComponent>(entity)) {
    const auto& st = registry->GetComponent<UIStyleStateComponent>(entity);
    if (flags.isChecked) ApplyStylePatch(st.checked, out, opacity);
    if (flags.isFocused) ApplyStylePatch(st.focus, out, opacity);
    if (flags.isHovered) ApplyStylePatch(st.hover, out, opacity);
    if (flags.isPressed) ApplyStylePatch(st.press, out, opacity);
    if (flags.isDisabled) ApplyStylePatch(st.disabled, out, opacity);
  }
  out.bg = ApplyOpacityMod(out.bg, opacity);
  out.border = ApplyOpacityMod(out.border, opacity);
  out.text = ApplyOpacityMod(out.text, opacity);
  return out;
}

}  // namespace octarine::ui
