#include "AssetManager/TextureAtlasBaker.h"

// STB_IMAGE_IMPLEMENTATION lives in src/AssetManager/GlyphAtlas.cpp; STB_IMAGE_WRITE_IMPLEMENTATION
// lives in src/AssetManager/AtlasBaker.cpp (the glyph-atlas baker). Each implementation symbol set
// is defined in exactly one TU repo-wide to keep the linker happy. We only own
// STB_RECT_PACK_IMPLEMENTATION here.
#define STB_RECT_PACK_IMPLEMENTATION
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

#include "AssetManager/AssetCatalog.h"
#include "General/Logger.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_rect_pack.h"

namespace {
namespace fs = std::filesystem;

struct LoadedSource {
  std::string memberId;
  std::vector<unsigned char> rgba;
  int w{0};
  int h{0};
};

std::string AtlasCatalogIdFor(const std::string& groupName) { return "__atlas_" + groupName; }

// Pick the smallest square canvas, rounded up to a power of two and capped at the engine's
// SDL_GPU-safe ceiling, into which every member of the group could in principle fit. This
// is a starting point for stb_rect_pack — if packing fails at this size we keep doubling.
int InitialCanvasDim(const std::vector<LoadedSource>& sources, const int paddingPx, const int maxDim) {
  long long totalArea = 0;
  int widest = 0;
  int tallest = 0;
  for (const auto& s : sources) {
    const int padW = s.w + paddingPx;
    const int padH = s.h + paddingPx;
    totalArea += static_cast<long long>(padW) * padH;
    widest = std::max(widest, s.w);
    tallest = std::max(tallest, s.h);
  }
  int dim = 64;
  while (static_cast<long long>(dim) * dim < totalArea || dim < widest || dim < tallest) {
    dim *= 2;
    if (dim > maxDim) return dim;  // surface the failure to the caller
  }
  return dim;
}

bool LoadGroupSources(const AssetCatalog& catalog, const std::vector<std::string>& memberIds,
                      std::vector<LoadedSource>& outSources) {
  outSources.clear();
  outSources.reserve(memberIds.size());
  for (const auto& mid : memberIds) {
    const CatalogEntry* member = catalog.Find(mid);
    if (member == nullptr) continue;

    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char* data = stbi_load(member->fullPath.c_str(), &w, &h, &comp, 4);
    if (data == nullptr) {
      Logger::Error("TextureAtlasBaker: failed to load source for atlas member '" + mid + "' (" + member->fullPath +
                    "): " + std::string(stbi_failure_reason() != nullptr ? stbi_failure_reason() : ""));
      return false;
    }
    LoadedSource src;
    src.memberId = mid;
    src.w = w;
    src.h = h;
    src.rgba.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    outSources.push_back(std::move(src));
  }
  return true;
}

bool PackGroupRects(const std::string& groupName, const std::vector<LoadedSource>& sources, const int padding,
                    const int maxDim, std::vector<stbrp_rect>& outRects, int& outDim) {
  outDim = InitialCanvasDim(sources, padding, maxDim);
  outRects.resize(sources.size());
  for (size_t i = 0; i < sources.size(); ++i) {
    outRects[i].id = static_cast<int>(i);
    outRects[i].w = sources[i].w + padding;
    outRects[i].h = sources[i].h + padding;
  }

  while (outDim <= maxDim) {
    std::vector<stbrp_node> nodes(static_cast<size_t>(outDim));
    stbrp_context ctx{};
    stbrp_init_target(&ctx, outDim, outDim, nodes.data(), static_cast<int>(nodes.size()));
    if (stbrp_pack_rects(&ctx, outRects.data(), static_cast<int>(outRects.size())) != 0) {
      return true;
    }
    outDim *= 2;
  }

  Logger::Error("TextureAtlasBaker: atlas '" + groupName + "' (" + std::to_string(sources.size()) +
                " members) cannot fit inside " + std::to_string(maxDim) +
                "px square. Split the group or shrink the source art.");
  return false;
}

bool WriteAtlasPng(const fs::path& atlasOut, const int dim, const std::vector<LoadedSource>& sources,
                   const std::vector<stbrp_rect>& rects) {
  // Allocate the canvas as transparent RGBA. Padding stays transparent, which gives
  // bilinear sampling safe borders when linear filtering is active.
  std::vector<unsigned char> canvas(static_cast<size_t>(dim) * dim * 4, 0);
  for (const auto& r : rects) {
    const LoadedSource& src = sources[r.id];
    for (int row = 0; row < src.h; ++row) {
      const size_t srcOffset = static_cast<size_t>(row) * src.w * 4;
      const size_t dstOffset = (static_cast<size_t>(r.y + row) * dim + static_cast<size_t>(r.x)) * 4;
      std::memcpy(canvas.data() + dstOffset, src.rgba.data() + srcOffset, static_cast<size_t>(src.w) * 4);
    }
  }
  if (stbi_write_png(atlasOut.string().c_str(), dim, dim, 4, canvas.data(), dim * 4) == 0) {
    Logger::Error("TextureAtlasBaker: failed to write atlas PNG " + atlasOut.string());
    return false;
  }
  return true;
}

std::optional<ScaleMode> ResolveCommonExplicitScaleMode(const AssetCatalog& catalog,
                                                        const std::vector<LoadedSource>& sources) {
  if (sources.empty()) return std::nullopt;
  std::optional<ScaleMode> commonMode;
  for (const auto& src : sources) {
    const CatalogEntry* member = catalog.Find(src.memberId);
    if (member == nullptr || !member->hasExplicitScaleMode || !member->scaleMode.has_value()) {
      return std::nullopt;
    }
    if (!commonMode.has_value()) {
      commonMode = member->scaleMode;
    } else if (*commonMode != *member->scaleMode) {
      return std::nullopt;
    }
  }
  return commonMode;
}

void RecordAtlasCatalogEntries(AssetCatalog& catalog, const std::string& groupName, const fs::path& atlasOut,
                               const std::vector<LoadedSource>& sources, const std::vector<stbrp_rect>& rects) {
  std::error_code ec;
  const std::string atlasId = AtlasCatalogIdFor(groupName);
  const std::string atlasFullPath = fs::absolute(atlasOut, ec).lexically_normal().string();
  const auto commonScale = ResolveCommonExplicitScaleMode(catalog, sources);

  for (const auto& r : rects) {
    const std::string& mid = sources[r.id].memberId;
    const SDL_FRect slice{static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(sources[r.id].w),
                          static_cast<float>(sources[r.id].h)};
    catalog.SetAtlasMembership(mid, atlasId, slice);
  }

  CatalogEntry atlasEntry;
  atlasEntry.type = AssetType::Texture;
  atlasEntry.id = atlasId;
  atlasEntry.fullPath = atlasFullPath;
  if (commonScale.has_value()) {
    atlasEntry.scaleMode = *commonScale;
    atlasEntry.hasExplicitScaleMode = true;
  }
  catalog.InsertOrReplace(std::move(atlasEntry));
}

std::map<std::string, std::vector<std::string>> CollectAtlasGroups(const AssetCatalog& catalog) {
  std::map<std::string, std::vector<std::string>> groups;
  for (const auto& [id, entry] : catalog.Entries()) {
    if (entry.type != AssetType::Texture || entry.noAtlas) continue;
    if (!entry.atlas.has_value() || entry.atlas->empty()) continue;
    groups[*entry.atlas].push_back(id);
  }
  return groups;
}

bool BakeAtlasGroup(AssetCatalog& catalog, const fs::path& atlasOutDir, const std::string& groupName,
                    const std::vector<std::string>& memberIds, const int padding, const int maxDim) {
  std::vector<LoadedSource> sources;
  if (!LoadGroupSources(catalog, memberIds, sources)) return false;
  if (sources.empty()) return true;

  std::vector<stbrp_rect> rects;
  int dim = 0;
  if (!PackGroupRects(groupName, sources, padding, maxDim, rects, dim)) {
    return false;
  }

  const fs::path atlasOut = atlasOutDir / (groupName + ".png");
  if (!WriteAtlasPng(atlasOut, dim, sources, rects)) return false;

  RecordAtlasCatalogEntries(catalog, groupName, atlasOut, sources, rects);
  Logger::Info("TextureAtlasBaker: packed atlas '" + groupName + "' (" + std::to_string(sources.size()) + " members, " +
               std::to_string(dim) + "x" + std::to_string(dim) + ") -> " + atlasOut.string());
  return true;
}
}  // namespace

bool TextureAtlasBaker::Run(AssetCatalog& catalog, const std::string& basePath) const {
  const auto groups = CollectAtlasGroups(catalog);
  if (groups.empty()) return true;

  const fs::path atlasOutDir = fs::path(basePath) / "_atlases";
  std::error_code ec;
  fs::create_directories(atlasOutDir, ec);
  if (ec) {
    Logger::Error("TextureAtlasBaker: failed to create " + atlasOutDir.string() + ": " + ec.message());
    return false;
  }

  for (const auto& [groupName, memberIds] : groups) {
    if (!BakeAtlasGroup(catalog, atlasOutDir, groupName, memberIds, kDefaultPaddingPx, kDefaultMaxAtlasDim)) {
      return false;
    }
  }

  return true;
}
