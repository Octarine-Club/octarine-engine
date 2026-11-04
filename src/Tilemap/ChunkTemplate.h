#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace octarine::tilemap {

enum TileCollision : uint8_t {
  kTileEmpty = 0,
  kTileSolid = 1,
  kTileChasm = 2,
  kTileHazard = 3,
  kTileSurface = 4,
};

constexpr uint8_t kTileMaskEmpty = static_cast<uint8_t>(1u << kTileEmpty);
constexpr uint8_t kTileMaskSolid = static_cast<uint8_t>(1u << kTileSolid);
constexpr uint8_t kTileMaskChasm = static_cast<uint8_t>(1u << kTileChasm);
constexpr uint8_t kTileMaskHazard = static_cast<uint8_t>(1u << kTileHazard);
constexpr uint8_t kTileMaskGroundBlocked = static_cast<uint8_t>(kTileMaskSolid | kTileMaskChasm);

constexpr int32_t kDefaultTileSize = 16;
constexpr int32_t kDefaultChunkGridSize = 32;
constexpr int32_t kMaxChunkGridSize = 64;
constexpr size_t kMaxChunkArea = static_cast<size_t>(kMaxChunkGridSize) * static_cast<size_t>(kMaxChunkGridSize);

enum class StitchMode : uint8_t {
  Toroidal = 0,
  ProceduralHash = 1,
  ArenaFixed = 2,
};

struct TileLayerMesh {
  std::string layerName{};
  std::string tilesetAssetId{};
  std::string tilesetRelPath{};
  SDL_Texture* cachedTexture = nullptr;
  unsigned int renderLayer = 0;
  float renderDepth = 0.0f;
  std::vector<SDL_Vertex> localVertices{};
  std::vector<int> indices{};
};

struct YSortTileCmd {
  std::string tilesetAssetId{};
  std::string tilesetRelPath{};
  SDL_Texture* cachedTexture = nullptr;
  SDL_FRect srcRect{};
  float localX = 0.0f;
  float localY = 0.0f;
  float width = 16.0f;
  float height = 16.0f;
  // Shared bottom-most Y offset in chunk-local pixels (supports multi-tile vertical props).
  float sortLocalY = 16.0f;
  SDL_FlipMode flip = SDL_FLIP_NONE;
};

using PropFieldVal = std::variant<int64_t, double, bool, std::string>;

struct PropSpawnDesc {
  std::string identifier{};
  float localX = 0.0f;
  float localY = 0.0f;
  float width = 16.0f;
  float height = 16.0f;
  float pivotX = 0.0f;
  float pivotY = 0.0f;
  uint32_t propIndex = 0;
  std::unordered_map<std::string, PropFieldVal> fields{};
};

struct ChunkTemplate {
  std::string identifier{};
  int32_t gridX = 0;
  int32_t gridY = 0;
  int32_t tileSize = kDefaultTileSize;
  int32_t gridWidth = kDefaultChunkGridSize;
  int32_t gridHeight = kDefaultChunkGridSize;
  float weight = 1.0f;
  bool isBorder = false;
  std::string biome{};
  std::string edgeNorth{};
  std::string edgeEast{};
  std::string edgeSouth{};
  std::string edgeWest{};
  std::unordered_map<std::string, PropFieldVal> fields{};

  std::vector<TileLayerMesh> staticLayers{};
  std::vector<YSortTileCmd> ysortTiles{};
  std::array<uint8_t, kMaxChunkArea> collisions{};
  std::vector<PropSpawnDesc> propSpawns{};

  [[nodiscard]] bool HasEdgeSockets() const noexcept {
    return !edgeNorth.empty() || !edgeEast.empty() || !edgeSouth.empty() || !edgeWest.empty();
  }

  [[nodiscard]] static bool IsOpenSocketTag(const std::string& s) noexcept {
    if (s.empty() || s == "*") {
      return true;
    }
    auto startsWithCaseInsensitive = [](const std::string& str, const char* prefix, const size_t len) noexcept {
      if (str.size() < len) return false;
      for (size_t i = 0; i < len; ++i) {
        const char c = (str[i] >= 'A' && str[i] <= 'Z') ? static_cast<char>(str[i] + ('a' - 'A')) : str[i];
        if (c != prefix[i]) return false;
      }
      return true;
    };
    return startsWithCaseInsensitive(s, "open", 4) || startsWithCaseInsensitive(s, "none", 4) ||
           startsWithCaseInsensitive(s, "any", 3);
  }

  // Returns true when all four edge sockets are unset or tagged with an unconstrained prefix ("open*", "none*",
  // "any*", "*"), making the template safe for neighbor-independent procedural hash placement.
  [[nodiscard]] bool IsUnconstrainedSocketTemplate() const noexcept {
    return IsOpenSocketTag(edgeNorth) && IsOpenSocketTag(edgeEast) && IsOpenSocketTag(edgeSouth) &&
           IsOpenSocketTag(edgeWest);
  }

  [[nodiscard]] constexpr uint8_t GetCollisionLocal(const int32_t localTx, const int32_t localTy) const noexcept {
    if (localTx < 0 || localTx >= gridWidth || localTy < 0 || localTy >= gridHeight) {
      return kTileEmpty;
    }
    const auto idx = static_cast<size_t>(localTy) * static_cast<size_t>(gridWidth) + static_cast<size_t>(localTx);
    return idx < kMaxChunkArea ? collisions[idx] : kTileEmpty;
  }

  static ChunkTemplate CreateSolidBorder(const int32_t tileSize = kDefaultTileSize,
                                         const int32_t gridW = kDefaultChunkGridSize,
                                         const int32_t gridH = kDefaultChunkGridSize) {
    ChunkTemplate border;
    border.identifier = "__SynthesizedSolidBorder";
    border.tileSize = tileSize;
    border.gridWidth = gridW;
    border.gridHeight = gridH;
    border.weight = 0.0f;
    border.isBorder = true;
    border.collisions.fill(kTileSolid);
    return border;
  }
};

struct TilemapData {
  std::string sourceFormat{"custom"};
  int32_t tileSize = kDefaultTileSize;
  int32_t chunkGridWidth = kDefaultChunkGridSize;
  int32_t chunkGridHeight = kDefaultChunkGridSize;
  int32_t gridCols = 1;
  int32_t gridRows = 1;
  std::vector<ChunkTemplate> templates{};
  std::vector<ChunkTemplate> paletteTemplates{};
  std::unordered_map<std::string, std::vector<ChunkTemplate>> templateVariants{};
  ChunkTemplate borderTemplate = ChunkTemplate::CreateSolidBorder();
  // Pairs of (tilesetAssetId, resolvedRelativePath) discovered in the tilemap source.
  std::vector<std::pair<std::string, std::string>> tilesetAssets{};
};

using LdtkProjectData = TilemapData;

}  // namespace octarine::tilemap
