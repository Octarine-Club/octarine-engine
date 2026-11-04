#include "Tilemap/TilemapLoader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>

#include "AssetManager/AssetManager.h"
#include "General/Logger.h"
#include "Tilemap/LdtkParser.h"

namespace octarine::tilemap {

namespace {

std::string NormalizeKey(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string NormalizeExt(std::string ext) {
  if (!ext.empty() && ext.front() != '.') {
    ext.insert(ext.begin(), '.');
  }
  return NormalizeKey(std::move(ext));
}

struct LoaderRegistryState {
  std::unordered_map<std::string, TilemapFormatLoader> byFormatId{};
  std::unordered_map<std::string, std::string> extToFormatId{};
  VariantGeneratorFn defaultVariantGenerator{};

  void Register(TilemapFormatLoader loader) {
    const std::string id = NormalizeKey(loader.formatId);
    if (id.empty()) {
      return;
    }
    loader.formatId = id;
    for (const auto& rawExt : loader.extensions) {
      if (!rawExt.empty()) {
        extToFormatId[NormalizeExt(rawExt)] = id;
      }
    }
    byFormatId[id] = std::move(loader);
  }

  [[nodiscard]] const TilemapFormatLoader* Resolve(const std::string& formatOrExt) const {
    const std::string key = NormalizeKey(formatOrExt);
    if (key.empty()) {
      return nullptr;
    }
    if (const auto it = byFormatId.find(key); it != byFormatId.end()) {
      return &it->second;
    }
    const std::string extKey = NormalizeExt(key);
    if (const auto extIt = extToFormatId.find(extKey); extIt != extToFormatId.end()) {
      if (const auto idIt = byFormatId.find(extIt->second); idIt != byFormatId.end()) {
        return &idIt->second;
      }
    }
    return nullptr;
  }
};

LoaderRegistryState& GetRegistryState() {
  static LoaderRegistryState state = []() {
    LoaderRegistryState init;
    TilemapFormatLoader ldtkLoader;
    ldtkLoader.formatId = "ldtk";
    ldtkLoader.extensions = {".ldtk"};
    ldtkLoader.loadFromFile = [](const std::string& path, const AssetManager* assetManager) {
      return LdtkParser::ParseFromFile(path, assetManager);
    };
    ldtkLoader.loadFromMemory = [](const std::string& content, const std::string& baseDirRelPath) {
      return LdtkParser::ParseFromJsonString(content, baseDirRelPath);
    };
    init.Register(std::move(ldtkLoader));
    return init;
  }();
  return state;
}

}  // namespace

void TilemapLoaderRegistry::RegisterFormat(TilemapFormatLoader loader) {
  GetRegistryState().Register(std::move(loader));
}

bool TilemapLoaderRegistry::HasFormat(const std::string& formatOrExt) {
  return GetRegistryState().Resolve(formatOrExt) != nullptr;
}

void TilemapLoaderRegistry::SetDefaultVariantGenerator(VariantGeneratorFn generator) {
  GetRegistryState().defaultVariantGenerator = std::move(generator);
}

void TilemapLoaderRegistry::ApplyVariantGenerator(TilemapData& data, const int32_t variantCount, const uint32_t seed) {
  const auto& gen = GetRegistryState().defaultVariantGenerator;
  if (gen) {
    gen(data, variantCount, seed);
  }
}

std::optional<TilemapData> TilemapLoaderRegistry::LoadFromFile(const std::string& path,
                                                               const AssetManager* assetManager,
                                                               const std::string& formatHint) {
  const auto& state = GetRegistryState();
  const TilemapFormatLoader* loader = nullptr;

  if (!formatHint.empty()) {
    loader = state.Resolve(formatHint);
  }
  if (loader == nullptr) {
    const std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty()) {
      loader = state.Resolve(ext);
    }
  }
  if (loader == nullptr && assetManager != nullptr) {
    const std::filesystem::path fsPath(path);
    const CatalogEntry* entry = assetManager->GetCatalog().Find(path);
    if (entry == nullptr) {
      entry = assetManager->GetCatalog().Find(fsPath.filename().string());
    }
    if (entry == nullptr) {
      entry = assetManager->GetCatalog().Find(fsPath.stem().string());
    }
    if (entry != nullptr) {
      const std::string ext = std::filesystem::path(entry->fullPath).extension().string();
      if (!ext.empty()) {
        loader = state.Resolve(ext);
      }
    }
  }
  if (loader == nullptr) {
    loader = state.Resolve("ldtk");
  }
  if (loader == nullptr || !loader->loadFromFile) {
    Logger::Error("TilemapLoaderRegistry::LoadFromFile: no loader registered for '" + path + "'");
    return std::nullopt;
  }

  auto dataOpt = loader->loadFromFile(path, assetManager);
  if (dataOpt.has_value() && (dataOpt->sourceFormat.empty() || dataOpt->sourceFormat == "custom")) {
    dataOpt->sourceFormat = loader->formatId;
  }
  return dataOpt;
}

std::optional<TilemapData> TilemapLoaderRegistry::LoadFromMemory(const std::string& content,
                                                                 const std::string& baseDirRelPath,
                                                                 const std::string& formatHint) {
  const auto& state = GetRegistryState();
  const std::string effectiveFormat = formatHint.empty() ? "ldtk" : formatHint;
  const TilemapFormatLoader* loader = state.Resolve(effectiveFormat);
  if (loader == nullptr || !loader->loadFromMemory) {
    Logger::Error("TilemapLoaderRegistry::LoadFromMemory: no memory loader registered for format '" + effectiveFormat +
                  "'");
    return std::nullopt;
  }

  auto dataOpt = loader->loadFromMemory(content, baseDirRelPath);
  if (dataOpt.has_value() && (dataOpt->sourceFormat.empty() || dataOpt->sourceFormat == "custom")) {
    dataOpt->sourceFormat = loader->formatId;
  }
  return dataOpt;
}

}  // namespace octarine::tilemap
