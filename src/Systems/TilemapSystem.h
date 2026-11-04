#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "ECS/Entity.h"
#include "General/FileWatcher.h"
#include "Tilemap/ChunkTemplate.h"
#include "Tilemap/TilemapCollision.h"
#include "Tilemap/TilemapLoader.h"

class AssetManager;
class ContextFacade;
class Registry;
class RenderQueue;

namespace octarine::tilemap {

struct ActiveChunk {
  int32_t chunkX = 0;
  int32_t chunkY = 0;
  const ChunkTemplate* templatePtr = nullptr;
  // Per-static-layer screen-transformed vertex buffers fed to SDL_RenderGeometry.
  std::vector<std::vector<SDL_Vertex>> layerScreenVertices{};
  // Entities spawned from the chunk's Entities layer while resident.
  std::vector<Entity> spawnedProps{};
};

struct TilemapLoadOptions {
  StitchMode stitchMode = StitchMode::Toroidal;
  uint32_t seed = 1337u;
  int32_t loadRadius = 1;    // 1 => 3x3 inner load window
  int32_t unloadRadius = 2;  // 2 => 5x5 outer hysteresis unload window
  unsigned int ysortLayer = 3u;
  float tileScale =
      1.0f;  // World-space multiplier applied to tile/chunk dimensions (e.g. 2.0f => 16px tiles become 32px in world)
  int32_t variantCount = 1;  // > 1 generates randomized prop/decal variants per template at load time
  std::string formatHint{};  // Optional format id override (e.g. "ldtk"); empty auto-detects by extension
};

}  // namespace octarine::tilemap

class TilemapSystem {
 public:
  TilemapSystem() = default;
  ~TilemapSystem() = default;

  TilemapSystem(const TilemapSystem&) = delete;
  TilemapSystem& operator=(const TilemapSystem&) = delete;
  TilemapSystem(TilemapSystem&&) noexcept = default;
  TilemapSystem& operator=(TilemapSystem&&) noexcept = default;

  // Loads a tilemap file from disk or AssetPak via TilemapLoaderRegistry and initializes the active chunk window.
  bool Load(Registry& registry, const std::string& path, const octarine::tilemap::TilemapLoadOptions& options = {});

  // Loads directly from an in-memory tilemap string via TilemapLoaderRegistry (defaults to "ldtk" when formatHint is
  // empty).
  bool LoadFromJsonString(Registry& registry, const std::string& jsonText,
                          const octarine::tilemap::TilemapLoadOptions& options = {},
                          const std::string& baseDirRelPath = "");

  // Loads directly from an in-memory TilemapData structure (used by procedural builders and tilemap.load_data).
  bool LoadFromData(Registry& registry, octarine::tilemap::TilemapData data,
                    const octarine::tilemap::TilemapLoadOptions& options = {},
                    const std::string& sourceLabel = "<data>");

  // Unloads all active chunks, despawning/parking resident props, and clears the loaded project.
  void Unload(Registry& registry);

  // Re-parses the currently loaded tilemap file and rebuilds active chunk meshes in-place.
  bool Reload(Registry& registry);

  // Polls FileWatcher for tilemap file timestamp changes and triggers Reload() when modified on disk.
  bool PollHotReload(Registry& registry, float deltaTime, float pollIntervalSeconds = 0.25f);

  // Per-frame update & render queue submission.
  void Tick(Registry* registry);

  // Updates the active sliding window around (centerChunkX, centerChunkY) with hysteresis.
  void UpdateActiveWindow(Registry& registry, int32_t centerChunkX, int32_t centerChunkY);

  // Stitching & configuration mutators.
  void SetStitchMode(Registry& registry, octarine::tilemap::StitchMode mode, uint32_t seed);
  void SetWindowRadii(int32_t loadRadius, int32_t unloadRadius);
  void SetYSortLayer(unsigned int layer) noexcept { ysort_layer_ = layer; }
  void SetPropSpawner(sol::protected_function callback);
  void ClearPropSpawner();
  void SetVariantGenerator(octarine::tilemap::VariantGeneratorFn generator);
  void ClearVariantGenerator();
  bool SetChunkOverride(Registry& registry, int32_t chunkX, int32_t chunkY, const std::string& templateId);
  void ClearChunkOverrides(Registry& registry);
  [[nodiscard]] std::string GetChunkTemplateId(int32_t chunkX, int32_t chunkY) const;
  [[nodiscard]] const octarine::tilemap::ChunkTemplate* FindTemplateById(const std::string& templateId) const noexcept;

  // Consumed prop tracking per (chunkX, chunkY) coordinate.
  void MarkPropConsumed(int32_t chunkX, int32_t chunkY, uint32_t propIndex) noexcept;
  [[nodiscard]] bool IsPropConsumed(int32_t chunkX, int32_t chunkY, uint32_t propIndex) const noexcept;
  void ClearConsumedProps() noexcept { consumed_props_.clear(); }

  // O(1) spatial & collision queries (seamlessly support negative coordinates across (0, 0)).
  [[nodiscard]] uint8_t GetTileByGlobalCoord(int32_t globalTx, int32_t globalTy) const noexcept;
  [[nodiscard]] uint8_t GetTileAt(float worldX, float worldY) const noexcept;
  [[nodiscard]] bool IsSolidAt(float worldX, float worldY,
                               uint8_t collisionMask = octarine::tilemap::kTileMaskGroundBlocked) const noexcept;
  [[nodiscard]] octarine::tilemap::TileRaycastHit Raycast(
      float x0, float y0, float x1, float y1, uint8_t collisionMask = octarine::tilemap::kTileMaskSolid) const noexcept;
  [[nodiscard]] glm::vec2 ResolveCircle(
      float worldX, float worldY, float radius,
      uint8_t collisionMask = octarine::tilemap::kTileMaskGroundBlocked) const noexcept;
  [[nodiscard]] glm::vec2 ResolveAABB(float worldX, float worldY, float width, float height,
                                      uint8_t collisionMask = octarine::tilemap::kTileMaskGroundBlocked) const noexcept;

  // Inspection & debug overlay state.
  [[nodiscard]] bool IsLoaded() const noexcept { return is_loaded_; }
  [[nodiscard]] const std::string& GetLoadedPath() const noexcept { return loaded_path_; }
  [[nodiscard]] octarine::tilemap::StitchMode GetStitchMode() const noexcept { return stitch_mode_; }
  [[nodiscard]] uint32_t GetSeed() const noexcept { return seed_; }
  [[nodiscard]] int32_t GetLoadRadius() const noexcept { return load_radius_; }
  [[nodiscard]] int32_t GetUnloadRadius() const noexcept { return unload_radius_; }
  [[nodiscard]] unsigned int GetYSortLayer() const noexcept { return ysort_layer_; }
  [[nodiscard]] float GetTileScale() const noexcept { return tile_scale_; }
  [[nodiscard]] int32_t GetTileSize() const noexcept { return project_data_.tileSize; }
  [[nodiscard]] int32_t GetChunkGridWidth() const noexcept { return project_data_.chunkGridWidth; }
  [[nodiscard]] int32_t GetChunkGridHeight() const noexcept { return project_data_.chunkGridHeight; }
  [[nodiscard]] int32_t GetChunkPixelWidth() const noexcept {
    return project_data_.chunkGridWidth * project_data_.tileSize;
  }
  [[nodiscard]] int32_t GetChunkPixelHeight() const noexcept {
    return project_data_.chunkGridHeight * project_data_.tileSize;
  }
  [[nodiscard]] size_t GetActiveChunkCount() const noexcept { return active_chunks_.size(); }
  [[nodiscard]] size_t GetTemplateCount() const noexcept { return project_data_.templates.size(); }
  [[nodiscard]] int32_t GetCameraChunkX() const noexcept { return camera_chunk_x_; }
  [[nodiscard]] int32_t GetCameraChunkY() const noexcept { return camera_chunk_y_; }
  [[nodiscard]] uint64_t GetHotReloadCount() const noexcept { return hot_reload_count_; }
  [[nodiscard]] const std::unordered_map<int64_t, octarine::tilemap::ActiveChunk>& GetActiveChunks() const noexcept {
    return active_chunks_;
  }
  [[nodiscard]] const octarine::tilemap::TilemapData& GetTilemapData() const noexcept { return project_data_; }
  [[nodiscard]] const octarine::tilemap::TilemapData& GetProjectData() const noexcept { return project_data_; }

  void SetShowChunkBounds(const bool show) noexcept { show_chunk_bounds_ = show; }
  [[nodiscard]] bool GetShowChunkBounds() const noexcept { return show_chunk_bounds_; }
  void SetShowCollisionOverlay(const bool show) noexcept { show_collision_overlay_ = show; }
  [[nodiscard]] bool GetShowCollisionOverlay() const noexcept { return show_collision_overlay_; }

 private:
  [[nodiscard]] const octarine::tilemap::ChunkTemplate* GetTemplateAtChunk(int32_t chunkX,
                                                                           int32_t chunkY) const noexcept;
  void InvokeVariantGenerator(octarine::tilemap::TilemapData& data, int32_t variantCount, uint32_t seed) const;
  void EnsureTilesetTexturesLoaded(Registry& registry);
  void RefreshTemplateTextureCache(AssetManager* assetManager);
  [[nodiscard]] SDL_Texture* ResolveTilesetTexture(AssetManager* assetManager, const std::string& assetId) const;
  void LoadChunk(Registry& registry, int32_t chunkX, int32_t chunkY);
  void UnloadChunk(Registry& registry, octarine::tilemap::ActiveChunk& chunk);
  void DespawnAllChunkProps(Registry& registry);
  void SpawnChunkProps(Registry& registry, octarine::tilemap::ActiveChunk& chunk) const;
  void SubmitChunkRenderCommands(Registry& registry, RenderQueue& renderQueue, AssetManager* assetManager, float camX,
                                 float camY, float viewW, float viewH, float zoom);
  void SubmitDebugOverlays(RenderQueue& renderQueue, float camX, float camY, float zoom) const;

  bool is_loaded_ = false;
  std::string loaded_path_{};
  std::string loaded_full_path_{};
  std::string loaded_format_hint_{};
  octarine::tilemap::TilemapData project_data_{};
  uint64_t cached_texture_gen_ = 0;
  octarine::tilemap::StitchMode stitch_mode_ = octarine::tilemap::StitchMode::Toroidal;
  uint32_t seed_ = 1337u;
  int32_t load_radius_ = 1;
  int32_t unload_radius_ = 2;
  unsigned int ysort_layer_ = 3u;
  float tile_scale_ = 1.0f;
  int32_t variant_count_ = 1;
  int32_t camera_chunk_x_ = 0;
  int32_t camera_chunk_y_ = 0;

  std::unordered_map<int64_t, octarine::tilemap::ActiveChunk> active_chunks_{};
  std::unordered_map<int64_t, std::string> chunk_overrides_{};
  std::unordered_map<int64_t, uint64_t> consumed_props_{};
  sol::protected_function prop_spawner_{};
  octarine::tilemap::VariantGeneratorFn variant_generator_{};

  FileWatcher file_watcher_{};
  float hot_reload_accum_ = 0.0f;
  uint64_t hot_reload_count_ = 0;

  bool show_chunk_bounds_ = false;
  bool show_collision_overlay_ = false;
};

// Lightweight ECS BulkSystem wrapper that ticks the Registry's TilemapSystem singleton each frame.
class TilemapRenderSystem {
 public:
  void operator()(const ContextFacade& ctx) const;
};
