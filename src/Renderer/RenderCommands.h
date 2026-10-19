#pragma once

#include <SDL3/SDL.h>

#include "General/BlendMode.h"
#include "General/Constants.h"

namespace octarine {

// Boundary translation from the backend-agnostic component enum to SDL's sparse constants.
inline SDL_BlendMode ToSdlBlendMode(const BlendMode mode) {
  switch (mode) {
    case BlendMode::None:
      return SDL_BLENDMODE_NONE;
    case BlendMode::Add:
      return SDL_BLENDMODE_ADD;
    case BlendMode::Mod:
      return SDL_BLENDMODE_MOD;
    case BlendMode::Mul:
      return SDL_BLENDMODE_MUL;
    case BlendMode::Blend:
    default:
      return SDL_BLENDMODE_BLEND;
  }
}

}  // namespace octarine

struct SpriteCommand {
  float destX{}, destY{}, destW{}, destH{};
  SDL_FRect srcRect{};
  float rotation{};
  SDL_FPoint pivot{};
  SDL_FlipMode flip{};
  SDL_Texture* texture{};
  // Per-draw modulation: rgb tints (multiplies) the texture, alpha fades it. 255 = no-op.
  SDL_Color colorMod{Constants::kUint8Max, Constants::kUint8Max, Constants::kUint8Max, Constants::kUint8Max};
  SDL_BlendMode blendMode{SDL_BLENDMODE_BLEND};
};

struct SquareCommand {
  SDL_FRect destRect{};
  SDL_Color color{};
  float rotation{};
  SDL_FPoint pivot{};
  SDL_BlendMode blendMode{SDL_BLENDMODE_BLEND};
};

struct TextCommand {
  SDL_FRect destRect{};
  SDL_Texture* texture{};
};

struct CircleCommand {
  SDL_FPoint center{};
  float radiusX{};
  float radiusY{};
  float rotation{};
  float thickness{1.0f};
  SDL_Color color{};
  SDL_BlendMode blendMode{SDL_BLENDMODE_BLEND};
  bool filled{true};
};

struct LineCommand {
  SDL_FPoint start{};
  SDL_FPoint end{};
  float thickness{1.0f};
  SDL_Color color{};
  SDL_BlendMode blendMode{SDL_BLENDMODE_BLEND};
};

struct TriangleCommand {
  SDL_FPoint p1{};
  SDL_FPoint p2{};
  SDL_FPoint p3{};
  float thickness{1.0f};
  SDL_Color color{};
  SDL_BlendMode blendMode{SDL_BLENDMODE_BLEND};
  bool filled{true};
};

struct CapsuleCommand {
  SDL_FPoint start{};
  SDL_FPoint end{};
  float radius{};
  float thickness{1.0f};
  SDL_Color color{};
  SDL_BlendMode blendMode{SDL_BLENDMODE_BLEND};
  bool filled{true};
};
