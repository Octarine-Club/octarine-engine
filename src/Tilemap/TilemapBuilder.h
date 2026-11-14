#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Tilemap/ChunkTemplate.h"

namespace octarine::tilemap {

struct TilesetDesc {
  std::string assetId{};
  std::string relPath{};
  float textureWidth = 256.0f;
  float textureHeight = 256.0f;
  int32_t tileGridSize = kDefaultTileSize;
};

struct TileQuadDesc {
  float localX = 0.0f;
  float localY = 0.0f;
  float srcX = 0.0f;
  float srcY = 0.0f;
  float size = static_cast<float>(kDefaultTileSize);
  float alpha = 1.0f;
  bool flipX = false;
  bool flipY = false;
};

class TilemapBuilder {
 public:
  // Half-texel UV inset prevents bilinear/nearest sampling bleeding across adjacent atlas cells.
  static constexpr float kUvInsetPx = 0.002f;

  [[nodiscard]] static bool IsPaletteTemplateName(const std::string& identifier) noexcept {
    if (identifier == "Chunk_Palette" || identifier == "Palette" || identifier == "__Palette") {
      return true;
    }
    const std::string needle = "palette";
    const auto it = std::search(
        identifier.begin(), identifier.end(), needle.begin(), needle.end(), [](const char c1, const char c2) {
          return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
        });
    return it != identifier.end();
  }

  static void ReserveLayerMesh(TileLayerMesh& mesh, const size_t additionalTileCount) {
    auto& verts = mesh.MutableVertices();
    auto& indices = mesh.MutableIndices();
    verts.reserve(verts.size() + additionalTileCount * 4u);
    indices.reserve(indices.size() + additionalTileCount * 6u);
  }

  static void AppendStaticTileQuad(TileLayerMesh& mesh, const TilesetDesc& ts, const TileQuadDesc& quad) {
    const float texW = std::max(1.0f, ts.textureWidth);
    const float texH = std::max(1.0f, ts.textureHeight);
    const float gridSize = std::max(1.0f, quad.size);
    const float inset = std::min(kUvInsetPx, gridSize * 0.25f);

    float u0 = (quad.srcX + inset) / texW;
    float v0 = (quad.srcY + inset) / texH;
    float u1 = (quad.srcX + gridSize - inset) / texW;
    float v1 = (quad.srcY + gridSize - inset) / texH;

    if (quad.flipX) {
      std::swap(u0, u1);
    }
    if (quad.flipY) {
      std::swap(v0, v1);
    }

    const float clampedAlpha = std::clamp(quad.alpha, 0.0f, 1.0f);
    const SDL_FColor color{1.0f, 1.0f, 1.0f, clampedAlpha};
    auto& verts = mesh.MutableVertices();
    auto& indices = mesh.MutableIndices();
    const auto baseIdx = static_cast<int>(verts.size());
    verts.push_back(SDL_Vertex{{quad.localX, quad.localY}, color, {u0, v0}});
    verts.push_back(SDL_Vertex{{quad.localX + gridSize, quad.localY}, color, {u1, v0}});
    verts.push_back(SDL_Vertex{{quad.localX + gridSize, quad.localY + gridSize}, color, {u1, v1}});
    verts.push_back(SDL_Vertex{{quad.localX, quad.localY + gridSize}, color, {u0, v1}});

    indices.push_back(baseIdx + 0);
    indices.push_back(baseIdx + 1);
    indices.push_back(baseIdx + 2);
    indices.push_back(baseIdx + 0);
    indices.push_back(baseIdx + 2);
    indices.push_back(baseIdx + 3);
  }

  static void AppendYSortTile(std::vector<YSortTileCmd>& outTiles, const TilesetDesc& ts, const TileQuadDesc& quad) {
    const float gridSize = std::max(1.0f, quad.size);
    YSortTileCmd cmd;
    cmd.tilesetAssetId = ts.assetId;
    cmd.tilesetRelPath = ts.relPath;
    cmd.localX = quad.localX;
    cmd.localY = quad.localY;
    cmd.width = gridSize;
    cmd.height = gridSize;
    cmd.sortLocalY = cmd.localY + gridSize;
    cmd.srcRect = SDL_FRect{quad.srcX, quad.srcY, gridSize, gridSize};

    int flipVal = SDL_FLIP_NONE;
    if (quad.flipX) flipVal |= SDL_FLIP_HORIZONTAL;
    if (quad.flipY) flipVal |= SDL_FLIP_VERTICAL;
    cmd.flip = static_cast<SDL_FlipMode>(flipVal);

    outTiles.push_back(std::move(cmd));
  }

  // Processes Y-sorted tiles bottom-up (descending localY) so contiguous vertical stacks
  // (e.g. multi-tile pillars, statues, or trees) share the bottom-most tile's sortLocalY.
  static void GroupVerticalYSortTiles(std::vector<YSortTileCmd>& tiles) {
    if (tiles.size() <= 1u) {
      return;
    }
    std::vector<size_t> order(tiles.size());
    for (size_t i = 0; i < tiles.size(); ++i) {
      order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](const size_t a, const size_t b) {
      if (std::abs(tiles[a].localY - tiles[b].localY) > 0.01f) {
        return tiles[a].localY > tiles[b].localY;
      }
      return tiles[a].localX < tiles[b].localX;
    });

    for (const size_t idx : order) {
      auto& upper = tiles[idx];
      const float targetBelowY = upper.localY + upper.height;
      for (const size_t otherIdx : order) {
        const auto& lower = tiles[otherIdx];
        if (std::abs(lower.localX - upper.localX) < 0.1f && std::abs(lower.localY - targetBelowY) < 0.1f) {
          upper.sortLocalY = std::max(upper.sortLocalY, lower.sortLocalY);
          break;
        }
      }
    }
  }

  static bool ExtractPaletteTemplates(TilemapData& data) {
    bool extracted = false;
    for (auto it = data.templates.begin(); it != data.templates.end();) {
      if (it->isPalette || IsPaletteTemplateName(it->identifier)) {
        it->isPalette = true;
        data.paletteTemplates.push_back(std::move(*it));
        it = data.templates.erase(it);
        extracted = true;
      } else {
        ++it;
      }
    }
    return extracted;
  }

  static void RecomputeGridBounds(TilemapData& data) noexcept {
    int32_t maxGridX = 0;
    int32_t maxGridY = 0;
    for (const auto& tmpl : data.templates) {
      maxGridX = std::max(maxGridX, tmpl.gridX);
      maxGridY = std::max(maxGridY, tmpl.gridY);
    }
    data.gridCols = std::max(1, maxGridX + 1);
    data.gridRows = std::max(1, maxGridY + 1);
  }

  static void FinalizeTilemapData(TilemapData& data, const bool hasExplicitBorder = false) {
    data.tileSize = std::max(1, data.tileSize);
    data.chunkGridWidth = std::clamp(data.chunkGridWidth, 1, kMaxChunkGridSize);
    data.chunkGridHeight = std::clamp(data.chunkGridHeight, 1, kMaxChunkGridSize);
    ExtractPaletteTemplates(data);
    RecomputeGridBounds(data);

    if (!hasExplicitBorder) {
      data.borderTemplate = ChunkTemplate::CreateSolidBorder(data.tileSize, data.chunkGridWidth, data.chunkGridHeight);
    }
  }

  static void ScaleSingleTemplate(ChunkTemplate& tmpl, const int32_t scaledTileSize, const float exactFactor) {
    tmpl.tileSize = scaledTileSize;
    for (auto& layer : tmpl.staticLayers) {
      for (auto& v : layer.MutableVertices()) {
        v.position.x *= exactFactor;
        v.position.y *= exactFactor;
      }
    }
    for (auto& ytile : tmpl.ysortTiles) {
      ytile.localX *= exactFactor;
      ytile.localY *= exactFactor;
      ytile.width *= exactFactor;
      ytile.height *= exactFactor;
      ytile.sortLocalY *= exactFactor;
    }
    for (auto& prop : tmpl.propSpawns) {
      prop.localX *= exactFactor;
      prop.localY *= exactFactor;
      prop.width *= exactFactor;
      prop.height *= exactFactor;
    }
  }

  static void ApplyWorldTileScale(TilemapData& data, const float rawScale) {
    const float scale = std::clamp(rawScale, 0.25f, 16.0f);
    if (std::abs(scale - 1.0f) < 1e-4f) {
      return;
    }
    const int32_t scaledTileSize =
        std::max(1, static_cast<int32_t>(std::lround(static_cast<float>(data.tileSize) * scale)));
    const float exactFactor = static_cast<float>(scaledTileSize) / static_cast<float>(std::max(1, data.tileSize));
    data.tileSize = scaledTileSize;

    for (auto& tmpl : data.templates) {
      ScaleSingleTemplate(tmpl, scaledTileSize, exactFactor);
    }
    for (auto& pal : data.paletteTemplates) {
      ScaleSingleTemplate(pal, scaledTileSize, exactFactor);
    }
    for (auto& [_, varList] : data.templateVariants) {
      for (auto& var : varList) {
        ScaleSingleTemplate(var, scaledTileSize, exactFactor);
      }
    }
    ScaleSingleTemplate(data.borderTemplate, scaledTileSize, exactFactor);
  }
};

}  // namespace octarine::tilemap
