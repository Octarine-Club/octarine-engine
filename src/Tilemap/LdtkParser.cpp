#include "Tilemap/LdtkParser.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_map>

#include "AssetManager/AssetManager.h"
#include "Engine/SdlFileReader.h"
#include "General/Logger.h"
#include "Tilemap/TilemapBuilder.h"
#include "Tilemap/TilemapMath.h"

namespace octarine::tilemap {

namespace {

using Json = nlohmann::json;

struct TilesetDef {
  int32_t uid = 0;
  TilesetDesc desc{};
};

struct LevelRawCoord {
  int32_t worldX = 0;
  int32_t worldY = 0;
  int32_t pxWid = kDefaultChunkGridSize * kDefaultTileSize;
  int32_t pxHei = kDefaultChunkGridSize * kDefaultTileSize;
};

struct ParsedLevelRaw {
  ChunkTemplate tmpl{};
  LevelRawCoord coord{};
  int32_t rawGridW = kDefaultChunkGridSize;
  int32_t rawGridH = kDefaultChunkGridSize;
  std::vector<uint8_t> rawCollisions{};
};

std::string ResolveRelativePath(const std::string& baseDir, const std::string& relPath) {
  if (relPath.empty()) {
    return {};
  }
  if (baseDir.empty()) {
    return std::filesystem::path(relPath).lexically_normal().generic_string();
  }
  return (std::filesystem::path(baseDir) / relPath).lexically_normal().generic_string();
}

std::unordered_map<int32_t, TilesetDef> ParseTilesetDefs(const Json& root, const std::string& ldtkDirRelPath,
                                                         TilemapData& outData) {
  std::unordered_map<int32_t, TilesetDef> defs;
  if (!root.contains("defs") || !root["defs"].is_object()) {
    return defs;
  }
  const Json& defsObj = root["defs"];
  if (!defsObj.contains("tilesets") || !defsObj["tilesets"].is_array()) {
    return defs;
  }
  for (const auto& ts : defsObj["tilesets"]) {
    if (!ts.is_object()) continue;
    TilesetDef def;
    def.uid = ts.value("uid", 0);
    const std::string rawRel = ts.value("relPath", std::string{});
    def.desc.relPath = ResolveRelativePath(ldtkDirRelPath, rawRel);
    def.desc.assetId = ts.value("identifier", std::string{});
    if (def.desc.assetId.empty() && !rawRel.empty()) {
      def.desc.assetId = std::filesystem::path(rawRel).stem().string();
    }
    def.desc.textureWidth = std::max(1.0f, static_cast<float>(ts.value("pxWid", 256)));
    def.desc.textureHeight = std::max(1.0f, static_cast<float>(ts.value("pxHei", 256)));
    def.desc.tileGridSize = std::max(1, ts.value("tileGridSize", kDefaultTileSize));
    defs[def.uid] = def;
    if (!def.desc.assetId.empty()) {
      outData.tilesetAssets.emplace_back(def.desc.assetId, def.desc.relPath);
    }
  }
  return defs;
}

TilesetDef ResolveLayerTileset(const Json& layer, const std::unordered_map<int32_t, TilesetDef>& tilesets,
                               const std::string& ldtkDirRelPath) {
  int32_t uid = layer.value("__tilesetDefUid", -1);
  if (uid < 0 && layer.contains("tilesetDefUid") && !layer["tilesetDefUid"].is_null()) {
    uid = layer.value("tilesetDefUid", -1);
  }
  if (const auto it = tilesets.find(uid); it != tilesets.end()) {
    return it->second;
  }
  TilesetDef fallback;
  fallback.uid = uid;
  const std::string rawRel = layer.value("__tilesetRelPath", std::string{});
  fallback.desc.relPath = ResolveRelativePath(ldtkDirRelPath, rawRel);
  fallback.desc.assetId =
      rawRel.empty() ? std::string{"default_tileset"} : std::filesystem::path(rawRel).stem().string();
  fallback.desc.tileGridSize = std::max(1, layer.value("__gridSize", kDefaultTileSize));
  return fallback;
}

void AppendTileQuad(TileLayerMesh& mesh, const Json& tile, const TilesetDef& ts, const float gridSize,
                    const float layerOpacity) {
  if (!tile.contains("px") || !tile.contains("src") || tile["px"].size() < 2 || tile["src"].size() < 2) {
    return;
  }
  const int32_t flipBits = tile.value("f", 0);
  TileQuadDesc quad;
  quad.localX = static_cast<float>(tile["px"][0].get<int32_t>());
  quad.localY = static_cast<float>(tile["px"][1].get<int32_t>());
  quad.srcX = static_cast<float>(tile["src"][0].get<int32_t>());
  quad.srcY = static_cast<float>(tile["src"][1].get<int32_t>());
  quad.size = gridSize;
  quad.alpha = tile.value("a", 1.0f) * layerOpacity;
  quad.flipX = (flipBits & 1) != 0;
  quad.flipY = (flipBits & 2) != 0;
  TilemapBuilder::AppendStaticTileQuad(mesh, ts.desc, quad);
}

void ParseStaticTileLayer(const Json& layer, const TilesetDef& ts, const unsigned int renderLayer,
                          const float renderDepth, ChunkTemplate& outTemplate) {
  TileLayerMesh mesh;
  mesh.layerName = layer.value("__identifier", std::string{"Ground"});
  mesh.tilesetAssetId = ts.desc.assetId;
  mesh.tilesetRelPath = ts.desc.relPath;
  mesh.renderLayer = renderLayer;
  mesh.renderDepth = renderDepth;

  const auto gridSize = static_cast<float>(std::max(1, layer.value("__gridSize", ts.desc.tileGridSize)));
  const float opacity = layer.value("__opacity", 1.0f);

  size_t tileCount = 0;
  if (layer.contains("autoLayerTiles") && layer["autoLayerTiles"].is_array()) {
    tileCount += layer["autoLayerTiles"].size();
  }
  if (layer.contains("gridTiles") && layer["gridTiles"].is_array()) {
    tileCount += layer["gridTiles"].size();
  }
  TilemapBuilder::ReserveLayerMesh(mesh, tileCount);

  if (layer.contains("autoLayerTiles") && layer["autoLayerTiles"].is_array()) {
    for (const auto& tile : layer["autoLayerTiles"]) {
      AppendTileQuad(mesh, tile, ts, gridSize, opacity);
    }
  }
  if (layer.contains("gridTiles") && layer["gridTiles"].is_array()) {
    for (const auto& tile : layer["gridTiles"]) {
      AppendTileQuad(mesh, tile, ts, gridSize, opacity);
    }
  }

  if (!mesh.localVertices.empty()) {
    outTemplate.staticLayers.push_back(std::move(mesh));
  }
}

void AppendYSortTile(std::vector<YSortTileCmd>& outTiles, const Json& tile, const TilesetDef& ts,
                     const float gridSize) {
  if (!tile.contains("px") || !tile.contains("src") || tile["px"].size() < 2 || tile["src"].size() < 2) {
    return;
  }
  const int32_t flipBits = tile.value("f", 0);
  TileQuadDesc quad;
  quad.localX = static_cast<float>(tile["px"][0].get<int32_t>());
  quad.localY = static_cast<float>(tile["px"][1].get<int32_t>());
  quad.srcX = static_cast<float>(tile["src"][0].get<int32_t>());
  quad.srcY = static_cast<float>(tile["src"][1].get<int32_t>());
  quad.size = gridSize;
  quad.flipX = (flipBits & 1) != 0;
  quad.flipY = (flipBits & 2) != 0;
  TilemapBuilder::AppendYSortTile(outTiles, ts.desc, quad);
}

void ParseYSortLayer(const Json& layer, const TilesetDef& ts, ChunkTemplate& outTemplate) {
  const auto gridSize = static_cast<float>(std::max(1, layer.value("__gridSize", ts.desc.tileGridSize)));
  std::vector<YSortTileCmd> layerTiles;

  if (layer.contains("autoLayerTiles") && layer["autoLayerTiles"].is_array()) {
    for (const auto& tile : layer["autoLayerTiles"]) {
      AppendYSortTile(layerTiles, tile, ts, gridSize);
    }
  }
  if (layer.contains("gridTiles") && layer["gridTiles"].is_array()) {
    for (const auto& tile : layer["gridTiles"]) {
      AppendYSortTile(layerTiles, tile, ts, gridSize);
    }
  }

  TilemapBuilder::GroupVerticalYSortTiles(layerTiles);
  for (auto& cmd : layerTiles) {
    outTemplate.ysortTiles.push_back(std::move(cmd));
  }
}

void ParseIntGridCsv(const Json& csv, const size_t maxCells, std::vector<uint8_t>& outCollisions) {
  const size_t count = std::min(csv.size(), maxCells);
  for (size_t i = 0; i < count; ++i) {
    const int32_t val = csv[i].is_number_integer() ? csv[i].get<int32_t>() : 0;
    outCollisions[i] = static_cast<uint8_t>(std::clamp(val, 0, 255));
  }
}

void ParseLegacyIntGrid(const Json& cells, const size_t maxCells, std::vector<uint8_t>& outCollisions) {
  for (const auto& cell : cells) {
    if (!cell.is_object()) continue;
    const int32_t coordId = cell.value("coordId", -1);
    if (coordId < 0 || static_cast<size_t>(coordId) >= maxCells) continue;
    const int32_t val = cell.value("v", 0);
    outCollisions[static_cast<size_t>(coordId)] = static_cast<uint8_t>(std::clamp(val, 0, 255));
  }
}

void ParseCollisionsLayer(const Json& layer, ParsedLevelRaw& outRaw) {
  const int32_t cWid = std::clamp(layer.value("__cWid", outRaw.rawGridW), 1, 512);
  const int32_t cHei = std::clamp(layer.value("__cHei", outRaw.rawGridH), 1, 512);
  outRaw.rawGridW = cWid;
  outRaw.rawGridH = cHei;
  const auto maxCells = static_cast<size_t>(cWid) * static_cast<size_t>(cHei);
  if (outRaw.rawCollisions.size() < maxCells) {
    outRaw.rawCollisions.resize(maxCells, kTileEmpty);
  }

  if (layer.contains("intGridCsv") && layer["intGridCsv"].is_array()) {
    ParseIntGridCsv(layer["intGridCsv"], maxCells, outRaw.rawCollisions);
  } else if (layer.contains("intGrid") && layer["intGrid"].is_array()) {
    ParseLegacyIntGrid(layer["intGrid"], maxCells, outRaw.rawCollisions);
  }
}

void ParseFieldInstance(const Json& field, std::unordered_map<std::string, PropFieldVal>& outFields) {
  if (!field.is_object() || !field.contains("__identifier") || !field.contains("__value")) {
    return;
  }
  const std::string key = field.value("__identifier", std::string{});
  const Json& val = field["__value"];
  if (key.empty() || val.is_null()) {
    return;
  }
  if (val.is_boolean()) {
    outFields[key] = val.get<bool>();
  } else if (val.is_number_integer()) {
    outFields[key] = val.get<int64_t>();
  } else if (val.is_number_float()) {
    outFields[key] = val.get<double>();
  } else if (val.is_string()) {
    outFields[key] = val.get<std::string>();
  }
}

PropSpawnDesc ParseSingleEntityInstance(const Json& ent, const uint32_t propIndex) {
  PropSpawnDesc desc;
  desc.identifier = ent.value("__identifier", std::string{"Prop"});
  if (ent.contains("px") && ent["px"].is_array() && ent["px"].size() >= 2) {
    desc.localX = static_cast<float>(ent["px"][0].get<int32_t>());
    desc.localY = static_cast<float>(ent["px"][1].get<int32_t>());
  }
  desc.width = static_cast<float>(ent.value("width", kDefaultTileSize));
  desc.height = static_cast<float>(ent.value("height", kDefaultTileSize));
  if (ent.contains("__pivot") && ent["__pivot"].is_array() && ent["__pivot"].size() >= 2) {
    desc.pivotX = ent["__pivot"][0].get<float>();
    desc.pivotY = ent["__pivot"][1].get<float>();
  }
  desc.propIndex = propIndex;

  if (ent.contains("fieldInstances") && ent["fieldInstances"].is_array()) {
    for (const auto& field : ent["fieldInstances"]) {
      ParseFieldInstance(field, desc.fields);
    }
  }
  return desc;
}

void ParseEntitiesLayer(const Json& layer, ChunkTemplate& outTemplate) {
  if (!layer.contains("entityInstances") || !layer["entityInstances"].is_array()) {
    return;
  }
  for (const auto& ent : layer["entityInstances"]) {
    if (!ent.is_object()) continue;
    const auto idx = static_cast<uint32_t>(outTemplate.propSpawns.size());
    outTemplate.propSpawns.push_back(ParseSingleEntityInstance(ent, idx));
  }
}

void ApplyStringLevelField(const std::string& id, const std::string& val, ChunkTemplate& outTemplate) {
  if (id == "biome" || id == "Biome") {
    outTemplate.biome = val;
  } else if (id == "edge_north" || id == "EdgeNorth") {
    outTemplate.edgeNorth = val;
  } else if (id == "edge_east" || id == "EdgeEast") {
    outTemplate.edgeEast = val;
  } else if (id == "edge_south" || id == "EdgeSouth") {
    outTemplate.edgeSouth = val;
  } else if (id == "edge_west" || id == "EdgeWest") {
    outTemplate.edgeWest = val;
  }
}

void ApplyKnownLevelField(const std::string& id, const Json& val, ChunkTemplate& outTemplate) {
  if ((id == "weight" || id == "Weight") && val.is_number()) {
    outTemplate.weight = std::max(0.0f, val.get<float>());
  } else if ((id == "is_border" || id == "IsBorder") && val.is_boolean()) {
    outTemplate.isBorder = val.get<bool>();
  } else if (val.is_string()) {
    ApplyStringLevelField(id, val.get<std::string>(), outTemplate);
  }
}

void ParseLevelFields(const Json& level, ChunkTemplate& outTemplate) {
  if (outTemplate.identifier == "Border" || outTemplate.identifier == "__Border") {
    outTemplate.isBorder = true;
  }
  if (!level.contains("fieldInstances") || !level["fieldInstances"].is_array()) {
    return;
  }
  for (const auto& field : level["fieldInstances"]) {
    if (!field.is_object()) continue;
    ParseFieldInstance(field, outTemplate.fields);
    ApplyKnownLevelField(field.value("__identifier", std::string{}), field["__value"], outTemplate);
  }
}

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needleLower) noexcept {
  if (needleLower.empty() || haystack.size() < needleLower.size()) {
    return false;
  }
  for (size_t i = 0; i + needleLower.size() <= haystack.size(); ++i) {
    bool match = true;
    for (size_t j = 0; j < needleLower.size(); ++j) {
      const char c = (haystack[i + j] >= 'A' && haystack[i + j] <= 'Z')
                         ? static_cast<char>(haystack[i + j] + ('a' - 'A'))
                         : haystack[i + j];
      if (c != needleLower[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

bool LevelHasExplicitCollisionLayer(const Json& layers) {
  return std::ranges::any_of(layers, [](const Json& layer) {
    return layer.is_object() && ContainsCaseInsensitive(layer.value("__identifier", std::string{}), "collision");
  });
}

void ParseLevelLayer(const Json& layer, const std::unordered_map<int32_t, TilesetDef>& tilesets,
                     const std::string& ldtkDirRelPath, const bool hasExplicitCollisionLayer, float& nextStaticDepth,
                     ParsedLevelRaw& outRaw) {
  const std::string id = layer.value("__identifier", std::string{});
  const std::string type = layer.value("__type", std::string{});

  if (type == "Entities" || id == "Entities") {
    ParseEntitiesLayer(layer, outRaw.tmpl);
    return;
  }

  const TilesetDef ts = ResolveLayerTileset(layer, tilesets, ldtkDirRelPath);
  if (ContainsCaseInsensitive(id, "ysort")) {
    ParseYSortLayer(layer, ts, outRaw.tmpl);
    return;
  }

  const float layerDepth = nextStaticDepth;
  nextStaticDepth += 1.0f;

  const bool isCollisionLayer =
      ContainsCaseInsensitive(id, "collision") || (!hasExplicitCollisionLayer && type == "IntGrid");
  if (isCollisionLayer) {
    ParseCollisionsLayer(layer, outRaw);
  }
  ParseStaticTileLayer(layer, ts, 0u, layerDepth, outRaw.tmpl);
}

ParsedLevelRaw ParseSingleLevel(const Json& level, const std::unordered_map<int32_t, TilesetDef>& tilesets,
                                const std::string& ldtkDirRelPath, const int32_t defaultTileSize) {
  ParsedLevelRaw raw;
  raw.tmpl.identifier = level.value("identifier", std::string{"Level"});
  raw.tmpl.tileSize = defaultTileSize;

  raw.coord.worldX = level.value("worldX", 0);
  raw.coord.worldY = level.value("worldY", 0);
  raw.coord.pxWid = std::max(defaultTileSize, level.value("pxWid", kDefaultChunkGridSize * defaultTileSize));
  raw.coord.pxHei = std::max(defaultTileSize, level.value("pxHei", kDefaultChunkGridSize * defaultTileSize));

  raw.rawGridW = std::clamp(raw.coord.pxWid / defaultTileSize, 1, 512);
  raw.rawGridH = std::clamp(raw.coord.pxHei / defaultTileSize, 1, 512);
  raw.tmpl.gridWidth = std::min(raw.rawGridW, kMaxChunkGridSize);
  raw.tmpl.gridHeight = std::min(raw.rawGridH, kMaxChunkGridSize);
  raw.rawCollisions.assign(static_cast<size_t>(raw.rawGridW) * static_cast<size_t>(raw.rawGridH), kTileEmpty);

  ParseLevelFields(level, raw.tmpl);

  if (level.contains("layerInstances") && level["layerInstances"].is_array()) {
    const auto& layers = level["layerInstances"];
    const bool hasExplicitCollisionLayer = LevelHasExplicitCollisionLayer(layers);
    float nextStaticDepth = 0.0f;
    // Iterate in reverse so background layers (last in LDtk's array) are parsed before foreground layers.
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      if (it->is_object()) {
        ParseLevelLayer(*it, tilesets, ldtkDirRelPath, hasExplicitCollisionLayer, nextStaticDepth, raw);
      }
    }
  }
  return raw;
}

void CopyRawCollisionsToSingleChunk(const ParsedLevelRaw& raw, ChunkTemplate& outTmpl) noexcept {
  outTmpl.gridWidth = std::clamp(raw.rawGridW, 1, kMaxChunkGridSize);
  outTmpl.gridHeight = std::clamp(raw.rawGridH, 1, kMaxChunkGridSize);
  const size_t maxCells =
      std::min({raw.rawCollisions.size(),
                static_cast<size_t>(outTmpl.gridWidth) * static_cast<size_t>(outTmpl.gridHeight), kMaxChunkArea});
  for (size_t i = 0; i < maxCells; ++i) {
    outTmpl.collisions[i] = raw.rawCollisions[i];
  }
}

void SliceStaticLayersForSubChunk(const ChunkTemplate& full, const float subX0, const float subY0, const float chunkPxW,
                                  const float chunkPxH, ChunkTemplate& outSub) {
  const float subX1 = subX0 + chunkPxW - 0.5f;
  const float subY1 = subY0 + chunkPxH - 0.5f;
  for (const auto& layer : full.staticLayers) {
    TileLayerMesh subMesh;
    subMesh.layerName = layer.layerName;
    subMesh.tilesetAssetId = layer.tilesetAssetId;
    subMesh.tilesetRelPath = layer.tilesetRelPath;
    subMesh.renderLayer = layer.renderLayer;
    subMesh.renderDepth = layer.renderDepth;

    const size_t quadCount = layer.localVertices.size() / 4u;
    for (size_t q = 0; q < quadCount; ++q) {
      const auto& v0 = layer.localVertices[q * 4u];
      if (v0.position.x < subX0 - 0.5f || v0.position.x > subX1 || v0.position.y < subY0 - 0.5f ||
          v0.position.y > subY1) {
        continue;
      }
      const auto baseIdx = static_cast<int>(subMesh.localVertices.size());
      for (size_t k = 0; k < 4u; ++k) {
        SDL_Vertex vert = layer.localVertices[q * 4u + k];
        vert.position.x -= subX0;
        vert.position.y -= subY0;
        subMesh.localVertices.push_back(vert);
      }
      subMesh.indices.push_back(baseIdx + 0);
      subMesh.indices.push_back(baseIdx + 1);
      subMesh.indices.push_back(baseIdx + 2);
      subMesh.indices.push_back(baseIdx + 0);
      subMesh.indices.push_back(baseIdx + 2);
      subMesh.indices.push_back(baseIdx + 3);
    }
    if (!subMesh.localVertices.empty()) {
      outSub.staticLayers.push_back(std::move(subMesh));
    }
  }
}

void SliceYSortTilesForSubChunk(const ChunkTemplate& full, const float subX0, const float subY0, const float chunkPxW,
                                const float chunkPxH, ChunkTemplate& outSub) {
  const float subX1 = subX0 + chunkPxW - 0.5f;
  const float subY1 = subY0 + chunkPxH - 0.5f;
  for (const auto& ytile : full.ysortTiles) {
    if (ytile.localX < subX0 - 0.5f || ytile.localX > subX1 || ytile.localY < subY0 - 0.5f || ytile.localY > subY1) {
      continue;
    }
    YSortTileCmd cmd = ytile;
    cmd.localX -= subX0;
    cmd.localY -= subY0;
    cmd.sortLocalY -= subY0;
    outSub.ysortTiles.push_back(std::move(cmd));
  }
}

void SliceCollisionsForSubChunk(const ParsedLevelRaw& raw, const int32_t sx, const int32_t sy, const int32_t baseGridW,
                                const int32_t baseGridH, ChunkTemplate& outSub) noexcept {
  for (int32_t ly = 0; ly < baseGridH; ++ly) {
    const int32_t gy = sy * baseGridH + ly;
    if (gy < 0 || gy >= raw.rawGridH) continue;
    for (int32_t lx = 0; lx < baseGridW; ++lx) {
      const int32_t gx = sx * baseGridW + lx;
      if (gx < 0 || gx >= raw.rawGridW) continue;
      const auto srcIdx = static_cast<size_t>(gy) * static_cast<size_t>(raw.rawGridW) + static_cast<size_t>(gx);
      const auto dstIdx = static_cast<size_t>(ly) * static_cast<size_t>(baseGridW) + static_cast<size_t>(lx);
      if (srcIdx < raw.rawCollisions.size() && dstIdx < kMaxChunkArea) {
        outSub.collisions[dstIdx] = raw.rawCollisions[srcIdx];
      }
    }
  }
}

void SlicePropsForSubChunk(const ChunkTemplate& full, const float subX0, const float subY0, const float chunkPxW,
                           const float chunkPxH, ChunkTemplate& outSub) {
  const float subX1 = subX0 + chunkPxW;
  const float subY1 = subY0 + chunkPxH;
  const auto tileSz = static_cast<float>(full.tileSize);
  for (const auto& prop : full.propSpawns) {
    const bool isBoxRegion =
        (prop.pivotX == 0.0f && prop.pivotY == 0.0f && (prop.width > tileSz || prop.height > tileSz));
    if (isBoxRegion) {
      const float x0 = std::max(subX0, prop.localX);
      const float y0 = std::max(subY0, prop.localY);
      const float x1 = std::min(subX1, prop.localX + prop.width);
      const float y1 = std::min(subY1, prop.localY + prop.height);
      if (x1 > x0 && y1 > y0) {
        PropSpawnDesc clipped = prop;
        clipped.localX = x0 - subX0;
        clipped.localY = y0 - subY0;
        clipped.width = x1 - x0;
        clipped.height = y1 - y0;
        clipped.propIndex = static_cast<uint32_t>(outSub.propSpawns.size());
        outSub.propSpawns.push_back(std::move(clipped));
      }
    } else if (prop.localX >= subX0 && prop.localX < subX1 && prop.localY >= subY0 && prop.localY < subY1) {
      PropSpawnDesc moved = prop;
      moved.localX -= subX0;
      moved.localY -= subY0;
      moved.propIndex = static_cast<uint32_t>(outSub.propSpawns.size());
      outSub.propSpawns.push_back(std::move(moved));
    }
  }
}

std::string FindOpenEdgeTag(const ChunkTemplate& tmpl) {
  for (const auto* edge : {&tmpl.edgeNorth, &tmpl.edgeEast, &tmpl.edgeSouth, &tmpl.edgeWest}) {
    if (!edge->empty() && ChunkTemplate::IsOpenSocketTag(*edge)) {
      return *edge;
    }
  }
  return {};
}

int32_t FindSurfaceSubXOnRow(const ParsedLevelRaw& raw, const int32_t gy, const int32_t baseGridW,
                             const int32_t cols) noexcept {
  if (gy < 0 || gy >= raw.rawGridH) {
    return cols / 2;
  }
  const auto rowOffset = static_cast<size_t>(gy) * static_cast<size_t>(raw.rawGridW);
  for (int32_t lx = 0; lx < raw.rawGridW; ++lx) {
    const size_t idx = rowOffset + static_cast<size_t>(lx);
    if (idx < raw.rawCollisions.size() && raw.rawCollisions[idx] == kTileSurface) {
      return std::clamp(lx / baseGridW, 0, cols - 1);
    }
  }
  return cols / 2;
}

int32_t FindSurfaceSubYOnCol(const ParsedLevelRaw& raw, const int32_t gx, const int32_t baseGridH,
                             const int32_t rows) noexcept {
  if (gx < 0 || gx >= raw.rawGridW) {
    return rows / 2;
  }
  for (int32_t ly = 0; ly < raw.rawGridH; ++ly) {
    const size_t idx = static_cast<size_t>(ly) * static_cast<size_t>(raw.rawGridW) + static_cast<size_t>(gx);
    if (idx < raw.rawCollisions.size() && raw.rawCollisions[idx] == kTileSurface) {
      return std::clamp(ly / baseGridH, 0, rows - 1);
    }
  }
  return rows / 2;
}

std::string ResolveHorizontalOuterEdge(const ParsedLevelRaw& raw, const int32_t sx, const int32_t cols,
                                       const int32_t baseGridW, const int32_t boundaryGy,
                                       const std::string& levelEdgeTag) {
  if (ChunkTemplate::IsOpenSocketTag(levelEdgeTag)) {
    return levelEdgeTag;
  }
  const int32_t targetSubX = FindSurfaceSubXOnRow(raw, boundaryGy, baseGridW, cols);
  return (sx == targetSubX) ? levelEdgeTag : FindOpenEdgeTag(raw.tmpl);
}

std::string ResolveVerticalOuterEdge(const ParsedLevelRaw& raw, const int32_t sy, const int32_t rows,
                                     const int32_t baseGridH, const int32_t boundaryGx,
                                     const std::string& levelEdgeTag) {
  if (ChunkTemplate::IsOpenSocketTag(levelEdgeTag)) {
    return levelEdgeTag;
  }
  const int32_t targetSubY = FindSurfaceSubYOnCol(raw, boundaryGx, baseGridH, rows);
  return (sy == targetSubY) ? levelEdgeTag : FindOpenEdgeTag(raw.tmpl);
}

ChunkTemplate BuildSubChunkTemplate(const ParsedLevelRaw& raw, const int32_t sx, const int32_t sy, const int32_t cols,
                                    const int32_t rows, const int32_t baseGridW, const int32_t baseGridH) {
  ChunkTemplate sub;
  sub.identifier = raw.tmpl.identifier + "__" + std::to_string(sx) + "_" + std::to_string(sy);
  sub.tileSize = raw.tmpl.tileSize;
  sub.gridWidth = baseGridW;
  sub.gridHeight = baseGridH;
  sub.weight = 0.0f;
  sub.biome = raw.tmpl.biome;
  sub.fields = raw.tmpl.fields;
  sub.fields["structure_id"] = raw.tmpl.identifier;
  sub.fields["sub_x"] = static_cast<int64_t>(sx);
  sub.fields["sub_y"] = static_cast<int64_t>(sy);
  sub.fields["span_w"] = static_cast<int64_t>(cols);
  sub.fields["span_h"] = static_cast<int64_t>(rows);

  sub.edgeNorth =
      (sy == 0) ? ResolveHorizontalOuterEdge(raw, sx, cols, baseGridW, 0, raw.tmpl.edgeNorth) : "__Internal";
  sub.edgeSouth = (sy == rows - 1)
                      ? ResolveHorizontalOuterEdge(raw, sx, cols, baseGridW, raw.rawGridH - 1, raw.tmpl.edgeSouth)
                      : "__Internal";
  sub.edgeWest = (sx == 0) ? ResolveVerticalOuterEdge(raw, sy, rows, baseGridH, 0, raw.tmpl.edgeWest) : "__Internal";
  sub.edgeEast = (sx == cols - 1)
                     ? ResolveVerticalOuterEdge(raw, sy, rows, baseGridH, raw.rawGridW - 1, raw.tmpl.edgeEast)
                     : "__Internal";

  const auto chunkPxW = static_cast<float>(baseGridW * raw.tmpl.tileSize);
  const auto chunkPxH = static_cast<float>(baseGridH * raw.tmpl.tileSize);
  const float subX0 = static_cast<float>(sx) * chunkPxW;
  const float subY0 = static_cast<float>(sy) * chunkPxH;

  SliceStaticLayersForSubChunk(raw.tmpl, subX0, subY0, chunkPxW, chunkPxH, sub);
  SliceYSortTilesForSubChunk(raw.tmpl, subX0, subY0, chunkPxW, chunkPxH, sub);
  SliceCollisionsForSubChunk(raw, sx, sy, baseGridW, baseGridH, sub);
  SlicePropsForSubChunk(raw.tmpl, subX0, subY0, chunkPxW, chunkPxH, sub);
  return sub;
}

void AppendParsedLevelOrSlices(ParsedLevelRaw&& raw, const int32_t baseGridW, const int32_t baseGridH,
                               std::vector<LevelRawCoord>& outCoords, TilemapData& projectData) {
  const int32_t cols = std::max(1, raw.rawGridW / baseGridW);
  const int32_t rows = std::max(1, raw.rawGridH / baseGridH);
  if (cols <= 1 && rows <= 1) {
    CopyRawCollisionsToSingleChunk(raw, raw.tmpl);
    outCoords.push_back(raw.coord);
    projectData.templates.push_back(std::move(raw.tmpl));
    return;
  }

  const int32_t chunkPxW = baseGridW * projectData.tileSize;
  const int32_t chunkPxH = baseGridH * projectData.tileSize;
  for (int32_t sy = 0; sy < rows; ++sy) {
    for (int32_t sx = 0; sx < cols; ++sx) {
      LevelRawCoord subCoord = raw.coord;
      subCoord.worldX = raw.coord.worldX + sx * chunkPxW;
      subCoord.worldY = raw.coord.worldY + sy * chunkPxH;
      subCoord.pxWid = chunkPxW;
      subCoord.pxHei = chunkPxH;
      outCoords.push_back(subCoord);
      projectData.templates.push_back(BuildSubChunkTemplate(raw, sx, sy, cols, rows, baseGridW, baseGridH));
    }
  }
}

void NormalizeLevelGridCoords(std::vector<ChunkTemplate>& templates, const std::vector<LevelRawCoord>& rawCoords,
                              TilemapData& outData) {
  if (templates.empty()) {
    return;
  }
  int32_t minWorldX = std::numeric_limits<int32_t>::max();
  int32_t minWorldY = std::numeric_limits<int32_t>::max();
  for (const auto& rc : rawCoords) {
    minWorldX = std::min(minWorldX, rc.worldX);
    minWorldY = std::min(minWorldY, rc.worldY);
  }

  const int32_t chunkPxW = std::max(1, outData.chunkGridWidth * outData.tileSize);
  const int32_t chunkPxH = std::max(1, outData.chunkGridHeight * outData.tileSize);
  int32_t maxGridX = 0;
  int32_t maxGridY = 0;

  for (size_t i = 0; i < templates.size(); ++i) {
    templates[i].gridX = FloorDiv(rawCoords[i].worldX - minWorldX, chunkPxW);
    templates[i].gridY = FloorDiv(rawCoords[i].worldY - minWorldY, chunkPxH);
    maxGridX = std::max(maxGridX, templates[i].gridX);
    maxGridY = std::max(maxGridY, templates[i].gridY);
  }
  outData.gridCols = std::max(1, maxGridX + 1);
  outData.gridRows = std::max(1, maxGridY + 1);
}

void ParseAllLevels(const Json& levelsArray, const std::unordered_map<int, TilesetDef>& tilesets,
                    const std::string& ldtkDirRelPath, TilemapData& projectData) {
  std::vector<ParsedLevelRaw> playableLevels;
  bool foundExplicitBorder = false;
  int32_t minGridW = std::numeric_limits<int32_t>::max();
  int32_t minGridH = std::numeric_limits<int32_t>::max();

  for (const auto& level : levelsArray) {
    if (!level.is_object()) continue;
    ParsedLevelRaw raw = ParseSingleLevel(level, tilesets, ldtkDirRelPath, projectData.tileSize);
    if (raw.tmpl.isBorder) {
      CopyRawCollisionsToSingleChunk(raw, raw.tmpl);
      projectData.borderTemplate = raw.tmpl;
      foundExplicitBorder = true;
      continue;
    }
    if (TilemapBuilder::IsPaletteTemplateName(raw.tmpl.identifier)) {
      CopyRawCollisionsToSingleChunk(raw, raw.tmpl);
      projectData.paletteTemplates.push_back(std::move(raw.tmpl));
      continue;
    }
    minGridW = std::min(minGridW, std::clamp(raw.rawGridW, 1, kMaxChunkGridSize));
    minGridH = std::min(minGridH, std::clamp(raw.rawGridH, 1, kMaxChunkGridSize));
    playableLevels.push_back(std::move(raw));
  }

  if (minGridW != std::numeric_limits<int32_t>::max()) {
    projectData.chunkGridWidth = minGridW;
    projectData.chunkGridHeight = minGridH;
  }

  std::vector<LevelRawCoord> rawCoords;
  for (auto& raw : playableLevels) {
    AppendParsedLevelOrSlices(std::move(raw), projectData.chunkGridWidth, projectData.chunkGridHeight, rawCoords,
                              projectData);
  }

  if (!foundExplicitBorder) {
    projectData.borderTemplate =
        ChunkTemplate::CreateSolidBorder(projectData.tileSize, projectData.chunkGridWidth, projectData.chunkGridHeight);
  }

  // If the file contained only a border level, keep it in templates as well so Toroidal/ProceduralHash still work.
  if (projectData.templates.empty() && foundExplicitBorder) {
    projectData.templates.push_back(projectData.borderTemplate);
    rawCoords.push_back(LevelRawCoord{});
  }

  NormalizeLevelGridCoords(projectData.templates, rawCoords, projectData);
}

const CatalogEntry* FindLdtkCatalogEntry(const AssetManager* assetManager, const std::string& path) {
  if (assetManager == nullptr) {
    return nullptr;
  }
  const auto& catalog = assetManager->GetCatalog();
  if (const auto* entry = catalog.Find(path)) {
    return entry;
  }
  const std::filesystem::path fsPath(path);
  if (const auto* entry = catalog.Find(fsPath.filename().string())) {
    return entry;
  }
  return catalog.Find(fsPath.stem().string());
}

std::string ResolveLdtkFullPath(const std::string& ldtkPath, const AssetManager* assetManager) {
  if (const auto* entry = FindLdtkCatalogEntry(assetManager, ldtkPath)) {
    return entry->fullPath;
  }
  const std::filesystem::path fsPath(ldtkPath);
  if (assetManager != nullptr && !fsPath.is_absolute()) {
    return assetManager->GetFullPath(ldtkPath);
  }
  return ldtkPath;
}

std::string ResolveLdtkDirRelPath(const std::string& ldtkPath, const std::string& fullPath,
                                  const AssetManager* assetManager) {
  const std::filesystem::path relParent = std::filesystem::path(ldtkPath).parent_path();
  if (!relParent.empty() && !relParent.is_absolute()) {
    return relParent.generic_string();
  }
  if (assetManager != nullptr && !assetManager->GetBasePath().empty()) {
    std::error_code ec;
    const auto rel = std::filesystem::relative(std::filesystem::path(fullPath).parent_path(),
                                               std::filesystem::path(assetManager->GetBasePath()), ec);
    if (!ec && rel != ".") {
      return rel.generic_string();
    }
  }
  return {};
}

}  // namespace

std::optional<TilemapData> LdtkParser::ParseFromJsonString(const std::string& jsonText,
                                                           const std::string& ldtkDirRelPath) {
  if (jsonText.empty()) {
    Logger::Error("LdtkParser: empty JSON string.");
    return std::nullopt;
  }

  const Json root = Json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    Logger::Error("LdtkParser: failed to parse JSON document.");
    return std::nullopt;
  }

  TilemapData projectData;
  projectData.sourceFormat = "ldtk";
  projectData.tileSize = std::max(1, root.value("defaultGridSize", kDefaultTileSize));
  const auto tilesets = ParseTilesetDefs(root, ldtkDirRelPath, projectData);
  if (!tilesets.empty() && !root.contains("defaultGridSize")) {
    projectData.tileSize = tilesets.begin()->second.desc.tileGridSize;
  }

  if (!root.contains("levels") || !root["levels"].is_array() || root["levels"].empty()) {
    Logger::Error("LdtkParser: no levels array found in LDtk JSON.");
    return std::nullopt;
  }

  ParseAllLevels(root["levels"], tilesets, ldtkDirRelPath, projectData);
  return projectData;
}

std::optional<TilemapData> LdtkParser::ParseFromFile(const std::string& ldtkPath, const AssetManager* assetManager) {
  const std::string fullPath = ResolveLdtkFullPath(ldtkPath, assetManager);
  if (fullPath.empty()) {
    Logger::Error("LdtkParser::ParseFromFile: invalid path '" + ldtkPath + "'");
    return std::nullopt;
  }

  const auto bytes = ReadFileViaSDL(fullPath, assetManager);
  if (!bytes.has_value()) {
    Logger::Error("LdtkParser::ParseFromFile: failed to read '" + fullPath + "'");
    return std::nullopt;
  }

  const std::string ldtkDirRelPath = ResolveLdtkDirRelPath(ldtkPath, fullPath, assetManager);
  return ParseFromJsonString(*bytes, ldtkDirRelPath);
}

}  // namespace octarine::tilemap
