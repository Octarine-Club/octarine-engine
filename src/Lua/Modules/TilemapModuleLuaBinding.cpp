#include "Lua/Modules/TilemapModuleLuaBinding.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ECS/Registry.h"
#include "Lua/LuaBindingContext.h"
#include "Systems/TilemapSystem.h"
#include "Tilemap/ChunkTemplate.h"
#include "Tilemap/TilemapBuilder.h"

using octarine::tilemap::ChunkTemplate;
using octarine::tilemap::CollisionLayerRule;
using octarine::tilemap::kDefaultChunkGridSize;
using octarine::tilemap::kDefaultTileSize;
using octarine::tilemap::kMaxChunkArea;
using octarine::tilemap::kMaxChunkGridSize;
using octarine::tilemap::kTileChannel1;
using octarine::tilemap::kTileChannel2;
using octarine::tilemap::kTileChannel3;
using octarine::tilemap::kTileChannel4;
using octarine::tilemap::kTileChannel5;
using octarine::tilemap::kTileChannel6;
using octarine::tilemap::kTileChannel7;
using octarine::tilemap::kTileEmpty;
using octarine::tilemap::kTileMaskChannel1;
using octarine::tilemap::kTileMaskChannel2;
using octarine::tilemap::kTileMaskChannel3;
using octarine::tilemap::kTileMaskChannel4;
using octarine::tilemap::kTileMaskChannel5;
using octarine::tilemap::kTileMaskChannel6;
using octarine::tilemap::kTileMaskChannel7;
using octarine::tilemap::kTileMaskChasm;
using octarine::tilemap::kTileMaskEmpty;
using octarine::tilemap::kTileMaskGroundBlocked;
using octarine::tilemap::kTileMaskHazard;
using octarine::tilemap::kTileMaskProjectileBlocked;
using octarine::tilemap::kTileMaskSolid;
using octarine::tilemap::kTileMaskSurface;
using octarine::tilemap::kTileMaskWall;
using octarine::tilemap::kTileSurface;
using octarine::tilemap::kTileWall;
using octarine::tilemap::PropSpawnDesc;
using octarine::tilemap::StitchMode;
using octarine::tilemap::TileLayerMesh;
using octarine::tilemap::TilemapBuilder;
using octarine::tilemap::TilemapData;
using octarine::tilemap::TilemapLoadOptions;
using octarine::tilemap::TilemapParseOptions;
using octarine::tilemap::TileQuadDesc;
using octarine::tilemap::TilesetDesc;
using octarine::tilemap::YSortTileCmd;

namespace {

TilemapSystem* EnsureTilemap(Registry* r) {
  if (r == nullptr) {
    return nullptr;
  }
  if (auto* tm = r->TryGet<TilemapSystem>()) {
    return tm;
  }
  return &r->Set<TilemapSystem>(TilemapSystem());
}

StitchMode ParseStitchMode(const std::string& modeStr) {
  if (modeStr == "procedural_hash" || modeStr == "ProceduralHash" || modeStr == "procedural") {
    return StitchMode::ProceduralHash;
  }
  if (modeStr == "arena_fixed" || modeStr == "ArenaFixed" || modeStr == "arena") {
    return StitchMode::ArenaFixed;
  }
  return StitchMode::Toroidal;
}

const char* StitchModeToString(const StitchMode mode) {
  switch (mode) {
    case StitchMode::ProceduralHash:
      return "procedural_hash";
    case StitchMode::ArenaFixed:
      return "arena_fixed";
    case StitchMode::Toroidal:
    default:
      return "toroidal";
  }
}

std::string GetStringOr(const sol::table& t, const char* key, std::string fallback = {}) {
  if (const auto opt = t.get<sol::optional<std::string>>(key); opt.has_value()) {
    return *opt;
  }
  return fallback;
}

void ParseLuaCollisionLayerRules(const sol::table& t, std::vector<CollisionLayerRule>& outRules) {
  const sol::optional<sol::table> rulesOpt = t.get<sol::optional<sol::table>>("collision_layers");
  if (!rulesOpt.has_value()) {
    return;
  }
  for (const auto& [_, val] : *rulesOpt) {
    if (val.is<std::string>()) {
      outRules.push_back(CollisionLayerRule{val.as<std::string>(), 0});
    } else if (val.is<sol::table>()) {
      const sol::table ruleTbl = val.as<sol::table>();
      const std::string match = GetStringOr(ruleTbl, "match", GetStringOr(ruleTbl, "name", ""));
      if (!match.empty()) {
        const uint8_t overrideVal = static_cast<uint8_t>(ruleTbl.get_or("value", 0));
        outRules.push_back(CollisionLayerRule{match, overrideVal});
      }
    }
  }
}

void ParseLuaStringListOption(const sol::table& t, const char* key, std::vector<std::string>& outList) {
  const sol::optional<sol::table> listOpt = t.get<sol::optional<sol::table>>(key);
  if (!listOpt.has_value()) {
    return;
  }
  for (const auto& [_, val] : *listOpt) {
    if (val.is<std::string>()) {
      const std::string s = val.as<std::string>();
      if (!s.empty()) {
        outList.push_back(s);
      }
    }
  }
}

TilemapLoadOptions ParseLoadOptions(const sol::optional<sol::table>& opts) {
  TilemapLoadOptions parsed;
  if (!opts.has_value()) {
    return parsed;
  }
  const sol::table& t = *opts;
  if (const sol::optional<std::string> modeStr = t.get<sol::optional<std::string>>("stitch_mode");
      modeStr.has_value()) {
    parsed.stitchMode = ParseStitchMode(*modeStr);
  }
  parsed.seed = t.get_or("seed", parsed.seed);
  parsed.loadRadius = t.get_or("load_radius", parsed.loadRadius);
  parsed.unloadRadius = t.get_or("unload_radius", parsed.unloadRadius);
  parsed.ysortLayer = t.get_or("ysort_layer", parsed.ysortLayer);
  parsed.tileScale = t.get_or("tile_scale", parsed.tileScale);
  parsed.variantCount = t.get_or("variant_count", parsed.variantCount);
  parsed.formatHint = GetStringOr(t, "format", parsed.formatHint);

  ParseLuaCollisionLayerRules(t, parsed.parseOptions.collisionLayerRules);
  ParseLuaStringListOption(t, "ysort_layers", parsed.parseOptions.ysortLayerMatches);
  ParseLuaStringListOption(t, "palette_names", parsed.parseOptions.paletteMatches);

  if (const auto anchorOpt = t.get<sol::optional<uint8_t>>("socket_anchor_tile"); anchorOpt.has_value()) {
    parsed.parseOptions.socketAnchorTileValue = *anchorOpt;
  }
  if (const auto sliceOpt = t.get<sol::optional<bool>>("slice_multi_chunk"); sliceOpt.has_value()) {
    parsed.parseOptions.sliceMultiChunkLevels = *sliceOpt;
  }
  if (const auto solidOpt = t.get<sol::optional<uint8_t>>("solid_mask"); solidOpt.has_value()) {
    parsed.defaultSolidMask = *solidOpt;
  }
  if (const auto raycastOpt = t.get<sol::optional<uint8_t>>("raycast_mask"); raycastOpt.has_value()) {
    parsed.defaultRaycastMask = *raycastOpt;
  }
  return parsed;
}

using TilesetMap = std::unordered_map<std::string, TilesetDesc>;

TilesetMap ParseLuaTilesets(const sol::table& dataTable, const int32_t defaultTileSize, TilemapData& outData) {
  TilesetMap map;
  const sol::optional<sol::table> listOpt = dataTable.get<sol::optional<sol::table>>("tilesets");
  if (!listOpt.has_value()) {
    return map;
  }
  for (const auto& [_, val] : *listOpt) {
    if (!val.is<sol::table>()) continue;
    const sol::table tsTable = val.as<sol::table>();
    TilesetDesc desc;
    desc.assetId = GetStringOr(tsTable, "id", "");
    desc.relPath = GetStringOr(tsTable, "file", GetStringOr(tsTable, "rel_path", ""));
    desc.textureWidth = std::max(1.0f, tsTable.get_or("width", 256.0f));
    desc.textureHeight = std::max(1.0f, tsTable.get_or("height", 256.0f));
    desc.tileGridSize = std::max(1, tsTable.get_or("tile_size", defaultTileSize));
    if (!desc.assetId.empty()) {
      outData.tilesetAssets.emplace_back(desc.assetId, desc.relPath);
      map[desc.assetId] = std::move(desc);
    }
  }
  return map;
}

TilesetDesc ResolveLuaTileset(const std::string& id, const TilesetMap& tilesets, const int32_t defaultTileSize) {
  if (const auto it = tilesets.find(id); it != tilesets.end()) {
    return it->second;
  }
  if (!tilesets.empty()) {
    return tilesets.begin()->second;
  }
  TilesetDesc fallback;
  fallback.assetId = id.empty() ? "default_tileset" : id;
  fallback.tileGridSize = defaultTileSize;
  return fallback;
}

TileQuadDesc ParseLuaTileQuad(const sol::table& tileTable, const float gridSize) {
  TileQuadDesc quad;
  quad.size = gridSize;
  if (const auto gx = tileTable.get<sol::optional<int32_t>>("gx"); gx.has_value()) {
    quad.localX = static_cast<float>(*gx) * gridSize;
    quad.localY = static_cast<float>(tileTable.get_or("gy", 0)) * gridSize;
  } else {
    quad.localX = tileTable.get_or("x", 0.0f);
    quad.localY = tileTable.get_or("y", 0.0f);
  }
  quad.srcX = tileTable.get_or("src_x", 0.0f);
  quad.srcY = tileTable.get_or("src_y", 0.0f);
  quad.alpha = tileTable.get_or("alpha", 1.0f);
  quad.flipX = tileTable.get_or("flip_x", false);
  quad.flipY = tileTable.get_or("flip_y", false);
  return quad;
}

void ParseLuaStaticLayer(const sol::table& layerTable, const TilesetMap& tilesets, const int32_t defaultTileSize,
                         ChunkTemplate& outTmpl) {
  const std::string tsId = GetStringOr(layerTable, "tileset", "");
  const TilesetDesc ts = ResolveLuaTileset(tsId, tilesets, defaultTileSize);
  const auto gridSize = static_cast<float>(std::max(1, layerTable.get_or("tile_size", ts.tileGridSize)));

  TileLayerMesh mesh;
  mesh.layerName = GetStringOr(layerTable, "name", "Ground");
  mesh.tilesetAssetId = ts.assetId;
  mesh.tilesetRelPath = ts.relPath;
  mesh.renderLayer = layerTable.get_or("render_layer", 0u);
  mesh.renderDepth = layerTable.get_or("render_depth", static_cast<float>(outTmpl.staticLayers.size()));

  if (const auto tilesOpt = layerTable.get<sol::optional<sol::table>>("tiles"); tilesOpt.has_value()) {
    TilemapBuilder::ReserveLayerMesh(mesh, tilesOpt->size());
    for (const auto& [_, tileVal] : *tilesOpt) {
      if (tileVal.is<sol::table>()) {
        const TileQuadDesc quad = ParseLuaTileQuad(tileVal.as<sol::table>(), gridSize);
        TilemapBuilder::AppendStaticTileQuad(mesh, ts, quad);
      }
    }
  }
  if (!mesh.localVertices.empty()) {
    outTmpl.staticLayers.push_back(std::move(mesh));
  }
}

void ParseLuaYSortTiles(const sol::table& tmplTable, const TilesetMap& tilesets, const int32_t defaultTileSize,
                        ChunkTemplate& outTmpl) {
  const auto ysortOpt = tmplTable.get<sol::optional<sol::table>>("ysort_tiles");
  if (!ysortOpt.has_value()) {
    return;
  }
  for (const auto& [_, tileVal] : *ysortOpt) {
    if (!tileVal.is<sol::table>()) continue;
    const sol::table tileTable = tileVal.as<sol::table>();
    const std::string tsId = GetStringOr(tileTable, "tileset", "");
    const TilesetDesc ts = ResolveLuaTileset(tsId, tilesets, defaultTileSize);
    const auto gridSize = static_cast<float>(std::max(1, tileTable.get_or("tile_size", ts.tileGridSize)));
    const TileQuadDesc quad = ParseLuaTileQuad(tileTable, gridSize);
    TilemapBuilder::AppendYSortTile(outTmpl.ysortTiles, ts, quad);
  }
  TilemapBuilder::GroupVerticalYSortTiles(outTmpl.ysortTiles);
}

void ParseLuaCollisions(const sol::table& tmplTable, ChunkTemplate& outTmpl) {
  const auto collOpt = tmplTable.get<sol::optional<sol::table>>("collisions");
  if (!collOpt.has_value()) {
    return;
  }
  const size_t maxCells = std::min(static_cast<size_t>(outTmpl.gridWidth * outTmpl.gridHeight), kMaxChunkArea);
  const size_t len = std::min(collOpt->size(), maxCells);
  for (size_t i = 0; i < len; ++i) {
    const int val = (*collOpt)[i + 1].get_or(0);
    outTmpl.collisions[i] = static_cast<uint8_t>(std::clamp(val, 0, 255));
  }
}

void ParseLuaFieldMap(const sol::table& fieldsTable,
                      std::unordered_map<std::string, octarine::tilemap::PropFieldVal>& outFields) {
  for (const auto& [k, v] : fieldsTable) {
    if (!k.is<std::string>()) continue;
    const std::string key = k.as<std::string>();
    if (v.is<bool>()) {
      outFields[key] = v.as<bool>();
    } else if (v.is<std::string>()) {
      outFields[key] = v.as<std::string>();
    } else if (v.is<double>()) {
      const double d = v.as<double>();
      if (std::floor(d) == d) {
        outFields[key] = static_cast<int64_t>(d);
      } else {
        outFields[key] = d;
      }
    }
  }
}

void ParseLuaProps(const sol::table& tmplTable, const int32_t defaultTileSize, ChunkTemplate& outTmpl) {
  const auto propsOpt = tmplTable.get<sol::optional<sol::table>>("props");
  if (!propsOpt.has_value()) {
    return;
  }
  for (const auto& [_, propVal] : *propsOpt) {
    if (!propVal.is<sol::table>()) continue;
    const sol::table propTable = propVal.as<sol::table>();
    PropSpawnDesc desc;
    desc.identifier = GetStringOr(propTable, "id", "Prop");
    desc.localX = propTable.get_or("x", 0.0f);
    desc.localY = propTable.get_or("y", 0.0f);
    desc.width = propTable.get_or("width", static_cast<float>(defaultTileSize));
    desc.height = propTable.get_or("height", static_cast<float>(defaultTileSize));
    desc.propIndex = static_cast<uint32_t>(outTmpl.propSpawns.size());
    if (const auto fieldsOpt = propTable.get<sol::optional<sol::table>>("fields"); fieldsOpt.has_value()) {
      ParseLuaFieldMap(*fieldsOpt, desc.fields);
    }
    outTmpl.propSpawns.push_back(std::move(desc));
  }
}

ChunkTemplate ParseLuaTemplate(const sol::table& tmplTable, const TilesetMap& tilesets, const TilemapData& data) {
  ChunkTemplate tmpl;
  tmpl.identifier = GetStringOr(tmplTable, "id", "Chunk");
  tmpl.gridX = tmplTable.get_or("grid_x", 0);
  tmpl.gridY = tmplTable.get_or("grid_y", 0);
  tmpl.tileSize = data.tileSize;
  tmpl.gridWidth = std::clamp(tmplTable.get_or("grid_width", data.chunkGridWidth), 1, kMaxChunkGridSize);
  tmpl.gridHeight = std::clamp(tmplTable.get_or("grid_height", data.chunkGridHeight), 1, kMaxChunkGridSize);
  tmpl.weight = std::max(0.0f, tmplTable.get_or("weight", 1.0f));
  tmpl.isBorder = tmplTable.get_or("is_border", false);
  tmpl.biome = GetStringOr(tmplTable, "biome", "");
  tmpl.edgeNorth = GetStringOr(tmplTable, "edge_north", "");
  tmpl.edgeEast = GetStringOr(tmplTable, "edge_east", "");
  tmpl.edgeSouth = GetStringOr(tmplTable, "edge_south", "");
  tmpl.edgeWest = GetStringOr(tmplTable, "edge_west", "");
  if (const auto fieldsOpt = tmplTable.get<sol::optional<sol::table>>("fields"); fieldsOpt.has_value()) {
    ParseLuaFieldMap(*fieldsOpt, tmpl.fields);
  }

  ParseLuaCollisions(tmplTable, tmpl);
  if (const auto layersOpt = tmplTable.get<sol::optional<sol::table>>("static_layers"); layersOpt.has_value()) {
    for (const auto& [_, layerVal] : *layersOpt) {
      if (layerVal.is<sol::table>()) {
        ParseLuaStaticLayer(layerVal.as<sol::table>(), tilesets, data.tileSize, tmpl);
      }
    }
  }
  ParseLuaYSortTiles(tmplTable, tilesets, data.tileSize, tmpl);
  ParseLuaProps(tmplTable, data.tileSize, tmpl);
  return tmpl;
}

bool AppendParsedLuaTemplate(ChunkTemplate tmpl, const bool hasExplicitW, const bool hasExplicitH, TilemapData& data) {
  if (tmpl.isBorder) {
    data.borderTemplate = std::move(tmpl);
    return true;
  }
  if (TilemapBuilder::IsPaletteTemplateName(tmpl.identifier)) {
    data.paletteTemplates.push_back(std::move(tmpl));
    return false;
  }
  if (!hasExplicitW) data.chunkGridWidth = tmpl.gridWidth;
  if (!hasExplicitH) data.chunkGridHeight = tmpl.gridHeight;
  data.templates.push_back(std::move(tmpl));
  return false;
}

std::optional<TilemapData> BuildTilemapDataFromLuaTable(const sol::table& dataTable) {
  TilemapData data;
  data.sourceFormat = GetStringOr(dataTable, "source_format", "lua_data");
  data.tileSize = std::max(1, dataTable.get_or("tile_size", kDefaultTileSize));
  const auto explicitGridW = dataTable.get<sol::optional<int32_t>>("chunk_grid_width");
  const auto explicitGridH = dataTable.get<sol::optional<int32_t>>("chunk_grid_height");
  data.chunkGridWidth = std::clamp(explicitGridW.value_or(kDefaultChunkGridSize), 1, kMaxChunkGridSize);
  data.chunkGridHeight = std::clamp(explicitGridH.value_or(kDefaultChunkGridSize), 1, kMaxChunkGridSize);

  const TilesetMap tilesets = ParseLuaTilesets(dataTable, data.tileSize, data);
  const auto tmplsOpt = dataTable.get<sol::optional<sol::table>>("templates");
  if (!tmplsOpt.has_value()) {
    return std::nullopt;
  }

  bool foundExplicitBorder = false;
  for (const auto& [_, tmplVal] : *tmplsOpt) {
    if (!tmplVal.is<sol::table>()) continue;
    ChunkTemplate tmpl = ParseLuaTemplate(tmplVal.as<sol::table>(), tilesets, data);
    if (AppendParsedLuaTemplate(std::move(tmpl), explicitGridW.has_value(), explicitGridH.has_value(), data)) {
      foundExplicitBorder = true;
    }
  }

  if (data.templates.empty() && !foundExplicitBorder) {
    return std::nullopt;
  }
  if (data.templates.empty() && foundExplicitBorder) {
    data.templates.push_back(data.borderTemplate);
  }
  TilemapBuilder::FinalizeTilemapData(data, foundExplicitBorder);
  return data;
}

void BindLoadFunctions(sol::table& tm, LuaBindingContext& ctx) {
  tm.set_function("load", [&ctx](const std::string& path, const sol::optional<sol::table>& opts) -> bool {
    Registry* r = ctx.GetRegistry();
    auto* sys = EnsureTilemap(r);
    return (sys != nullptr) && sys->Load(*r, path, ParseLoadOptions(opts));
  });

  tm.set_function("load_json", [&ctx](const std::string& jsonText, const sol::optional<sol::table>& opts) -> bool {
    Registry* r = ctx.GetRegistry();
    auto* sys = EnsureTilemap(r);
    return (sys != nullptr) && sys->LoadFromJsonString(*r, jsonText, ParseLoadOptions(opts));
  });

  tm.set_function("load_data", [&ctx](const sol::table& dataTable, const sol::optional<sol::table>& opts) -> bool {
    Registry* r = ctx.GetRegistry();
    auto* sys = EnsureTilemap(r);
    if (sys == nullptr) {
      return false;
    }
    auto built = BuildTilemapDataFromLuaTable(dataTable);
    if (!built.has_value()) {
      return false;
    }
    return sys->LoadFromData(*r, std::move(*built), ParseLoadOptions(opts), "<lua_data>");
  });

  tm.set_function("unload", [&ctx]() {
    Registry* r = ctx.GetRegistry();
    if (auto* sys = EnsureTilemap(r)) sys->Unload(*r);
  });

  tm.set_function("reload", [&ctx]() -> bool {
    Registry* r = ctx.GetRegistry();
    auto* sys = EnsureTilemap(r);
    return (sys != nullptr) && sys->Reload(*r);
  });

  tm.set_function("is_loaded", [&ctx]() -> bool {
    const auto* sys = EnsureTilemap(ctx.GetRegistry());
    return (sys != nullptr) && sys->IsLoaded();
  });
}

void BindStitchFunctions(sol::table& tm, LuaBindingContext& ctx) {
  tm.set_function("set_stitch_mode",
                  [&ctx](const std::string& modeStr, const sol::optional<uint32_t>& seedOpt) -> void {
                    Registry* r = ctx.GetRegistry();
                    if (auto* sys = EnsureTilemap(r)) {
                      sys->SetStitchMode(*r, ParseStitchMode(modeStr), seedOpt.value_or(sys->GetSeed()));
                    }
                  });

  tm.set_function("get_stitch_mode", [&ctx]() -> std::string {
    const auto* sys = EnsureTilemap(ctx.GetRegistry());
    return StitchModeToString(sys != nullptr ? sys->GetStitchMode() : StitchMode::Toroidal);
  });

  tm.set_function("get_seed", [&ctx]() -> uint32_t {
    const auto* sys = EnsureTilemap(ctx.GetRegistry());
    return sys != nullptr ? sys->GetSeed() : 0u;
  });

  tm.set_function("set_window_radii", [&ctx](const int32_t loadRadius, const int32_t unloadRadius) {
    if (auto* sys = EnsureTilemap(ctx.GetRegistry())) {
      sys->SetWindowRadii(loadRadius, unloadRadius);
    }
  });

  tm.set_function(
      "set_chunk_override",
      [&ctx](const int32_t chunkX, const int32_t chunkY, const sol::optional<std::string>& tmplIdOpt) -> bool {
        Registry* r = ctx.GetRegistry();
        auto* sys = EnsureTilemap(r);
        return (sys != nullptr) && sys->SetChunkOverride(*r, chunkX, chunkY, tmplIdOpt.value_or(std::string{}));
      });

  tm.set_function("clear_chunk_overrides", [&ctx]() {
    Registry* r = ctx.GetRegistry();
    if (auto* sys = EnsureTilemap(r)) {
      sys->ClearChunkOverrides(*r);
    }
  });

  tm.set_function("get_chunk_template_id", [&ctx](const int32_t chunkX, const int32_t chunkY) -> std::string {
    const auto* sys = EnsureTilemap(ctx.GetRegistry());
    return sys != nullptr ? sys->GetChunkTemplateId(chunkX, chunkY) : std::string{};
  });
}

void BindPropFunctions(sol::table& tm, LuaBindingContext& ctx) {
  tm.set_function("set_prop_spawner", [&ctx](const sol::object& fnObj) {
    auto* sys = EnsureTilemap(ctx.GetRegistry());
    if (sys == nullptr) return;
    if (fnObj.is<sol::protected_function>()) {
      sys->SetPropSpawner(fnObj.as<sol::protected_function>());
    } else {
      sys->ClearPropSpawner();
    }
  });

  tm.set_function("mark_prop_consumed", [&ctx](const int32_t cx, const int32_t cy, const uint32_t propIdx) {
    if (auto* sys = EnsureTilemap(ctx.GetRegistry())) {
      sys->MarkPropConsumed(cx, cy, propIdx);
    }
  });

  tm.set_function("is_prop_consumed", [&ctx](const int32_t cx, const int32_t cy, const uint32_t propIdx) -> bool {
    const auto* sys = EnsureTilemap(ctx.GetRegistry());
    return (sys != nullptr) && sys->IsPropConsumed(cx, cy, propIdx);
  });

  tm.set_function("clear_consumed_props", [&ctx]() {
    if (auto* sys = EnsureTilemap(ctx.GetRegistry())) {
      sys->ClearConsumedProps();
    }
  });
}

sol::table BuildTemplatesLuaList(sol::state_view lua, const TilemapSystem* sys) {
  sol::table list = lua.create_table();
  if (sys == nullptr || !sys->IsLoaded()) {
    return list;
  }
  const auto& templates = sys->GetTilemapData().templates;
  for (size_t i = 0; i < templates.size(); ++i) {
    const auto& tmpl = templates[i];
    sol::table entry = lua.create_table();
    entry.set("id", tmpl.identifier);
    entry.set("biome", tmpl.biome);
    entry.set("weight", tmpl.weight);
    entry.set("edge_north", tmpl.edgeNorth);
    entry.set("edge_east", tmpl.edgeEast);
    entry.set("edge_south", tmpl.edgeSouth);
    entry.set("edge_west", tmpl.edgeWest);
    sol::table fieldsTbl = lua.create_table();
    for (const auto& [k, v] : tmpl.fields) {
      std::visit([&](const auto& val) { fieldsTbl.set(k, sol::make_object(lua, val)); }, v);
    }
    entry.set("fields", fieldsTbl);
    sol::table propsTbl = lua.create_table();
    for (size_t p = 0; p < tmpl.propSpawns.size(); ++p) {
      const auto& prop = tmpl.propSpawns[p];
      sol::table pt = lua.create_table();
      pt.set("id", prop.identifier);
      pt.set("x", prop.localX);
      pt.set("y", prop.localY);
      pt.set("width", prop.width);
      pt.set("height", prop.height);
      pt.set("pivot_x", prop.pivotX);
      pt.set("pivot_y", prop.pivotY);
      pt.set("index", prop.propIndex);
      sol::table propFieldsTbl = lua.create_table();
      for (const auto& [fk, fv] : prop.fields) {
        std::visit([&](const auto& val) { propFieldsTbl.set(fk, sol::make_object(lua, val)); }, fv);
      }
      pt.set("fields", propFieldsTbl);
      propsTbl.set(p + 1, pt);
    }
    entry.set("props", propsTbl);
    list.set(i + 1, entry);
  }
  return list;
}

void BindInspectionFunctions(sol::table& tm, LuaBindingContext& ctx) {
  tm.set_function("get_active_chunk_count", [&ctx]() -> size_t {
    const auto* sys = EnsureTilemap(ctx.GetRegistry());
    return sys != nullptr ? sys->GetActiveChunkCount() : 0u;
  });

  tm.set_function("get_camera_chunk", [&ctx]() -> std::tuple<int32_t, int32_t> {
    const auto* sys = EnsureTilemap(ctx.GetRegistry());
    if (sys == nullptr) return {0, 0};
    return {sys->GetCameraChunkX(), sys->GetCameraChunkY()};
  });

  tm.set_function("get_chunk_size", [&ctx]() -> std::tuple<int32_t, int32_t, int32_t, int32_t, int32_t> {
    const auto* sys = EnsureTilemap(ctx.GetRegistry());
    if (sys == nullptr) return {0, 0, 0, 0, 0};
    return {sys->GetChunkPixelWidth(), sys->GetChunkPixelHeight(), sys->GetChunkGridWidth(), sys->GetChunkGridHeight(),
            sys->GetTileSize()};
  });

  tm.set_function("get_templates", [&ctx](sol::this_state s) -> sol::table {
    return BuildTemplatesLuaList(sol::state_view(s), EnsureTilemap(ctx.GetRegistry()));
  });

  tm.set_function("set_debug_overlay", [&ctx](const bool showBounds, const bool showCollisions) {
    if (auto* sys = EnsureTilemap(ctx.GetRegistry())) {
      sys->SetShowChunkBounds(showBounds);
      sys->SetShowCollisionOverlay(showCollisions);
    }
  });
}

void BindCollisionQueryFunctions(sol::table& tm, LuaBindingContext& ctx) {
  tm.set_function("get_tile_at", [&ctx](const float wx, const float wy) -> int {
    const auto* sys = EnsureTilemap(ctx.GetRegistry());
    return static_cast<int>(sys != nullptr ? sys->GetTileAt(wx, wy) : static_cast<uint8_t>(kTileEmpty));
  });

  tm.set_function("is_solid_at", [&ctx](const float wx, const float wy, const sol::optional<uint8_t>& maskOpt) -> bool {
    const auto* sys = EnsureTilemap(ctx.GetRegistry());
    if (sys == nullptr) return false;
    const uint8_t mask = maskOpt.value_or(sys->GetDefaultSolidMask());
    return sys->IsSolidAt(wx, wy, mask);
  });

  tm.set_function(
      "raycast",
      [&ctx](const float x0, const float y0, const float x1, const float y1,
             const sol::optional<uint8_t>& maskOpt) -> std::tuple<bool, float, float, float, float, float, int> {
        const auto* sys = EnsureTilemap(ctx.GetRegistry());
        if (sys == nullptr) {
          return {false, x1, y1, 0.0f, 0.0f, 0.0f, 0};
        }
        const uint8_t mask = maskOpt.value_or(sys->GetDefaultRaycastMask());
        const auto hit = sys->Raycast(x0, y0, x1, y1, mask);
        return {hit.hit, hit.hitX, hit.hitY, hit.normalX, hit.normalY, hit.distance, static_cast<int>(hit.tileValue)};
      });

  tm.set_function("resolve_circle",
                  [&ctx](const float wx, const float wy, const float radius,
                         const sol::optional<uint8_t>& maskOpt) -> std::tuple<float, float> {
                    const auto* sys = EnsureTilemap(ctx.GetRegistry());
                    if (sys == nullptr) return {wx, wy};
                    const uint8_t mask = maskOpt.value_or(sys->GetDefaultSolidMask());
                    const glm::vec2 res = sys->ResolveCircle(wx, wy, radius, mask);
                    return {res.x, res.y};
                  });

  tm.set_function("resolve_aabb",
                  [&ctx](const float wx, const float wy, const float w, const float h,
                         const sol::optional<uint8_t>& maskOpt) -> std::tuple<float, float> {
                    const auto* sys = EnsureTilemap(ctx.GetRegistry());
                    if (sys == nullptr) return {wx, wy};
                    const uint8_t mask = maskOpt.value_or(sys->GetDefaultSolidMask());
                    const glm::vec2 res = sys->ResolveAABB(wx, wy, w, h, mask);
                    return {res.x, res.y};
                  });

  tm.set_function("set_default_masks", [&ctx](const uint8_t solidMask, const uint8_t raycastMask) {
    if (auto* sys = EnsureTilemap(ctx.GetRegistry())) {
      sys->SetDefaultMasks(solidMask, raycastMask);
    }
  });

  tm.set_function("get_default_masks", [&ctx]() -> std::tuple<uint8_t, uint8_t> {
    const auto* sys = EnsureTilemap(ctx.GetRegistry());
    if (sys == nullptr) {
      return {octarine::tilemap::kTileMaskGroundBlocked, octarine::tilemap::kTileMaskProjectileBlocked};
    }
    return {sys->GetDefaultSolidMask(), sys->GetDefaultRaycastMask()};
  });
}

}  // namespace

void LuaModuleBinding<TilemapModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  sol::table tm = lua.create_table();

  tm.set("TILE_EMPTY", static_cast<int>(octarine::tilemap::kTileEmpty));
  tm.set("TILE_1", static_cast<int>(kTileChannel1));
  tm.set("TILE_2", static_cast<int>(kTileChannel2));
  tm.set("TILE_3", static_cast<int>(kTileChannel3));
  tm.set("TILE_4", static_cast<int>(kTileChannel4));
  tm.set("TILE_5", static_cast<int>(kTileChannel5));
  tm.set("TILE_6", static_cast<int>(kTileChannel6));
  tm.set("TILE_7", static_cast<int>(kTileChannel7));

  tm.set("TILE_SOLID", static_cast<int>(octarine::tilemap::kTileSolid));
  tm.set("TILE_CHASM", static_cast<int>(octarine::tilemap::kTileChasm));
  tm.set("TILE_HAZARD", static_cast<int>(octarine::tilemap::kTileHazard));
  tm.set("TILE_SURFACE", static_cast<int>(kTileSurface));
  tm.set("TILE_WALL", static_cast<int>(kTileWall));

  tm.set("MASK_EMPTY", static_cast<int>(kTileMaskEmpty));
  tm.set("MASK_1", static_cast<int>(kTileMaskChannel1));
  tm.set("MASK_2", static_cast<int>(kTileMaskChannel2));
  tm.set("MASK_3", static_cast<int>(kTileMaskChannel3));
  tm.set("MASK_4", static_cast<int>(kTileMaskChannel4));
  tm.set("MASK_5", static_cast<int>(kTileMaskChannel5));
  tm.set("MASK_6", static_cast<int>(kTileMaskChannel6));
  tm.set("MASK_7", static_cast<int>(kTileMaskChannel7));

  tm.set("MASK_SOLID", static_cast<int>(kTileMaskSolid));
  tm.set("MASK_CHASM", static_cast<int>(kTileMaskChasm));
  tm.set("MASK_HAZARD", static_cast<int>(kTileMaskHazard));
  tm.set("MASK_SURFACE", static_cast<int>(kTileMaskSurface));
  tm.set("MASK_WALL", static_cast<int>(kTileMaskWall));
  tm.set("MASK_GROUND_BLOCKED", static_cast<int>(kTileMaskGroundBlocked));
  tm.set("MASK_PROJECTILE_BLOCKED", static_cast<int>(kTileMaskProjectileBlocked));

  BindLoadFunctions(tm, ctx);
  BindStitchFunctions(tm, ctx);
  BindPropFunctions(tm, ctx);
  BindInspectionFunctions(tm, ctx);
  BindCollisionQueryFunctions(tm, ctx);

  EnsureTilemap(ctx.GetRegistry());
  lua["tilemap"] = tm;
}
