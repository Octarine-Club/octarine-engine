#include "./Renderer.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "General/AngleUnit.h"
#include "General/Constants.h"
#include "General/Logger.h"
#include "General/Rotation2D.h"

namespace {
constexpr Uint8 kSceneClearGrey = 24;
constexpr Uint8 kWindowClearBlack = 0;
}  // namespace

bool Renderer::CreateScene(SDL_Renderer* sdlRenderer, const int width, const int height) {
  if (sdlRenderer == nullptr) return false;
  scene_texture_ = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
  if (scene_texture_ == nullptr) {
    Logger::Error("Renderer::CreateScene SDL_CreateTexture failed: " + std::string(SDL_GetError()));
    return false;
  }
  return true;
}

void Renderer::DestroyScene() {
  if (scene_texture_ != nullptr) {
    SDL_DestroyTexture(scene_texture_);
    scene_texture_ = nullptr;
  }
}

void Renderer::BeginScene(SDL_Renderer* sdlRenderer) const {
  SDL_SetRenderTarget(sdlRenderer, scene_texture_);
  SDL_SetRenderDrawColor(sdlRenderer, kSceneClearGrey, kSceneClearGrey, kSceneClearGrey, Constants::kUint8Max);
  SDL_RenderClear(sdlRenderer);
}

void Renderer::EndScene(SDL_Renderer* sdlRenderer) const {
  SDL_SetRenderTarget(sdlRenderer, nullptr);
  SDL_SetRenderDrawColor(sdlRenderer, kWindowClearBlack, kWindowClearBlack, kWindowClearBlack, Constants::kUint8Max);
  SDL_RenderClear(sdlRenderer);
}

void Renderer::CompositeSceneToWindow(SDL_Renderer* sdlRenderer) const {
  if (scene_texture_ != nullptr) {
    SDL_RenderTexture(sdlRenderer, scene_texture_, nullptr, nullptr);
  }
}

void Renderer::Present(SDL_Renderer* sdlRenderer) const { SDL_RenderPresent(sdlRenderer); }

bool Renderer::CaptureScene(SDL_Renderer* sdlRenderer, const std::string& path) const {
  if (scene_texture_ == nullptr) {
    Logger::Error("Renderer::CaptureScene: no scene texture to capture");
    return false;
  }
  // Read back from the scene target (the off-screen texture the frame was drawn into). In bench
  // mode the window backbuffer is never composited, so the scene texture is the only place the
  // rendered frame lives.
  SDL_SetRenderTarget(sdlRenderer, scene_texture_);
  SDL_Surface* surface = SDL_RenderReadPixels(sdlRenderer, nullptr);
  SDL_SetRenderTarget(sdlRenderer, nullptr);
  if (surface == nullptr) {
    Logger::Error("Renderer::CaptureScene SDL_RenderReadPixels failed: " + std::string(SDL_GetError()));
    return false;
  }
  const bool ok = SDL_SaveBMP(surface, path.c_str());
  if (!ok) {
    Logger::Error("Renderer::CaptureScene SDL_SaveBMP failed: " + std::string(SDL_GetError()));
  }
  SDL_DestroySurface(surface);
  return ok;
}

namespace {

inline SDL_FColor ToSdlFColor(const SDL_Color& c) {
  return {
      static_cast<float>(c.r) / 255.0f,
      static_cast<float>(c.g) / 255.0f,
      static_cast<float>(c.b) / 255.0f,
      static_cast<float>(c.a) / 255.0f,
  };
}

void DrawSprite(SDL_Renderer* renderer, const SpriteCommand& cmd) {
  const SDL_FRect destRect = {cmd.destX, cmd.destY, cmd.destW, cmd.destH};
  const float deg = cmd.rotation * octarine::kRadiansToDegrees;
  SDL_SetTextureColorMod(cmd.texture, cmd.colorMod.r, cmd.colorMod.g, cmd.colorMod.b);
  SDL_SetTextureAlphaMod(cmd.texture, cmd.colorMod.a);
  SDL_SetTextureBlendMode(cmd.texture, cmd.blendMode);
  SDL_RenderTextureRotated(renderer, cmd.texture, &cmd.srcRect, &destRect, deg, &cmd.pivot, cmd.flip);
}

void DrawSquare(SDL_Renderer* renderer, const SquareCommand& cmd) {
  SDL_SetRenderDrawBlendMode(renderer, cmd.blendMode);
  if (cmd.rotation == 0.0F) {
    SDL_SetRenderDrawColor(renderer, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_RenderFillRect(renderer, &cmd.destRect);
    return;
  }
  const glm::vec2 centre = {cmd.destRect.x + cmd.pivot.x, cmd.destRect.y + cmd.pivot.y};
  const glm::vec2 topLeft = {-cmd.pivot.x, -cmd.pivot.y};
  const glm::vec2 bottomRight = {cmd.destRect.w - cmd.pivot.x, cmd.destRect.h - cmd.pivot.y};
  const auto rot = octarine::Rotation2D::FromRadians(cmd.rotation);
  const SDL_FColor fcol = ToSdlFColor(cmd.color);
  const glm::vec2 offsets[4] = {topLeft, {bottomRight.x, topLeft.y}, bottomRight, {topLeft.x, bottomRight.y}};
  SDL_Vertex verts[4];
  for (int i = 0; i < 4; ++i) {
    const glm::vec2 p = centre + octarine::Rotate(offsets[i], rot);
    verts[i].position = {p.x, p.y};
    verts[i].color = fcol;
    verts[i].tex_coord = {0.0f, 0.0f};
  }
  constexpr int indices[6] = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
}

void DrawText(SDL_Renderer* renderer, const TextCommand& cmd) {
  SDL_SetTextureColorMod(cmd.texture, cmd.colorMod.r, cmd.colorMod.g, cmd.colorMod.b);
  SDL_SetTextureAlphaMod(cmd.texture, cmd.colorMod.a);
  SDL_RenderTexture(renderer, cmd.texture, nullptr, &cmd.destRect);
}

void DrawLine(SDL_Renderer* renderer, const LineCommand& cmd) {
  SDL_SetRenderDrawBlendMode(renderer, cmd.blendMode);
  SDL_SetRenderDrawColor(renderer, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
  if (cmd.thickness <= 1.0f) {
    SDL_RenderLine(renderer, cmd.start.x, cmd.start.y, cmd.end.x, cmd.end.y);
    return;
  }
  const glm::vec2 d{cmd.end.x - cmd.start.x, cmd.end.y - cmd.start.y};
  const float len = glm::length(d);
  if (len < 0.0001f) return;

  const glm::vec2 n = glm::vec2{-d.y, d.x} * (cmd.thickness * 0.5f / len);
  const SDL_FColor fcol = ToSdlFColor(cmd.color);
  const glm::vec2 start{cmd.start.x, cmd.start.y};
  const glm::vec2 end{cmd.end.x, cmd.end.y};
  const SDL_Vertex verts[4] = {
      {{start.x + n.x, start.y + n.y}, fcol, {0.0f, 0.0f}},
      {{start.x - n.x, start.y - n.y}, fcol, {0.0f, 0.0f}},
      {{end.x - n.x, end.y - n.y}, fcol, {0.0f, 0.0f}},
      {{end.x + n.x, end.y + n.y}, fcol, {0.0f, 0.0f}},
  };
  constexpr int indices[6] = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
}

template <int MaxVerts>
void DrawRingStrip(SDL_Renderer* renderer, const SDL_FPoint* outerPts, const SDL_FPoint* innerPts, const int count,
                   const SDL_Color& color) {
  const SDL_FColor fcol = ToSdlFColor(color);
  SDL_Vertex verts[MaxVerts * 2];
  int indices[MaxVerts * 6];
  for (int i = 0; i < count; ++i) {
    verts[2 * i] = {outerPts[i], fcol, {0.0f, 0.0f}};
    verts[2 * i + 1] = {innerPts[i], fcol, {0.0f, 0.0f}};
    const int next = (i + 1 == count) ? 0 : (i + 1);
    indices[6 * i + 0] = 2 * i;
    indices[6 * i + 1] = 2 * i + 1;
    indices[6 * i + 2] = 2 * next;
    indices[6 * i + 3] = 2 * next;
    indices[6 * i + 4] = 2 * i + 1;
    indices[6 * i + 5] = 2 * next + 1;
  }
  SDL_RenderGeometry(renderer, nullptr, verts, count * 2, indices, count * 6);
}

void DrawFilledCircle(SDL_Renderer* renderer, const CircleCommand& cmd, const int segments,
                      const octarine::Rotation2D stepRot, const bool rotateEllipse, const octarine::Rotation2D rot) {
  constexpr int kMaxSegments = 64;
  const SDL_FColor fcol = ToSdlFColor(cmd.color);
  SDL_Vertex verts[kMaxSegments + 1];
  verts[0] = {{cmd.center.x, cmd.center.y}, fcol, {0.0f, 0.0f}};
  glm::vec2 unitDir{1.0f, 0.0f};
  for (int i = 0; i < segments; ++i) {
    glm::vec2 offset{cmd.radiusX * unitDir.x, cmd.radiusY * unitDir.y};
    if (rotateEllipse) offset = octarine::Rotate(offset, rot);
    verts[i + 1] = {{cmd.center.x + offset.x, cmd.center.y + offset.y}, fcol, {0.0f, 0.0f}};
    unitDir = octarine::Rotate(unitDir, stepRot);
  }
  int indices[kMaxSegments * 3];
  for (int i = 0; i < segments; ++i) {
    indices[3 * i] = 0;
    indices[3 * i + 1] = i + 1;
    indices[3 * i + 2] = (i + 1 == segments) ? 1 : (i + 2);
  }
  SDL_RenderGeometry(renderer, nullptr, verts, segments + 1, indices, segments * 3);
}

void DrawOutlineCircle(SDL_Renderer* renderer, const CircleCommand& cmd, const int segments,
                       const octarine::Rotation2D stepRot, const bool rotateEllipse, const octarine::Rotation2D rot) {
  constexpr int kMaxSegments = 64;
  if (cmd.thickness <= 1.0f) {
    SDL_SetRenderDrawColor(renderer, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_FPoint points[kMaxSegments + 1];
    glm::vec2 unitDir{1.0f, 0.0f};
    for (int i = 0; i < segments; ++i) {
      glm::vec2 offset{cmd.radiusX * unitDir.x, cmd.radiusY * unitDir.y};
      if (rotateEllipse) offset = octarine::Rotate(offset, rot);
      points[i] = {cmd.center.x + offset.x, cmd.center.y + offset.y};
      unitDir = octarine::Rotate(unitDir, stepRot);
    }
    points[segments] = points[0];
    SDL_RenderLines(renderer, points, segments + 1);
    return;
  }
  const float halfT = cmd.thickness * 0.5f;
  const float outRx = cmd.radiusX + halfT;
  const float outRy = cmd.radiusY + halfT;
  const float inRx = std::max(0.0f, cmd.radiusX - halfT);
  const float inRy = std::max(0.0f, cmd.radiusY - halfT);
  SDL_FPoint outerPts[kMaxSegments];
  SDL_FPoint innerPts[kMaxSegments];
  glm::vec2 unitDir{1.0f, 0.0f};
  for (int i = 0; i < segments; ++i) {
    glm::vec2 outOff{outRx * unitDir.x, outRy * unitDir.y};
    glm::vec2 inOff{inRx * unitDir.x, inRy * unitDir.y};
    if (rotateEllipse) {
      outOff = octarine::Rotate(outOff, rot);
      inOff = octarine::Rotate(inOff, rot);
    }
    outerPts[i] = {cmd.center.x + outOff.x, cmd.center.y + outOff.y};
    innerPts[i] = {cmd.center.x + inOff.x, cmd.center.y + inOff.y};
    unitDir = octarine::Rotate(unitDir, stepRot);
  }
  DrawRingStrip<kMaxSegments>(renderer, outerPts, innerPts, segments, cmd.color);
}

void DrawCircle(SDL_Renderer* renderer, const CircleCommand& cmd) {
  if (cmd.radiusX <= 0.0f || cmd.radiusY <= 0.0f) return;
  SDL_SetRenderDrawBlendMode(renderer, cmd.blendMode);

  constexpr int kMaxSegments = 64;
  constexpr int kMinSegments = 16;
  const float maxR = std::max(cmd.radiusX, cmd.radiusY);
  const int segments = std::clamp(static_cast<int>(maxR * 0.75f), kMinSegments, kMaxSegments);
  const float step = (2.0f * std::numbers::pi_v<float>) / static_cast<float>(segments);
  const auto stepRot = octarine::Rotation2D::FromRadians(step);
  // Isotropic circles are rotationally invariant, so only rotate vertices when radii differ.
  const bool rotateEllipse = (cmd.rotation != 0.0f) && (cmd.radiusX != cmd.radiusY);
  const auto rot = rotateEllipse ? octarine::Rotation2D::FromRadians(cmd.rotation) : octarine::Rotation2D{};

  if (cmd.filled) {
    DrawFilledCircle(renderer, cmd, segments, stepRot, rotateEllipse, rot);
  } else {
    DrawOutlineCircle(renderer, cmd, segments, stepRot, rotateEllipse, rot);
  }
}

void DrawTriangle(SDL_Renderer* renderer, const TriangleCommand& cmd) {
  SDL_SetRenderDrawBlendMode(renderer, cmd.blendMode);
  if (cmd.filled) {
    const SDL_FColor fcol = ToSdlFColor(cmd.color);
    const SDL_Vertex verts[3] = {
        {cmd.p1, fcol, {0.0f, 0.0f}},
        {cmd.p2, fcol, {0.0f, 0.0f}},
        {cmd.p3, fcol, {0.0f, 0.0f}},
    };
    constexpr int indices[3] = {0, 1, 2};
    SDL_RenderGeometry(renderer, nullptr, verts, 3, indices, 3);
    return;
  }
  if (cmd.thickness <= 1.0f) {
    SDL_SetRenderDrawColor(renderer, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    const SDL_FPoint points[4] = {cmd.p1, cmd.p2, cmd.p3, cmd.p1};
    SDL_RenderLines(renderer, points, 4);
    return;
  }
  const glm::vec2 v[3] = {{cmd.p1.x, cmd.p1.y}, {cmd.p2.x, cmd.p2.y}, {cmd.p3.x, cmd.p3.y}};
  const float twiceArea = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x);
  if (std::abs(twiceArea) < 0.0001f) {
    SDL_SetRenderDrawColor(renderer, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    const SDL_FPoint points[4] = {cmd.p1, cmd.p2, cmd.p3, cmd.p1};
    SDL_RenderLines(renderer, points, 4);
    return;
  }
  const float sign = (twiceArea >= 0.0f) ? 1.0f : -1.0f;
  glm::vec2 normals[3];
  for (int i = 0; i < 3; ++i) {
    const glm::vec2 edge = v[(i + 1) % 3] - v[i];
    const float len = glm::length(edge);
    normals[i] = (len > 0.0001f) ? (sign * glm::vec2{edge.y, -edge.x} / len) : glm::vec2{0.0f, 0.0f};
  }
  SDL_FPoint outerPts[3];
  SDL_FPoint innerPts[3];
  for (int i = 0; i < 3; ++i) {
    const glm::vec2 b = normals[(i + 2) % 3] + normals[i];
    // For unit edge normals, dot(b, n_i) == 0.5 * dot(b, b); clamping dot(b, b) >= 0.5 caps sharp miters.
    const glm::vec2 miter = b * (cmd.thickness / std::max(0.5f, glm::dot(b, b)));
    outerPts[i] = {v[i].x + miter.x, v[i].y + miter.y};
    innerPts[i] = {v[i].x - miter.x, v[i].y - miter.y};
  }
  DrawRingStrip<3>(renderer, outerPts, innerPts, 3, cmd.color);
}

void BuildCapsuleContour(const glm::vec2 p1, const glm::vec2 p2, const glm::vec2 dir, const int arcSegs,
                         const octarine::Rotation2D stepRot, glm::vec2* outCenters, glm::vec2* outNormals) {
  glm::vec2 unitDir{dir.y, -dir.x};
  for (int i = 0; i <= arcSegs; ++i) {
    outCenters[i] = p2;
    outNormals[i] = unitDir;
    unitDir = octarine::Rotate(unitDir, stepRot);
  }
  unitDir = {-dir.y, dir.x};
  for (int i = 0; i <= arcSegs; ++i) {
    outCenters[arcSegs + 1 + i] = p1;
    outNormals[arcSegs + 1 + i] = unitDir;
    unitDir = octarine::Rotate(unitDir, stepRot);
  }
}

void DrawCapsule(SDL_Renderer* renderer, const CapsuleCommand& cmd) {
  SDL_SetRenderDrawBlendMode(renderer, cmd.blendMode);
  if (cmd.radius <= 0.0f) {
    DrawLine(renderer, LineCommand{cmd.start, cmd.end, cmd.thickness, cmd.color, cmd.blendMode});
    return;
  }
  const glm::vec2 p1{cmd.start.x, cmd.start.y};
  const glm::vec2 p2{cmd.end.x, cmd.end.y};
  const glm::vec2 d = p2 - p1;
  const float len = glm::length(d);
  if (len < 0.0001f) {
    DrawCircle(renderer, CircleCommand{cmd.start, cmd.radius, cmd.radius, 0.0f, cmd.thickness, cmd.color, cmd.blendMode,
                                       cmd.filled});
    return;
  }

  constexpr int kArcSegs = 16;
  constexpr int kTotalVerts = (kArcSegs + 1) * 2;
  const auto stepRot = octarine::Rotation2D::FromRadians(std::numbers::pi_v<float> / static_cast<float>(kArcSegs));
  glm::vec2 centers[kTotalVerts];
  glm::vec2 normals[kTotalVerts];
  BuildCapsuleContour(p1, p2, d / len, kArcSegs, stepRot, centers, normals);

  if (!cmd.filled) {
    if (cmd.thickness <= 1.0f) {
      SDL_SetRenderDrawColor(renderer, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
      SDL_FPoint points[kTotalVerts + 1];
      for (int i = 0; i < kTotalVerts; ++i) {
        points[i] = {centers[i].x + cmd.radius * normals[i].x, centers[i].y + cmd.radius * normals[i].y};
      }
      points[kTotalVerts] = points[0];
      SDL_RenderLines(renderer, points, kTotalVerts + 1);
      return;
    }
    const float halfT = cmd.thickness * 0.5f;
    const float outR = cmd.radius + halfT;
    const float inR = std::max(0.0f, cmd.radius - halfT);
    SDL_FPoint outerPts[kTotalVerts];
    SDL_FPoint innerPts[kTotalVerts];
    for (int i = 0; i < kTotalVerts; ++i) {
      outerPts[i] = {centers[i].x + outR * normals[i].x, centers[i].y + outR * normals[i].y};
      innerPts[i] = {centers[i].x + inR * normals[i].x, centers[i].y + inR * normals[i].y};
    }
    DrawRingStrip<kTotalVerts>(renderer, outerPts, innerPts, kTotalVerts, cmd.color);
    return;
  }

  const SDL_FColor fcol = ToSdlFColor(cmd.color);
  const glm::vec2 midpoint = (p1 + p2) * 0.5f;
  SDL_Vertex verts[kTotalVerts + 1];
  verts[0] = {{midpoint.x, midpoint.y}, fcol, {0.0f, 0.0f}};
  for (int i = 0; i < kTotalVerts; ++i) {
    const glm::vec2 pt = centers[i] + cmd.radius * normals[i];
    verts[i + 1] = {{pt.x, pt.y}, fcol, {0.0f, 0.0f}};
  }
  int indices[kTotalVerts * 3];
  for (int i = 0; i < kTotalVerts; ++i) {
    indices[3 * i] = 0;
    indices[3 * i + 1] = i + 1;
    indices[3 * i + 2] = (i + 1 == kTotalVerts) ? 1 : (i + 2);
  }
  SDL_RenderGeometry(renderer, nullptr, verts, kTotalVerts + 1, indices, kTotalVerts * 3);
}

}  // namespace

void Renderer::DrawQueue(const RenderQueue& renderQueue, SDL_Renderer* renderer) const {
  for (const RenderKey& key : renderQueue) {
    switch (key.type) {
      case SPRITE:
        DrawSprite(renderer, key.payload.sprite);
        break;
      case SQUARE_PRIMITIVE:
        DrawSquare(renderer, key.payload.square);
        break;
      case TEXT:
        DrawText(renderer, key.payload.text);
        break;
      case CIRCLE_PRIMITIVE:
        DrawCircle(renderer, key.payload.circle);
        break;
      case LINE_PRIMITIVE:
        DrawLine(renderer, key.payload.line);
        break;
      case TRIANGLE_PRIMITIVE:
        DrawTriangle(renderer, key.payload.triangle);
        break;
      case CAPSULE_PRIMITIVE:
        DrawCapsule(renderer, key.payload.capsule);
        break;
      default:
        Logger::Error("Unknown renderable type");
        break;
    }
  }
}
