#pragma once

#include <optional>
#include <string>

#include "Tilemap/ChunkTemplate.h"

class AssetManager;

namespace octarine::tilemap {

class LdtkParser {
 public:
  // Parses an LDtk JSON string directly into TilemapData.
  // `ldtkDirRelPath` is the project-relative directory containing the .ldtk file (used to resolve tileset relPaths).
  [[nodiscard]] static std::optional<TilemapData> ParseFromJsonString(const std::string& jsonText,
                                                                      const std::string& ldtkDirRelPath = "",
                                                                      const TilemapParseOptions& options = {});

  // Reads an .ldtk file via SDL/AssetManager (supporting both loose files and asset_bundle.pak) and parses it.
  [[nodiscard]] static std::optional<TilemapData> ParseFromFile(const std::string& ldtkPath,
                                                                const AssetManager* assetManager = nullptr,
                                                                const TilemapParseOptions& options = {});
};

}  // namespace octarine::tilemap
