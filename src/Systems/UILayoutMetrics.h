#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <glm/glm.hpp>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>

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

inline constexpr float kMinUiScale = 0.25f;
inline constexpr float kMaxUiScale = 4.0f;

[[nodiscard]] inline float ClampUserUiScale(const float scale) noexcept {
  if (!std::isfinite(scale) || scale <= 0.0f) return 1.0f;
  return std::clamp(scale, kMinUiScale, kMaxUiScale);
}

[[nodiscard]] inline float ResolveAutoUiScale(const GameConfig& cfg) noexcept {
  if ((!cfg.HasExplicitBaseResolution() && !cfg.HasLoadedConfig()) || cfg.windowWidth <= 0 || cfg.windowHeight <= 0) {
    return 1.0f;
  }
  const int baseW = cfg.GetBaseWidth();
  const int baseH = cfg.GetBaseHeight();
  if (baseW <= 0 || baseH <= 0) return 1.0f;
  const float sx = static_cast<float>(cfg.windowWidth) / static_cast<float>(baseW);
  const float sy = static_cast<float>(cfg.windowHeight) / static_cast<float>(baseH);
  const float raw = std::min(sx, sy);
  return (std::isfinite(raw) && raw > 0.0f) ? std::clamp(raw, kMinUiScale, kMaxUiScale) : 1.0f;
}

[[nodiscard]] inline float ResolveUiScale(const GameConfig& cfg) noexcept {
  return std::clamp(ResolveAutoUiScale(cfg) * ClampUserUiScale(cfg.GetEngineOptions().uiScale), kMinUiScale,
                    kMaxUiScale);
}

[[nodiscard]] inline float ResolveUiScale(const Registry* registry) noexcept {
  if (registry == nullptr) return 1.0f;
  const auto* cfg = registry->TryGet<GameConfig>();
  return cfg != nullptr ? ResolveUiScale(*cfg) : 1.0f;
}

inline float ParseFontPixelSize(const std::string& fontId) {
  static constexpr float kFallbackFontPixelSize = 16.0f;
  const auto dash = fontId.rfind('-');
  if (dash == std::string::npos || dash + 1 >= fontId.size()) return kFallbackFontPixelSize;
  char* endPtr = nullptr;
  const float parsed = std::strtof(fontId.c_str() + dash + 1, &endPtr);
  return (endPtr != fontId.c_str() + dash + 1 && parsed > 0.0f) ? parsed : kFallbackFontPixelSize;
}

inline bool IsUtf8ContinuationByte(const char c) {
  static constexpr unsigned char kUtf8ContMask = 0xC0u;
  static constexpr unsigned char kUtf8ContTag = 0x80u;
  return (static_cast<unsigned char>(c) & kUtf8ContMask) == kUtf8ContTag;
}

inline void FlushWordToWrappedLine(const float wordW, const float spaceW, const float maxW, float& currentLineW,
                                   float& maxLineW, int& lines) {
  if (wordW <= 0.0f) return;
  const float neededSpace = (currentLineW > 0.0f) ? spaceW : 0.0f;
  if (maxW > 0.0f && currentLineW > 0.0f && (currentLineW + neededSpace + wordW) > maxW) {
    maxLineW = std::max(maxLineW, currentLineW);
    currentLineW = wordW;
    ++lines;
  } else {
    currentLineW += neededSpace + wordW;
  }
}

inline glm::vec2 MeasureFallbackText(const std::string& text, const float fontSize, const int wrapWidth = 0) {
  static constexpr float kCharWidthFactor = 0.55f;
  const float charW = fontSize * kCharWidthFactor;
  const float maxW = wrapWidth > 0 ? static_cast<float>(wrapWidth) : 0.0f;
  float maxLineW = 0.0f;
  float currentLineW = 0.0f;
  float wordW = 0.0f;
  int lines = 1;
  for (const char c : text) {
    if (IsUtf8ContinuationByte(c)) continue;
    if (c == '\n') {
      FlushWordToWrappedLine(wordW, charW, maxW, currentLineW, maxLineW, lines);
      wordW = 0.0f;
      maxLineW = std::max(maxLineW, currentLineW);
      currentLineW = 0.0f;
      ++lines;
    } else if (c == ' ' || c == '\t') {
      FlushWordToWrappedLine(wordW, charW, maxW, currentLineW, maxLineW, lines);
      wordW = 0.0f;
    } else {
      wordW += charW;
    }
  }
  FlushWordToWrappedLine(wordW, charW, maxW, currentLineW, maxLineW, lines);
  maxLineW = std::max(maxLineW, currentLineW);
  if (maxW > 0.0f) maxLineW = std::min(maxLineW, maxW);
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

inline int QuantizeFontScaleKey(const float scale) {
  constexpr int kMinScaleKey = 10;
  constexpr int kMaxScaleKey = 800;
  constexpr int kUnitScaleMin = 99;
  constexpr int kUnitScaleMax = 101;
  const int key = std::clamp(static_cast<int>(std::lround(scale * 100.0F)), kMinScaleKey, kMaxScaleKey);
  return (key >= kUnitScaleMin && key <= kUnitScaleMax) ? 100 : key;
}

inline int ResolveIntegerAtlasScale(const float scale) {
  const float rounded = std::round(scale);
  if (rounded >= 1.0F && rounded <= 4.0F && std::abs(scale - rounded) <= 0.01F) {
    return static_cast<int>(rounded);
  }
  return 0;
}

inline glm::vec2 MeasureTtfFontText(const AssetManager& assetMgr, const TextLabelComponent& label, const float effScale,
                                    const bool hasNewline, const int effWrapWidth) {
  TTF_Font* baseFont = assetMgr.GetFont(label.fontId);
  if (baseFont == nullptr) return {-1.0f, -1.0f};

  struct CacheKeyView {
    const void* baseFont = nullptr;
    std::uint64_t fontGen = 0;
    int baseSizeKey = 0;
    int scaleKey = 100;
    int wrapLen = 0;
    bool wrapped = false;
    std::string_view fontId{};
    std::string_view text{};
  };
  struct CacheKey {
    const void* baseFont = nullptr;
    std::uint64_t fontGen = 0;
    int baseSizeKey = 0;
    int scaleKey = 100;
    int wrapLen = 0;
    bool wrapped = false;
    std::string fontId{};
    std::string text{};
    operator CacheKeyView() const noexcept {
      return {baseFont, fontGen, baseSizeKey, scaleKey, wrapLen, wrapped, fontId, text};
    }
  };
  struct CacheKeyPtrHash {
    using is_transparent = void;
    static std::size_t HashView(const CacheKeyView& k) noexcept {
      std::size_t h = std::hash<const void*>{}(k.baseFont);
      h ^= std::hash<std::uint64_t>{}(k.fontGen) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<int>{}(k.baseSizeKey) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<int>{}(k.scaleKey) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<int>{}(k.wrapLen) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<bool>{}(k.wrapped) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<std::string_view>{}(k.fontId) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<std::string_view>{}(k.text) + 0x9e3779b9u + (h << 6) + (h >> 2);
      return h;
    }
    std::size_t operator()(const CacheKey* k) const noexcept { return HashView(*k); }
    std::size_t operator()(const CacheKeyView& k) const noexcept { return HashView(k); }
  };
  struct CacheKeyPtrEq {
    using is_transparent = void;
    static bool EqView(const CacheKeyView& a, const CacheKeyView& b) noexcept {
      return a.baseFont == b.baseFont && a.fontGen == b.fontGen && a.baseSizeKey == b.baseSizeKey &&
             a.scaleKey == b.scaleKey && a.wrapLen == b.wrapLen && a.wrapped == b.wrapped && a.fontId == b.fontId &&
             a.text == b.text;
    }
    bool operator()(const CacheKey* a, const CacheKey* b) const noexcept { return EqView(*a, *b); }
    bool operator()(const CacheKey* a, const CacheKeyView& b) const noexcept { return EqView(*a, b); }
    bool operator()(const CacheKeyView& a, const CacheKey* b) const noexcept { return EqView(a, *b); }
  };
  struct CacheNode {
    CacheKey key;
    glm::vec2 size;
  };
  static constexpr std::size_t kMaxCacheEntries = 1024;
  static std::list<CacheNode> s_lru;
  static std::unordered_map<const CacheKey*, std::list<CacheNode>::iterator, CacheKeyPtrHash, CacheKeyPtrEq> s_map;

  const bool wrapped = effWrapWidth > 0 || hasNewline;
  const int wrapLen = effWrapWidth > 0 ? effWrapWidth : 0;
  const CacheKeyView view{baseFont,
                          assetMgr.FontGeneration(),
                          static_cast<int>(std::lround(TTF_GetFontSize(baseFont) * 100.0F)),
                          QuantizeFontScaleKey(effScale),
                          wrapLen,
                          wrapped,
                          label.fontId,
                          label.text};
  if (const auto it = s_map.find(view); it != s_map.end()) {
    s_lru.splice(s_lru.begin(), s_lru, it->second);
    return it->second->size;
  }

  TTF_Font* scaledFont = assetMgr.GetFontScaled(label.fontId, effScale);
  if (scaledFont == nullptr) return {-1.0f, -1.0f};
  int w = 0;
  int h = 0;
  const bool ok = wrapped ? TTF_GetStringSizeWrapped(scaledFont, label.text.c_str(), 0, wrapLen, &w, &h)
                          : TTF_GetStringSize(scaledFont, label.text.c_str(), 0, &w, &h);
  if (!ok) return {-1.0f, -1.0f};
  const glm::vec2 result{static_cast<float>(w), static_cast<float>(h)};
  if (s_lru.size() >= kMaxCacheEntries) {
    s_map.erase(&s_lru.back().key);
    s_lru.pop_back();
  }
  CacheKey ownedKey{view.baseFont, view.fontGen, view.baseSizeKey, view.scaleKey,
                    view.wrapLen,  view.wrapped, label.fontId,     label.text};
  s_lru.push_front(CacheNode{std::move(ownedKey), result});
  s_map.emplace(&s_lru.front().key, s_lru.begin());
  return result;
}

inline glm::vec2 TryMeasureGlyphAtlas(const AssetManager& assetMgr, const TextLabelComponent& label,
                                      const float effScale, const int effWrap, const bool hasNewline) {
  if (effWrap > 0 || hasNewline) {
    return {-1.0f, -1.0f};
  }
  const int intScale = ResolveIntegerAtlasScale(effScale);
  if (intScale == 0) {
    return {-1.0f, -1.0f};
  }
  if (intScale > 1 && assetMgr.GetEffectiveScaleMode() == SDL_SCALEMODE_LINEAR) {
    return {-1.0f, -1.0f};
  }
  const GlyphAtlas* atlas = assetMgr.GetGlyphAtlas(label.fontId);
  if (atlas == nullptr || !atlas->IsLoaded()) {
    return {-1.0f, -1.0f};
  }
  const glm::vec2 base = MeasureFromAtlas(*atlas, label.text);
  if (base.x < 0.0f) {
    return base;
  }
  return base * static_cast<float>(intScale);
}

inline glm::vec2 MeasureTextLabel(Registry* registry, const TextLabelComponent& label, const int wrapWidthOverride = 0,
                                  const float uiScale = 1.0f) {
  if (label.text.empty()) return {0.0f, 0.0f};
  const float effScale = (std::isfinite(uiScale) && uiScale > 0.0f) ? uiScale : 1.0f;
  const int effWrap = label.wrapWidth > 0
                          ? std::max(1, static_cast<int>(std::lround(static_cast<float>(label.wrapWidth) * effScale)))
                          : wrapWidthOverride;
  const auto* assetMgr = registry != nullptr ? registry->TryGet<AssetManager>() : nullptr;
  if (assetMgr == nullptr) {
    return MeasureFallbackText(label.text, ParseFontPixelSize(label.fontId) * effScale, effWrap);
  }
  const bool hasNewline = label.text.find('\n') != std::string::npos;
  const glm::vec2 atlasSize = TryMeasureGlyphAtlas(*assetMgr, label, effScale, effWrap, hasNewline);
  if (atlasSize.x >= 0.0f) {
    return atlasSize;
  }
  const glm::vec2 ttfSize = MeasureTtfFontText(*assetMgr, label, effScale, hasNewline, effWrap);
  if (ttfSize.x >= 0.0f) return ttfSize;
  return MeasureFallbackText(label.text, ParseFontPixelSize(label.fontId) * effScale, effWrap);
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
  float borderWidth = -1.0f;
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
  if (patch.HasBorderWidth()) out.borderWidth = static_cast<float>(patch.borderWidth);
  if (patch.HasText()) out.text = patch.textColor;
  if (patch.HasOpacity()) opacity = patch.opacity;
}

// When a widget is checked and hovered/pressed without an explicit :checked:hover or :checked:press
// background override, synthesize a subtle luminance delta on the checked background while still
// honoring any non-background properties (border, text, opacity) from the base :hover/:press rule.
inline constexpr int kCheckedHoverBgDelta = 24;

inline octarine::Color OffsetColorRgb(octarine::Color c, const int delta) noexcept {
  c.r = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.r) + delta, 0, 255));
  c.g = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.g) + delta, 0, 255));
  c.b = static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.b) + delta, 0, 255));
  return c;
}

inline void ApplyNonBgStylePatch(const UIStylePatch& patch, ResolvedVisualColors& out, std::uint8_t& opacity) {
  if (patch.HasBorder()) out.border = patch.borderColor;
  if (patch.HasBorderWidth()) out.borderWidth = static_cast<float>(patch.borderWidth);
  if (patch.HasText()) out.text = patch.textColor;
  if (patch.HasOpacity()) opacity = patch.opacity;
}

inline void ApplyCheckedVisualState(const UIStyleStateComponent& st, const InteractiveEntityFlags& flags,
                                    ResolvedVisualColors& out, std::uint8_t& opacity) {
  ApplyStylePatch(st.checked, out, opacity);
  if (flags.isFocused) ApplyStylePatch(st.focus, out, opacity);
  if (flags.isHovered) {
    if (st.checkedHover.flags != 0) {
      ApplyStylePatch(st.checkedHover, out, opacity);
    } else {
      ApplyNonBgStylePatch(st.hover, out, opacity);
      out.bg = OffsetColorRgb(out.bg, kCheckedHoverBgDelta);
    }
  }
  if (flags.isPressed) {
    if (st.checkedPress.flags != 0) {
      ApplyStylePatch(st.checkedPress, out, opacity);
    } else if (st.press.flags != 0) {
      ApplyNonBgStylePatch(st.press, out, opacity);
      out.bg = OffsetColorRgb(out.bg, -kCheckedHoverBgDelta);
    }
  }
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
    if (foc.isInert) {
      flags.isDisabled = foc.isDisabled;
    } else if (foc.isDisabled) {
      flags.isDisabled = true;
    }
  }
  return flags;
}

inline void ApplyUncheckedInteractivePatch(const UIStyleStateComponent& st, const InteractiveEntityFlags& flags,
                                           ResolvedVisualColors& out, std::uint8_t& opacity) {
  if (flags.isFocused) ApplyStylePatch(st.focus, out, opacity);
  if (flags.isHovered) ApplyStylePatch(st.hover, out, opacity);
  if (flags.isPressed) ApplyStylePatch(st.press, out, opacity);
}

inline ResolvedVisualColors ResolveVisualState(Registry* registry, const Entity entity, const octarine::Color& baseBg,
                                               const octarine::Color& baseBorder, const octarine::Color& baseText) {
  ResolvedVisualColors out{baseBg, baseBorder, baseText};
  if (!registry || !registry->IsAlive(entity)) return out;

  const InteractiveEntityFlags flags = ReadInteractiveFlags(registry, entity, out);
  std::uint8_t opacity = 255;
  if (registry->HasComponent<UIStyleStateComponent>(entity)) {
    const auto& st = registry->GetComponent<UIStyleStateComponent>(entity);
    if (flags.isChecked)
      ApplyCheckedVisualState(st, flags, out, opacity);
    else
      ApplyUncheckedInteractivePatch(st, flags, out, opacity);
    if (flags.isDisabled) ApplyStylePatch(st.disabled, out, opacity);
  }
  out.bg = ApplyOpacityMod(out.bg, opacity);
  out.border = ApplyOpacityMod(out.border, opacity);
  out.text = ApplyOpacityMod(out.text, opacity);
  return out;
}

}  // namespace octarine::ui
