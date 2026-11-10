#include "Renderer/RenderQueueStats.h"

#include <algorithm>
#include <cmath>

#include "Renderer/RenderCommands.h"
#include "Renderer/RenderKey.h"
#include "Renderer/RenderQueue.h"

namespace {

constexpr float kHairlineScaleThreshold = 1.05F;

enum class Topology : uint8_t { Triangles, Lines, None };

struct CommandGeometry {
  Topology topology = Topology::None;
  const SDL_Texture* texture = nullptr;
  SDL_BlendMode blendMode = SDL_BLENDMODE_NONE;
  std::size_t vertexCount = 0;
  std::size_t indexCount = 0;
  bool valid = false;
};

CommandGeometry ClassifySprite(const SpriteCommand& cmd) noexcept {
  return CommandGeometry{Topology::Triangles, cmd.texture, cmd.blendMode, 4, 6, true};
}

CommandGeometry ClassifySquare(const SquareCommand& cmd) noexcept {
  return CommandGeometry{Topology::Triangles, nullptr, cmd.blendMode, 4, 6, true};
}

CommandGeometry ClassifyText(const TextCommand& cmd) noexcept {
  return CommandGeometry{Topology::Triangles, cmd.texture, SDL_BLENDMODE_BLEND, 4, 6, true};
}

CommandGeometry ClassifyLine(const LineCommand& cmd, const float renderScale) noexcept {
  const float effectiveThickness = std::max(cmd.thickness, 1.0F);
  if (effectiveThickness * renderScale <= kHairlineScaleThreshold) {
    return CommandGeometry{Topology::Lines, nullptr, cmd.blendMode, 2, 0, true};
  }
  const float dx = cmd.end.x - cmd.start.x;
  const float dy = cmd.end.y - cmd.start.y;
  if ((dx * dx + dy * dy) < 0.000001F) return CommandGeometry{};
  return CommandGeometry{Topology::Triangles, nullptr, cmd.blendMode, 4, 6, true};
}

CommandGeometry ClassifyCircle(const CircleCommand& cmd, const float renderScale) noexcept {
  if (cmd.radiusX <= 0.0F || cmd.radiusY <= 0.0F) return CommandGeometry{};
  constexpr int kMaxSegments = 64;
  constexpr int kMinSegments = 16;
  const float maxR = std::max(cmd.radiusX, cmd.radiusY) * std::max(1.0F, renderScale);
  const int segments = std::clamp(static_cast<int>(maxR * 0.75F), kMinSegments, kMaxSegments);

  if (cmd.filled) {
    return CommandGeometry{Topology::Triangles,
                           nullptr,
                           cmd.blendMode,
                           static_cast<std::size_t>(segments + 1),
                           static_cast<std::size_t>(segments * 3),
                           true};
  }
  const float effectiveThickness = std::max(cmd.thickness, 1.0F);
  if (effectiveThickness * renderScale <= kHairlineScaleThreshold) {
    return CommandGeometry{Topology::Lines, nullptr, cmd.blendMode, static_cast<std::size_t>(segments + 1), 0, true};
  }
  return CommandGeometry{Topology::Triangles,
                         nullptr,
                         cmd.blendMode,
                         static_cast<std::size_t>(segments * 2),
                         static_cast<std::size_t>(segments * 6),
                         true};
}

CommandGeometry ClassifyTriangle(const TriangleCommand& cmd, const float renderScale) noexcept {
  if (cmd.filled) {
    return CommandGeometry{Topology::Triangles, nullptr, cmd.blendMode, 3, 3, true};
  }
  const float effectiveThickness = std::max(cmd.thickness, 1.0F);
  if (effectiveThickness * renderScale <= kHairlineScaleThreshold) {
    return CommandGeometry{Topology::Lines, nullptr, cmd.blendMode, 4, 0, true};
  }
  const float twiceArea = (cmd.p2.x - cmd.p1.x) * (cmd.p3.y - cmd.p1.y) - (cmd.p2.y - cmd.p1.y) * (cmd.p3.x - cmd.p1.x);
  if (std::abs(twiceArea) < 0.0001F) {
    return CommandGeometry{Topology::Lines, nullptr, cmd.blendMode, 4, 0, true};
  }
  return CommandGeometry{Topology::Triangles, nullptr, cmd.blendMode, 6, 18, true};
}

CommandGeometry ClassifyCapsule(const CapsuleCommand& cmd, const float renderScale) noexcept {
  if (cmd.radius <= 0.0F) {
    return ClassifyLine(LineCommand{cmd.start, cmd.end, cmd.thickness, cmd.color, cmd.blendMode}, renderScale);
  }
  const float dx = cmd.end.x - cmd.start.x;
  const float dy = cmd.end.y - cmd.start.y;
  if ((dx * dx + dy * dy) < 0.000001F) {
    return ClassifyCircle(
        CircleCommand{cmd.start, cmd.radius, cmd.radius, 0.0F, cmd.thickness, cmd.color, cmd.blendMode, cmd.filled},
        renderScale);
  }
  constexpr int kTotalVerts = 34;  // (16 + 1) * 2
  if (cmd.filled) {
    return CommandGeometry{Topology::Triangles,
                           nullptr,
                           cmd.blendMode,
                           static_cast<std::size_t>(kTotalVerts + 1),
                           static_cast<std::size_t>(kTotalVerts * 3),
                           true};
  }
  const float effectiveThickness = std::max(cmd.thickness, 1.0F);
  if (effectiveThickness * renderScale <= kHairlineScaleThreshold) {
    return CommandGeometry{Topology::Lines, nullptr, cmd.blendMode, static_cast<std::size_t>(kTotalVerts + 1), 0, true};
  }
  return CommandGeometry{Topology::Triangles,
                         nullptr,
                         cmd.blendMode,
                         static_cast<std::size_t>(kTotalVerts * 2),
                         static_cast<std::size_t>(kTotalVerts * 6),
                         true};
}

CommandGeometry ClassifyTileChunk(const TileChunkCommand& cmd) noexcept {
  if (cmd.vertices == nullptr || cmd.vertexCount <= 0) return CommandGeometry{};
  return CommandGeometry{Topology::Triangles,
                         cmd.texture,
                         cmd.blendMode,
                         static_cast<std::size_t>(cmd.vertexCount),
                         static_cast<std::size_t>(cmd.indexCount),
                         true};
}

CommandGeometry ClassifyCommand(const RenderKey& key, const float renderScale) noexcept {
  switch (key.type) {
    case SPRITE:
      return ClassifySprite(key.payload.sprite);
    case SQUARE_PRIMITIVE:
      return ClassifySquare(key.payload.square);
    case TEXT:
      return ClassifyText(key.payload.text);
    case LINE_PRIMITIVE:
      return ClassifyLine(key.payload.line, renderScale);
    case CIRCLE_PRIMITIVE:
      return ClassifyCircle(key.payload.circle, renderScale);
    case TRIANGLE_PRIMITIVE:
      return ClassifyTriangle(key.payload.triangle, renderScale);
    case CAPSULE_PRIMITIVE:
      return ClassifyCapsule(key.payload.capsule, renderScale);
    case TILE_CHUNK:
      return ClassifyTileChunk(key.payload.tileChunk);
    default:
      return CommandGeometry{};
  }
}

struct BatchTracker {
  const SDL_Texture* texture = nullptr;
  SDL_BlendMode blendMode = SDL_BLENDMODE_NONE;
  uint8_t clipId = 0;
  Topology topology = Topology::None;
  bool isFirst = true;

  void ProcessCommand(const CommandGeometry& geom, const uint8_t nextClipId, RenderQueueStats& stats) noexcept {
    bool isSwitch = false;
    if (isFirst) {
      isSwitch = true;
      isFirst = false;
    } else {
      if (nextClipId != clipId) {
        ++stats.clipSwitches;
        isSwitch = true;
      }
      if (geom.texture != texture) {
        ++stats.textureSwitches;
        isSwitch = true;
      }
      if (geom.blendMode != blendMode) {
        ++stats.blendSwitches;
        isSwitch = true;
      }
      if (geom.topology != topology) {
        ++stats.topologySwitches;
        isSwitch = true;
      }
    }

    if (isSwitch) {
      ++stats.drawBatches;
      clipId = nextClipId;
      texture = geom.texture;
      blendMode = geom.blendMode;
      topology = geom.topology;
    }
  }
};

}  // namespace

RenderQueueStats AnalyzeRenderQueue(const RenderQueue& queue, const float renderScale) noexcept {
  RenderQueueStats stats;
  BatchTracker tracker;

  for (const RenderKey& key : queue) {
    const auto geom = ClassifyCommand(key, renderScale);
    if (!geom.valid) continue;

    tracker.ProcessCommand(geom, key.clipId, stats);
    ++stats.commandCount;
    stats.totalVertices += geom.vertexCount;
    stats.totalIndices += geom.indexCount;
  }

  if (stats.drawBatches > 0) {
    stats.keysPerBatch = static_cast<double>(stats.commandCount) / static_cast<double>(stats.drawBatches);
  }
  return stats;
}
