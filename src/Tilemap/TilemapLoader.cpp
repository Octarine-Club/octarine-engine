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
  TilemapParseOptions defaultParseOptions{};

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
    ldtkLoader.loadFromFileWithOptions = [](const std::string& path, const AssetManager* assetManager,
                                            const TilemapParseOptions& options) {
      return LdtkParser::ParseFromFile(path, assetManager, options);
    };
    ldtkLoader.loadFromMemoryWithOptions = [](const std::string& content, const std::string& baseDirRelPath,
                                              const TilemapParseOptions& options) {
      return LdtkParser::ParseFromJsonString(content, baseDirRelPath, options);
    };
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

const CatalogEntry* FindCatalogEntry(const AssetManager* assetManager, const std::string& path) {
  if (assetManager == nullptr) {
    return nullptr;
  }
  const auto& catalog = assetManager->GetCatalog();
  if (const CatalogEntry* entry = catalog.Find(path)) {
    return entry;
  }
  const std::filesystem::path fsPath(path);
  if (const CatalogEntry* entry = catalog.Find(fsPath.filename().string())) {
    return entry;
  }
  return catalog.Find(fsPath.stem().string());
}

const TilemapFormatLoader* ResolveLoaderForFile(const LoaderRegistryState& state, const std::string& path,
                                                const AssetManager* assetManager, const std::string& formatHint) {
  if (!formatHint.empty()) {
    if (const auto* loader = state.Resolve(formatHint)) {
      return loader;
    }
  }
  const std::string ext = std::filesystem::path(path).extension().string();
  if (!ext.empty()) {
    if (const auto* loader = state.Resolve(ext)) {
      return loader;
    }
  }
  if (const auto* entry = FindCatalogEntry(assetManager, path)) {
    const std::string entryExt = std::filesystem::path(entry->fullPath).extension().string();
    if (!entryExt.empty()) {
      if (const auto* loader = state.Resolve(entryExt)) {
        return loader;
      }
    }
  }
  return state.Resolve("ldtk");
}

}  // namespace

void TilemapLoaderRegistry::RegisterFormat(TilemapFormatLoader loader) {
  GetRegistryState().Register(std::move(loader));
}

bool TilemapLoaderRegistry::HasFormat(const std::string& formatOrExt) {
  return GetRegistryState().Resolve(formatOrExt) != nullptr;
}

void TilemapLoaderRegistry::SetDefaultParseOptions(TilemapParseOptions options) {
  GetRegistryState().defaultParseOptions = std::move(options);
}

const TilemapParseOptions& TilemapLoaderRegistry::GetDefaultParseOptions() {
  return GetRegistryState().defaultParseOptions;
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
                                                               const std::string& formatHint,
                                                               const TilemapParseOptions& parseOptions) {
  const auto& state = GetRegistryState();
  const TilemapFormatLoader* loader = ResolveLoaderForFile(state, path, assetManager, formatHint);
  if (loader == nullptr || (!loader->loadFromFile && !loader->loadFromFileWithOptions)) {
    Logger::Error("TilemapLoaderRegistry::LoadFromFile: no loader registered for '" + path + "'");
    return std::nullopt;
  }

  auto dataOpt = loader->loadFromFileWithOptions ? loader->loadFromFileWithOptions(path, assetManager, parseOptions)
                                                 : loader->loadFromFile(path, assetManager);
  if (dataOpt.has_value() && (dataOpt->sourceFormat.empty() || dataOpt->sourceFormat == "custom")) {
    dataOpt->sourceFormat = loader->formatId;
  }
  return dataOpt;
}

std::optional<TilemapData> TilemapLoaderRegistry::LoadFromMemory(const std::string& content,
                                                                 const std::string& baseDirRelPath,
                                                                 const std::string& formatHint,
                                                                 const TilemapParseOptions& parseOptions) {
  const auto& state = GetRegistryState();
  const std::string effectiveFormat = formatHint.empty() ? "ldtk" : formatHint;
  const TilemapFormatLoader* loader = state.Resolve(effectiveFormat);
  if (loader == nullptr || (!loader->loadFromMemory && !loader->loadFromMemoryWithOptions)) {
    Logger::Error("TilemapLoaderRegistry::LoadFromMemory: no memory loader registered for format '" + effectiveFormat +
                  "'");
    return std::nullopt;
  }

  auto dataOpt = loader->loadFromMemoryWithOptions
                     ? loader->loadFromMemoryWithOptions(content, baseDirRelPath, parseOptions)
                     : loader->loadFromMemory(content, baseDirRelPath);
  if (dataOpt.has_value() && (dataOpt->sourceFormat.empty() || dataOpt->sourceFormat == "custom")) {
    dataOpt->sourceFormat = loader->formatId;
  }
  return dataOpt;
}

}  // namespace octarine::tilemap
