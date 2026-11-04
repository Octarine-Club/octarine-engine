#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Tilemap/ChunkTemplate.h"

class AssetManager;

namespace octarine::tilemap {

using VariantGeneratorFn = std::function<void(TilemapData& data, int32_t variantCount, uint32_t seed)>;

struct TilemapFormatLoader {
  std::string formatId{};
  std::vector<std::string> extensions{};
  std::function<std::optional<TilemapData>(const std::string& path, const AssetManager* assetManager)> loadFromFile{};
  std::function<std::optional<TilemapData>(const std::string& content, const std::string& baseDirRelPath)>
      loadFromMemory{};
};

class TilemapLoaderRegistry {
 public:
  static void RegisterFormat(TilemapFormatLoader loader);
  [[nodiscard]] static bool HasFormat(const std::string& formatOrExt);

  // Registers an optional process-wide default variant generator (e.g. from a native game module).
  static void SetDefaultVariantGenerator(VariantGeneratorFn generator);
  static void ApplyVariantGenerator(TilemapData& data, int32_t variantCount, uint32_t seed);

  // Auto-detects format by file extension (or explicit formatHint override) and parses via SDL/AssetManager.
  [[nodiscard]] static std::optional<TilemapData> LoadFromFile(const std::string& path,
                                                               const AssetManager* assetManager = nullptr,
                                                               const std::string& formatHint = "");

  // Parses in-memory text/bytes using formatHint (defaults to "ldtk" if empty).
  [[nodiscard]] static std::optional<TilemapData> LoadFromMemory(const std::string& content,
                                                                 const std::string& baseDirRelPath = "",
                                                                 const std::string& formatHint = "ldtk");
};

}  // namespace octarine::tilemap
