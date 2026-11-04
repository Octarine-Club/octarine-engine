#include "Systems/TilemapSystem.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "AssetManager/AssetManager.h"
#include "Components/CameraComponents.h"
#include "Components/ChunkResidentComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/PositionComponent.h"
#include "ECS/Registry.h"
#include "Engine/EngineContext.h"
#include "Game/GameConfig.h"
#include "General/Constants.h"
#include "General/Logger.h"
#include "Renderer/RenderQueue.h"
#include "Systems/EntityPoolSystem.h"
#include "Tilemap/TilemapBuilder.h"
#include "Tilemap/TilemapLoader.h"
#include "Tilemap/TilemapMath.h"
#include "Tilemap/TilemapStitcher.h"

using octarine::tilemap::ActiveChunk;
using octarine::tilemap::ChunkTemplate;
using octarine::tilemap::FloorDiv;
using octarine::tilemap::GlobalTileToChunk;
using octarine::tilemap::GlobalTileToLocalTile;
using octarine::tilemap::kTileChasm;
using octarine::tilemap::kTileEmpty;
using octarine::tilemap::kTileHazard;
using octarine::tilemap::kTileSolid;
using octarine::tilemap::kTileSurface;
using octarine::tilemap::MatchesCollisionMask;
using octarine::tilemap::PackChunkKey;
using octarine::tilemap::PropFieldVal;
using octarine::tilemap::PropSpawnDesc;
using octarine::tilemap::RaycastTilemap;
using octarine::tilemap::ResolveAABBVsTilemap;
using octarine::tilemap::ResolveCircleVsTilemap;
using octarine::tilemap::StitchMode;
using octarine::tilemap::TilemapBuilder;
using octarine::tilemap::TilemapData;
using octarine::tilemap::TilemapLoaderRegistry;
using octarine::tilemap::TilemapLoadOptions;
using octarine::tilemap::TilemapStitcher;
using octarine::tilemap::TileRaycastHit;
using octarine::tilemap::WorldToChunk;
using octarine::tilemap::WorldToGlobalTile;

namespace {

constexpr unsigned int kDebugOverlayLayer = 950u;

void PopulateLuaFieldsTable(sol::state_view lua, sol::table& tbl,
                            const std::unordered_map<std::string, PropFieldVal>& fields) {
  for (const auto& [key, val] : fields) {
    std::visit([&](const auto& v) { tbl[key] = sol::make_object(lua, v); }, val);
  }
}

void RecordSpawnedEntity(Registry& registry, ActiveChunk& chunk, const Entity ent, const uint32_t propIndex) {
  if (!registry.IsAlive(ent)) {
    return;
  }
  registry.AddComponent<ChunkResidentComponent>(ent, ChunkResidentComponent(chunk.chunkX, chunk.chunkY, propIndex));
  if (registry.HasComponent<PositionComponent>(ent) && registry.HasComponent<GlobalTransformComponent>(ent)) {
    registry.GetComponent<GlobalTransformComponent>(ent).position = registry.GetComponent<PositionComponent>(ent).value;
  }
  chunk.spawnedProps.push_back(ent);
}

void RecordSpawnedResult(Registry& registry, ActiveChunk& chunk, const sol::object& retObj, const uint32_t propIndex) {
  if (retObj.is<Entity>()) {
    RecordSpawnedEntity(registry, chunk, retObj.as<Entity>(), propIndex);
  } else if (retObj.is<sol::table>()) {
    for (const auto& [_, elem] : retObj.as<sol::table>()) {
      if (elem.is<Entity>()) {
        RecordSpawnedEntity(registry, chunk, elem.as<Entity>(), propIndex);
      }
    }
  }
}

void InitChunkScreenVertices(ActiveChunk& chunk) {
  chunk.layerScreenVertices.clear();
  if (chunk.templatePtr == nullptr) {
    return;
  }
  chunk.layerScreenVertices.resize(chunk.templatePtr->staticLayers.size());
  for (size_t i = 0; i < chunk.templatePtr->staticLayers.size(); ++i) {
    chunk.layerScreenVertices[i] = chunk.templatePtr->staticLayers[i].localVertices;
  }
}

void ComputeCameraCenter(Registry& registry, float& outCamX, float& outCamY, float& outViewW, float& outViewH,
                         float& outZoom) {
  outCamX = 0.0f;
  outCamY = 0.0f;
  outViewW = 1280.0f;
  outViewH = 720.0f;
  outZoom = 1.0f;

  if (const auto* cfg = registry.TryGet<GameConfig>()) {
    outViewW = static_cast<float>(cfg->windowWidth);
    outViewH = static_cast<float>(cfg->windowHeight);
  }
  if (const auto* cam = registry.TryGet<CameraComponent>()) {
    outZoom = (cam->zoom > 0.001f) ? cam->zoom : 1.0f;
    outCamX = cam->viewport.x;
    outCamY = cam->viewport.y;
    outViewW = (cam->viewport.w > 0.0f) ? cam->viewport.w : (outViewW / outZoom);
    outViewH = (cam->viewport.h > 0.0f) ? cam->viewport.h : (outViewH / outZoom);
  }
}

}  // namespace

bool TilemapSystem::LoadFromData(Registry& registry, TilemapData data, const TilemapLoadOptions& options,
                                 const std::string& sourceLabel) {
  if (data.templates.empty() && !data.borderTemplate.isBorder) {
    return false;
  }

  Unload(registry);
  loaded_path_ = sourceLabel.empty() ? "<data>" : sourceLabel;
  loaded_full_path_.clear();
  loaded_format_hint_ = options.formatHint;
  tile_scale_ = std::clamp(options.tileScale, 0.25f, 16.0f);
  variant_count_ = std::clamp(options.variantCount, 1, 16);
  TilemapBuilder::ApplyWorldTileScale(data, tile_scale_);
  InvokeVariantGenerator(data, variant_count_, options.seed);
  project_data_ = std::move(data);
  stitch_mode_ = options.stitchMode;
  seed_ = options.seed;
  ysort_layer_ = options.ysortLayer;
  SetWindowRadii(options.loadRadius, options.unloadRadius);
  resolved_templates_.clear();
  ++map_revision_;
  is_loaded_ = true;

  EnsureTilesetTexturesLoaded(registry);

  float camX = 0.0f, camY = 0.0f, viewW = 1280.0f, viewH = 720.0f, zoom = 1.0f;
  ComputeCameraCenter(registry, camX, camY, viewW, viewH, zoom);
  camera_chunk_x_ = WorldToChunk(camX + viewW * 0.5f, GetChunkPixelWidth());
  camera_chunk_y_ = WorldToChunk(camY + viewH * 0.5f, GetChunkPixelHeight());
  UpdateActiveWindow(registry, camera_chunk_x_, camera_chunk_y_);
  return true;
}

bool TilemapSystem::Load(Registry& registry, const std::string& path, const TilemapLoadOptions& options) {
  const auto* assetManager = registry.TryGet<AssetManager>();
  auto parsed = TilemapLoaderRegistry::LoadFromFile(path, assetManager, options.formatHint);
  if (!parsed.has_value()) {
    return false;
  }

  if (!LoadFromData(registry, std::move(*parsed), options, path)) {
    return false;
  }

  loaded_path_ = path;
  loaded_full_path_ =
      (assetManager != nullptr && !std::filesystem::path(path).is_absolute()) ? assetManager->GetFullPath(path) : path;
  loaded_format_hint_ = options.formatHint;

  if (!loaded_full_path_.empty()) {
    file_watcher_.Track(loaded_full_path_);
  }
  return true;
}

bool TilemapSystem::LoadFromJsonString(Registry& registry, const std::string& jsonText,
                                       const TilemapLoadOptions& options, const std::string& baseDirRelPath) {
  const std::string formatHint = options.formatHint.empty() ? "ldtk" : options.formatHint;
  auto parsed = TilemapLoaderRegistry::LoadFromMemory(jsonText, baseDirRelPath, formatHint);
  if (!parsed.has_value()) {
    return false;
  }
  return LoadFromData(registry, std::move(*parsed), options, "<memory>");
}

void TilemapSystem::Unload(Registry& registry) {
  DespawnAllChunkProps(registry);
  active_chunks_.clear();
  chunk_overrides_.clear();
  resolved_templates_.clear();
  consumed_props_.clear();
  file_watcher_ = FileWatcher{};
  loaded_path_.clear();
  loaded_full_path_.clear();
  loaded_format_hint_.clear();
  project_data_ = TilemapData{};
  cached_texture_gen_ = 0;
  ++map_revision_;
  is_loaded_ = false;
}

bool TilemapSystem::Reload(Registry& registry) {
  if (!is_loaded_ || loaded_path_.empty() || loaded_path_.front() == '<') {
    return false;
  }
  const auto* assetManager = registry.TryGet<AssetManager>();
  auto parsed = TilemapLoaderRegistry::LoadFromFile(loaded_path_, assetManager, loaded_format_hint_);
  if (!parsed.has_value()) {
    Logger::Warn("TilemapSystem::Reload: keeping existing templates after parse failure in '" + loaded_path_ + "'");
    return false;
  }

  DespawnAllChunkProps(registry);
  active_chunks_.clear();
  resolved_templates_.clear();
  TilemapBuilder::ApplyWorldTileScale(*parsed, tile_scale_);
  InvokeVariantGenerator(*parsed, variant_count_, seed_);
  project_data_ = std::move(*parsed);
  EnsureTilesetTexturesLoaded(registry);
  UpdateActiveWindow(registry, camera_chunk_x_, camera_chunk_y_);
  ++hot_reload_count_;
  ++map_revision_;
  Logger::Info("TilemapSystem: hot-reloaded '" + loaded_path_ + "'");
  return true;
}

bool TilemapSystem::PollHotReload(Registry& registry, const float deltaTime, const float pollIntervalSeconds) {
  if (!is_loaded_ || loaded_full_path_.empty()) {
    return false;
  }
  hot_reload_accum_ += deltaTime;
  if (hot_reload_accum_ < pollIntervalSeconds) {
    return false;
  }
  hot_reload_accum_ = 0.0f;
  const auto changed = file_watcher_.Poll();
  if (changed.empty()) {
    return false;
  }
  return Reload(registry);
}

void TilemapSystem::SetStitchMode(Registry& registry, const StitchMode mode, const uint32_t seed) {
  if (stitch_mode_ == mode && seed_ == seed) {
    return;
  }
  const bool seedChanged = (seed_ != seed);
  stitch_mode_ = mode;
  seed_ = seed;
  resolved_templates_.clear();
  ++map_revision_;
  if (!is_loaded_) {
    return;
  }
  DespawnAllChunkProps(registry);
  active_chunks_.clear();
  if (seedChanged && variant_count_ > 1) {
    InvokeVariantGenerator(project_data_, variant_count_, seed_);
    cached_texture_gen_ = 0;
    RefreshTemplateTextureCache(registry.TryGet<AssetManager>());
  }
  UpdateActiveWindow(registry, camera_chunk_x_, camera_chunk_y_);
}

void TilemapSystem::SetWindowRadii(const int32_t loadRadius, const int32_t unloadRadius) {
  load_radius_ = std::max(0, loadRadius);
  unload_radius_ = std::max(load_radius_, unloadRadius);
}

void TilemapSystem::SetPropSpawner(sol::protected_function callback) { prop_spawner_ = std::move(callback); }

void TilemapSystem::ClearPropSpawner() { prop_spawner_ = sol::lua_nil; }

void TilemapSystem::SetVariantGenerator(octarine::tilemap::VariantGeneratorFn generator) {
  variant_generator_ = std::move(generator);
}

void TilemapSystem::ClearVariantGenerator() { variant_generator_ = {}; }

void TilemapSystem::InvokeVariantGenerator(TilemapData& data, const int32_t variantCount, const uint32_t seed) const {
  if (variant_generator_) {
    variant_generator_(data, variantCount, seed);
    return;
  }
  TilemapLoaderRegistry::ApplyVariantGenerator(data, variantCount, seed);
}

const ChunkTemplate* TilemapSystem::FindTemplateById(const std::string& templateId) const noexcept {
  if (templateId.empty()) {
    return nullptr;
  }
  for (const auto& tmpl : project_data_.templates) {
    if (tmpl.identifier == templateId) {
      return &tmpl;
    }
  }
  if (project_data_.borderTemplate.identifier == templateId) {
    return &project_data_.borderTemplate;
  }
  return nullptr;
}

bool TilemapSystem::SetChunkOverride(Registry& registry, const int32_t chunkX, const int32_t chunkY,
                                     const std::string& templateId) {
  const int64_t key = PackChunkKey(chunkX, chunkY);
  if (templateId.empty()) {
    chunk_overrides_.erase(key);
  } else {
    if (is_loaded_ && FindTemplateById(templateId) == nullptr) {
      return false;
    }
    chunk_overrides_[key] = templateId;
  }
  resolved_templates_.erase(key);
  ++map_revision_;

  if (const auto it = active_chunks_.find(key); it != active_chunks_.end()) {
    UnloadChunk(registry, it->second);
    it->second.templatePtr = nullptr;
    it->second.templatePtr = GetTemplateAtChunk(chunkX, chunkY);
    InitChunkScreenVertices(it->second);
    SpawnChunkProps(registry, it->second);
  }
  return true;
}

void TilemapSystem::ClearChunkOverrides(Registry& registry) {
  if (chunk_overrides_.empty()) {
    return;
  }
  chunk_overrides_.clear();
  resolved_templates_.clear();
  ++map_revision_;
  if (!is_loaded_) {
    return;
  }
  DespawnAllChunkProps(registry);
  active_chunks_.clear();
  UpdateActiveWindow(registry, camera_chunk_x_, camera_chunk_y_);
}

std::string TilemapSystem::GetChunkTemplateId(const int32_t chunkX, const int32_t chunkY) const {
  const ChunkTemplate* tmpl = GetTemplateAtChunk(chunkX, chunkY);
  return tmpl != nullptr ? tmpl->identifier : std::string{};
}

void TilemapSystem::MarkPropConsumed(const int32_t chunkX, const int32_t chunkY, const uint32_t propIndex) noexcept {
  if (propIndex >= 64u) {
    return;
  }
  consumed_props_[PackChunkKey(chunkX, chunkY)] |= (1ULL << propIndex);
}

bool TilemapSystem::IsPropConsumed(const int32_t chunkX, const int32_t chunkY,
                                   const uint32_t propIndex) const noexcept {
  if (propIndex >= 64u) {
    return false;
  }
  const auto it = consumed_props_.find(PackChunkKey(chunkX, chunkY));
  if (it == consumed_props_.end()) {
    return false;
  }
  return (it->second & (1ULL << propIndex)) != 0ULL;
}

void TilemapSystem::EnsureTilesetTexturesLoaded(Registry& registry) {
  auto* assetManager = registry.TryGet<AssetManager>();
  const auto* ctx = registry.TryGet<EngineContext>();
  if (assetManager != nullptr && ctx != nullptr && ctx->sdlRenderer != nullptr) {
    for (const auto& [assetId, relPath] : project_data_.tilesetAssets) {
      if (assetManager->GetTexture(assetId) != nullptr) {
        continue;
      }
      const CatalogEntry* entry = assetManager->GetCatalog().Find(assetId);
      if (entry == nullptr && !relPath.empty()) {
        const std::string stem = std::filesystem::path(relPath).stem().string();
        entry = assetManager->GetCatalog().Find(stem);
      }
      if (entry != nullptr) {
        assetManager->Acquire(entry->id, ctx->sdlRenderer, ctx->mixer);
      } else if (!relPath.empty()) {
        assetManager->AddTexture(ctx->sdlRenderer, assetId, relPath);
      }
    }
  }
  cached_texture_gen_ = 0;
  RefreshTemplateTextureCache(assetManager);
}

void TilemapSystem::RefreshTemplateTextureCache(AssetManager* assetManager) {
  const uint64_t currentGen = (assetManager != nullptr) ? assetManager->TextureGeneration() : 0u;
  if (cached_texture_gen_ != 0u && cached_texture_gen_ == currentGen) {
    return;
  }
  auto refreshTmpl = [&](ChunkTemplate& tmpl) {
    for (auto& layer : tmpl.staticLayers) {
      layer.cachedTexture = ResolveTilesetTexture(assetManager, layer.tilesetAssetId);
    }
    for (auto& ytile : tmpl.ysortTiles) {
      ytile.cachedTexture = ResolveTilesetTexture(assetManager, ytile.tilesetAssetId);
    }
  };
  for (auto& tmpl : project_data_.templates) {
    refreshTmpl(tmpl);
  }
  for (auto& [_, varList] : project_data_.templateVariants) {
    for (auto& var : varList) {
      refreshTmpl(var);
    }
  }
  refreshTmpl(project_data_.borderTemplate);
  cached_texture_gen_ = currentGen;
}

SDL_Texture* TilemapSystem::ResolveTilesetTexture(AssetManager* assetManager, const std::string& assetId) const {
  if (assetManager == nullptr || assetId.empty()) {
    return nullptr;
  }
  if (SDL_Texture* tex = assetManager->GetTexture(assetId)) {
    return tex;
  }
  for (const auto& [aid, relPath] : project_data_.tilesetAssets) {
    if (aid == assetId) {
      const std::string stem = std::filesystem::path(relPath).stem().string();
      if (SDL_Texture* tex = assetManager->GetTexture(stem)) {
        return tex;
      }
      if (const auto* entry = assetManager->GetCatalog().Find(stem)) {
        if (SDL_Texture* tex = assetManager->GetTexture(entry->id)) {
          return tex;
        }
      }
      break;
    }
  }
  return nullptr;
}

void TilemapSystem::UpdateActiveWindow(Registry& registry, const int32_t centerChunkX, const int32_t centerChunkY) {
  if (!is_loaded_) {
    return;
  }
  camera_chunk_x_ = centerChunkX;
  camera_chunk_y_ = centerChunkY;

  // Step 1: Unload chunks outside the outer hysteresis ring (unload_radius_).
  for (auto it = active_chunks_.begin(); it != active_chunks_.end();) {
    const int32_t dx = std::abs(it->second.chunkX - centerChunkX);
    const int32_t dy = std::abs(it->second.chunkY - centerChunkY);
    if (dx > unload_radius_ || dy > unload_radius_) {
      UnloadChunk(registry, it->second);
      it = active_chunks_.erase(it);
    } else {
      ++it;
    }
  }

  // Step 2: Load missing chunks inside the inner window (load_radius_).
  for (int32_t cy = centerChunkY - load_radius_; cy <= centerChunkY + load_radius_; ++cy) {
    for (int32_t cx = centerChunkX - load_radius_; cx <= centerChunkX + load_radius_; ++cx) {
      const int64_t key = PackChunkKey(cx, cy);
      if (!active_chunks_.contains(key)) {
        LoadChunk(registry, cx, cy);
      }
    }
  }
}

void TilemapSystem::LoadChunk(Registry& registry, const int32_t chunkX, const int32_t chunkY) {
  ActiveChunk chunk;
  chunk.chunkX = chunkX;
  chunk.chunkY = chunkY;
  chunk.templatePtr = GetTemplateAtChunk(chunkX, chunkY);
  InitChunkScreenVertices(chunk);
  SpawnChunkProps(registry, chunk);
  active_chunks_.emplace(PackChunkKey(chunkX, chunkY), std::move(chunk));
}

void TilemapSystem::UnloadChunk(Registry& registry, ActiveChunk& chunk) {
  auto* pool = registry.TryGet<EntityPoolManager>();
  for (const Entity ent : chunk.spawnedProps) {
    if (!registry.IsAlive(ent)) {
      continue;
    }
    if (pool != nullptr && registry.HasTag<PoolableTag>(ent)) {
      registry.RemoveComponent<ChunkResidentComponent>(ent);
      pool->Park(registry, ent);
    } else {
      registry.BlamEntity(ent);
    }
  }
  chunk.spawnedProps.clear();
}

void TilemapSystem::DespawnAllChunkProps(Registry& registry) {
  for (auto& [key, chunk] : active_chunks_) {
    UnloadChunk(registry, chunk);
  }
}

namespace {

bool ShouldSpawnProp(const PropSpawnDesc& prop, const int32_t chunkX, const int32_t chunkY,
                     const uint32_t seed) noexcept {
  for (const char* chanceKey : {"spawn_chance", "SpawnChance"}) {
    const auto chanceIt = prop.fields.find(chanceKey);
    if (chanceIt == prop.fields.end()) {
      continue;
    }
    double chance = 1.0;
    if (const auto* d = std::get_if<double>(&chanceIt->second)) {
      chance = *d;
    } else if (const auto* i = std::get_if<int64_t>(&chanceIt->second)) {
      chance = static_cast<double>(*i);
    }
    if (chance >= 1.0) {
      return true;
    }
    const uint64_t rollHash =
        octarine::tilemap::HashChunkCoord(chunkX, chunkY, seed ^ ((prop.propIndex + 1u) * 0x9E3779B9u));
    const double roll01 = static_cast<double>(rollHash & 0x00FFFFFFu) / static_cast<double>(0x01000000u);
    return roll01 < std::clamp(chance, 0.0, 1.0);
  }
  return true;
}

}  // namespace

void TilemapSystem::SpawnChunkProps(Registry& registry, ActiveChunk& chunk) const {
  if (chunk.templatePtr == nullptr || !prop_spawner_.valid()) {
    return;
  }
  const auto originX = static_cast<float>(chunk.chunkX * GetChunkPixelWidth());
  const auto originY = static_cast<float>(chunk.chunkY * GetChunkPixelHeight());
  sol::state_view lua(prop_spawner_.lua_state());

  for (const PropSpawnDesc& prop : chunk.templatePtr->propSpawns) {
    if (IsPropConsumed(chunk.chunkX, chunk.chunkY, prop.propIndex) ||
        !ShouldSpawnProp(prop, chunk.chunkX, chunk.chunkY, seed_)) {
      continue;
    }

    sol::table fieldsTbl = lua.create_table();
    PopulateLuaFieldsTable(lua, fieldsTbl, prop.fields);

    const float worldX = originX + prop.localX;
    const float worldY = originY + prop.localY;
    auto res = prop_spawner_(prop.identifier, worldX, worldY, fieldsTbl, chunk.chunkX, chunk.chunkY, prop.propIndex);
    if (!res.valid()) {
      const sol::error err = res;
      Logger::ErrorLua(std::string("tilemap prop_spawner error: ") + err.what());
      continue;
    }
    if (res.return_count() > 0) {
      RecordSpawnedResult(registry, chunk, res.get<sol::object>(), prop.propIndex);
    }
  }
}

void TilemapSystem::Tick(Registry* registry) {
  if (registry == nullptr || !is_loaded_) {
    return;
  }

  float camX = 0.0f, camY = 0.0f, viewW = 1280.0f, viewH = 720.0f, zoom = 1.0f;
  ComputeCameraCenter(*registry, camX, camY, viewW, viewH, zoom);

  const int32_t centerChunkX = WorldToChunk(camX + viewW * 0.5f, GetChunkPixelWidth());
  const int32_t centerChunkY = WorldToChunk(camY + viewH * 0.5f, GetChunkPixelHeight());
  if (centerChunkX != camera_chunk_x_ || centerChunkY != camera_chunk_y_ || active_chunks_.empty()) {
    UpdateActiveWindow(*registry, centerChunkX, centerChunkY);
  }

  auto* renderQueue = registry->TryGet<RenderQueue>();
  if (renderQueue == nullptr) {
    return;
  }
  auto* assetManager = registry->TryGet<AssetManager>();
  SubmitChunkRenderCommands(*registry, *renderQueue, assetManager, camX, camY, viewW, viewH, zoom);
  if (show_chunk_bounds_ || show_collision_overlay_) {
    SubmitDebugOverlays(*renderQueue, camX, camY, zoom);
  }
}

namespace {

void SubmitChunkStaticLayers(ActiveChunk& chunk, RenderQueue& renderQueue, const float screenOriginX,
                             const float screenOriginY, const float zoom) {
  for (size_t layerIdx = 0; layerIdx < chunk.templatePtr->staticLayers.size(); ++layerIdx) {
    const auto& layer = chunk.templatePtr->staticLayers[layerIdx];
    auto& screenVerts = chunk.layerScreenVertices[layerIdx];
    if (screenVerts.empty() || layer.indices.empty()) continue;

    for (size_t v = 0; v < screenVerts.size(); ++v) {
      const auto& localPos = layer.localVertices[v].position;
      screenVerts[v].position.x = std::round(screenOriginX + localPos.x * zoom);
      screenVerts[v].position.y = std::round(screenOriginY + localPos.y * zoom);
    }

    SDL_Texture* tex = layer.cachedTexture;
    auto& cmd = renderQueue.EmplaceTileChunk(layer.renderLayer, layer.renderDepth, tex);
    cmd.vertices = screenVerts.data();
    cmd.indices = layer.indices.data();
    cmd.vertexCount = static_cast<int>(screenVerts.size());
    cmd.indexCount = static_cast<int>(layer.indices.size());
    cmd.texture = tex;
    cmd.blendMode = SDL_BLENDMODE_BLEND;
  }
}

void SubmitChunkYSortTiles(const ActiveChunk& chunk, RenderQueue& renderQueue, const unsigned int ysortLayer,
                           const float worldOriginY, const float screenOriginX, const float screenOriginY,
                           const float zoom) {
  for (const auto& ytile : chunk.templatePtr->ysortTiles) {
    SDL_Texture* tex = ytile.cachedTexture;
    const float worldSortY = worldOriginY + ytile.sortLocalY;
    auto& scmd = renderQueue.EmplaceSprite(ysortLayer, worldSortY, tex);
    const float x0 = std::round(screenOriginX + ytile.localX * zoom);
    const float y0 = std::round(screenOriginY + ytile.localY * zoom);
    const float x1 = std::round(screenOriginX + (ytile.localX + ytile.width) * zoom);
    const float y1 = std::round(screenOriginY + (ytile.localY + ytile.height) * zoom);
    scmd.destX = x0;
    scmd.destY = y0;
    scmd.destW = std::max(1.0f, x1 - x0);
    scmd.destH = std::max(1.0f, y1 - y0);
    scmd.srcRect = ytile.srcRect;
    scmd.rotation = 0.0f;
    scmd.pivot = SDL_FPoint{0.0f, 0.0f};
    scmd.flip = ytile.flip;
    scmd.texture = tex;
    scmd.colorMod = SDL_Color{Constants::kUint8Max, Constants::kUint8Max, Constants::kUint8Max, Constants::kUint8Max};
    scmd.blendMode = SDL_BLENDMODE_BLEND;
  }
}

void SubmitChunkBoundsOverlay(RenderQueue& renderQueue, const float sx, const float sy, const float sw,
                              const float sh) {
  constexpr SDL_Color kBoundColor{0, 220, 255, 190};
  const SDL_FPoint p0{sx, sy};
  const SDL_FPoint p1{sx + sw, sy};
  const SDL_FPoint p2{sx + sw, sy + sh};
  const SDL_FPoint p3{sx, sy + sh};
  renderQueue.EmplaceLine(kDebugOverlayLayer, 0.0f) = {p0, p1, 1.5f, kBoundColor, SDL_BLENDMODE_BLEND};
  renderQueue.EmplaceLine(kDebugOverlayLayer, 0.0f) = {p1, p2, 1.5f, kBoundColor, SDL_BLENDMODE_BLEND};
  renderQueue.EmplaceLine(kDebugOverlayLayer, 0.0f) = {p2, p3, 1.5f, kBoundColor, SDL_BLENDMODE_BLEND};
  renderQueue.EmplaceLine(kDebugOverlayLayer, 0.0f) = {p3, p0, 1.5f, kBoundColor, SDL_BLENDMODE_BLEND};
}

SDL_Color CollisionValueToDebugColor(const uint8_t val) noexcept {
  if (val == kTileChasm) {
    return SDL_Color{70, 110, 255, 110};
  }
  if (val == kTileHazard) {
    return SDL_Color{255, 180, 30, 120};
  }
  if (val == kTileSurface) {
    return SDL_Color{235, 210, 120, 90};
  }
  return SDL_Color{255, 60, 60, 110};
}

void SubmitChunkCollisionOverlay(RenderQueue& renderQueue, const ChunkTemplate& tmpl, const float sx, const float sy,
                                 const float tileScreenSize) {
  for (int32_t ty = 0; ty < tmpl.gridHeight; ++ty) {
    for (int32_t tx = 0; tx < tmpl.gridWidth; ++tx) {
      const uint8_t val = tmpl.GetCollisionLocal(tx, ty);
      if (val == kTileEmpty) continue;
      auto& sq = renderQueue.EmplaceSquare(kDebugOverlayLayer, 0.0f);
      sq.destRect = SDL_FRect{sx + static_cast<float>(tx) * tileScreenSize,
                              sy + static_cast<float>(ty) * tileScreenSize, tileScreenSize, tileScreenSize};
      sq.color = CollisionValueToDebugColor(val);
      sq.rotation = 0.0f;
      sq.pivot = SDL_FPoint{0.0f, 0.0f};
      sq.blendMode = SDL_BLENDMODE_BLEND;
    }
  }
}

}  // namespace

void TilemapSystem::SubmitChunkRenderCommands(Registry& /*registry*/, RenderQueue& renderQueue,
                                              AssetManager* assetManager, const float camX, const float camY,
                                              const float viewW, const float viewH, const float zoom) {
  RefreshTemplateTextureCache(assetManager);
  const auto chunkPxW = static_cast<float>(GetChunkPixelWidth());
  const auto chunkPxH = static_cast<float>(GetChunkPixelHeight());
  const auto cullMargin = static_cast<float>(project_data_.tileSize * 2);

  for (auto& [key, chunk] : active_chunks_) {
    if (chunk.templatePtr == nullptr) continue;
    const float worldOriginX = static_cast<float>(chunk.chunkX) * chunkPxW;
    const float worldOriginY = static_cast<float>(chunk.chunkY) * chunkPxH;

    if (worldOriginX + chunkPxW + cullMargin < camX || worldOriginX - cullMargin > camX + viewW ||
        worldOriginY + chunkPxH + cullMargin < camY || worldOriginY - cullMargin > camY + viewH) {
      continue;
    }

    // Use exact world-to-screen coordinates matching RenderSpriteSystem and RenderPrimitiveSystem
    // so tilemap layers, Y-sort props, and world entities pan and zoom together without relative sliding.
    const float screenOriginX = (worldOriginX - camX) * zoom;
    const float screenOriginY = (worldOriginY - camY) * zoom;

    SubmitChunkStaticLayers(chunk, renderQueue, screenOriginX, screenOriginY, zoom);
    SubmitChunkYSortTiles(chunk, renderQueue, ysort_layer_, worldOriginY, screenOriginX, screenOriginY, zoom);
  }
}

void TilemapSystem::SubmitDebugOverlays(RenderQueue& renderQueue, const float camX, const float camY,
                                        const float zoom) const {
  const auto chunkPxW = static_cast<float>(GetChunkPixelWidth());
  const auto chunkPxH = static_cast<float>(GetChunkPixelHeight());
  const float tileScreenSize = static_cast<float>(project_data_.tileSize) * zoom;

  for (const auto& [key, chunk] : active_chunks_) {
    const float worldOriginX = static_cast<float>(chunk.chunkX) * chunkPxW;
    const float worldOriginY = static_cast<float>(chunk.chunkY) * chunkPxH;
    const float sx = (worldOriginX - camX) * zoom;
    const float sy = (worldOriginY - camY) * zoom;

    if (show_chunk_bounds_) {
      SubmitChunkBoundsOverlay(renderQueue, sx, sy, chunkPxW * zoom, chunkPxH * zoom);
    }
    if (show_collision_overlay_ && chunk.templatePtr != nullptr) {
      SubmitChunkCollisionOverlay(renderQueue, *chunk.templatePtr, sx, sy, tileScreenSize);
    }
  }
}

const ChunkTemplate* TilemapSystem::GetTemplateAtChunk(const int32_t chunkX, const int32_t chunkY) const noexcept {
  if (!is_loaded_) {
    return nullptr;
  }
  const int64_t key = PackChunkKey(chunkX, chunkY);
  if (const auto cacheIt = resolved_templates_.find(key); cacheIt != resolved_templates_.end()) {
    return cacheIt->second;
  }
  if (const auto it = active_chunks_.find(key); it != active_chunks_.end() && it->second.templatePtr != nullptr) {
    resolved_templates_.emplace(key, it->second.templatePtr);
    return it->second.templatePtr;
  }
  const ChunkTemplate* baseTmpl = nullptr;
  if (const auto ovIt = chunk_overrides_.find(key); ovIt != chunk_overrides_.end()) {
    baseTmpl = FindTemplateById(ovIt->second);
  }
  if (baseTmpl == nullptr) {
    baseTmpl = TilemapStitcher::SelectTemplate(project_data_, stitch_mode_, seed_, chunkX, chunkY);
  }
  const ChunkTemplate* resolvedTmpl = baseTmpl;
  if (baseTmpl != nullptr) {
    const auto varIt = project_data_.templateVariants.find(baseTmpl->identifier);
    if (varIt != project_data_.templateVariants.end() && !varIt->second.empty()) {
      const uint64_t h = octarine::tilemap::HashChunkCoord(chunkX, chunkY, seed_ ^ 0xA5A55A5Au);
      resolvedTmpl = &varIt->second[static_cast<size_t>(h % varIt->second.size())];
    }
  }
  resolved_templates_.emplace(key, resolvedTmpl);
  return resolvedTmpl;
}

uint8_t TilemapSystem::GetTileByGlobalCoord(const int32_t globalTx, const int32_t globalTy) const noexcept {
  if (!is_loaded_) {
    return kTileEmpty;
  }
  const int32_t cx = GlobalTileToChunk(globalTx, project_data_.chunkGridWidth);
  const int32_t cy = GlobalTileToChunk(globalTy, project_data_.chunkGridHeight);
  const ChunkTemplate* tmpl = GetTemplateAtChunk(cx, cy);
  if (tmpl == nullptr) {
    return kTileEmpty;
  }
  const int32_t lx = GlobalTileToLocalTile(globalTx, project_data_.chunkGridWidth);
  const int32_t ly = GlobalTileToLocalTile(globalTy, project_data_.chunkGridHeight);
  return tmpl->GetCollisionLocal(lx, ly);
}

uint8_t TilemapSystem::GetTileAt(const float worldX, const float worldY) const noexcept {
  if (!is_loaded_) {
    return kTileEmpty;
  }
  const int32_t gx = WorldToGlobalTile(worldX, project_data_.tileSize);
  const int32_t gy = WorldToGlobalTile(worldY, project_data_.tileSize);
  return GetTileByGlobalCoord(gx, gy);
}

bool TilemapSystem::IsSolidAt(const float worldX, const float worldY, const uint8_t collisionMask) const noexcept {
  return MatchesCollisionMask(GetTileAt(worldX, worldY), collisionMask);
}

TileRaycastHit TilemapSystem::Raycast(const float x0, const float y0, const float x1, const float y1,
                                      const uint8_t collisionMask) const noexcept {
  if (!is_loaded_) {
    return TileRaycastHit{false, x1, y1, 0.0f, 0.0f, std::hypot(x1 - x0, y1 - y0), kTileEmpty, 0, 0};
  }
  return RaycastTilemap(x0, y0, x1, y1, project_data_.tileSize, collisionMask,
                        [this](const int32_t gx, const int32_t gy) { return GetTileByGlobalCoord(gx, gy); });
}

glm::vec2 TilemapSystem::ResolveCircle(const float worldX, const float worldY, const float radius,
                                       const uint8_t collisionMask) const noexcept {
  if (!is_loaded_) {
    return {worldX, worldY};
  }
  return ResolveCircleVsTilemap(worldX, worldY, radius, project_data_.tileSize, collisionMask,
                                [this](const int32_t gx, const int32_t gy) { return GetTileByGlobalCoord(gx, gy); });
}

glm::vec2 TilemapSystem::ResolveAABB(const float worldX, const float worldY, const float width, const float height,
                                     const uint8_t collisionMask) const noexcept {
  if (!is_loaded_) {
    return {worldX, worldY};
  }
  return ResolveAABBVsTilemap(worldX, worldY, width, height, project_data_.tileSize, collisionMask,
                              [this](const int32_t gx, const int32_t gy) { return GetTileByGlobalCoord(gx, gy); });
}

void TilemapRenderSystem::operator()(const ContextFacade& ctx) const {
  auto* registry = ctx.GetRegistry();
  if (auto* tilemap = registry->TryGet<TilemapSystem>()) {
    tilemap->Tick(registry);
  }
}
