#include "AssetManager/FontStore.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>

#include "AssetManager/GlyphAtlas.h"
#include "General/Logger.h"

namespace {
bool OpenGlyphAtlasPair(const std::string& relPng, const std::string& relLua, const std::string& basePath,
                        const FontStore::IoOpener& openIO, SDL_IOStream*& pngStream, SDL_IOStream*& luaStream) {
  if (openIO) {
    pngStream = openIO(relPng);
    if (pngStream == nullptr) return false;
    luaStream = openIO(relLua);
  } else if (!basePath.empty()) {
    const std::filesystem::path atlasPng = std::filesystem::path(basePath) / relPng;
    const std::filesystem::path atlasLua = std::filesystem::path(basePath) / relLua;
    pngStream = SDL_IOFromFile(atlasPng.string().c_str(), "rb");
    if (pngStream == nullptr) return false;
    luaStream = SDL_IOFromFile(atlasLua.string().c_str(), "rb");
  } else {
    return false;
  }

  if (luaStream != nullptr) return true;
  SDL_CloseIO(pngStream);
  pngStream = nullptr;
  return false;
}

std::unique_ptr<GlyphAtlas> TryLoadGlyphAtlas(const std::string& id, const std::string& basePath,
                                              const FontStore::IoOpener& openIO) {
  // Probe for a glyph-atlas sidecar pair. The bake step writes them under
  // assets/atlases/ (or legacy atlases/) <asset_id>.atlas.{png,lua}; presence is the opt-in.
  // When openIO is provided (e.g. via AssetManager), streams resolve through asset_bundle.pak
  // or loose files alike.
  constexpr std::string_view kCandidatePrefixes[] = {"assets/atlases/", "atlases/"};

  for (const std::string_view prefix : kCandidatePrefixes) {
    const std::string relPng = std::string(prefix) + id + ".atlas.png";
    const std::string relLua = std::string(prefix) + id + ".atlas.lua";
    SDL_IOStream* pngStream = nullptr;
    SDL_IOStream* luaStream = nullptr;
    if (OpenGlyphAtlasPair(relPng, relLua, basePath, openIO, pngStream, luaStream)) {
      auto atlas = std::make_unique<GlyphAtlas>();
      if (atlas->Load(pngStream, luaStream, relPng, relLua)) {
        return atlas;
      }
      return nullptr;
    }
  }
  return nullptr;
}
}  // namespace

// Out-of-line so the unique_ptr<GlyphAtlas> members can be destroyed/moved where GlyphAtlas is a
// complete type (the header only forward-declares it).
FontStore::FontStore() = default;
FontStore::FontStore(FontStore&&) noexcept = default;
FontStore& FontStore::operator=(FontStore&&) noexcept = default;
FontStore::~FontStore() { Clear(); }

TTF_Font* FontStore::Add(const std::string& id, SDL_IOStream* io, const float fontSize, const std::string& basePath,
                         const IoOpener& openIO) {
  TTF_Font* font = TTF_OpenFontIO(io, true, fontSize);  // closes the stream
  if (!font) {
    Logger::Error("Failed to load font " + id + ": " + std::string(SDL_GetError()));
    return nullptr;
  }

  if (const auto it = fonts_.find(id); it != fonts_.end()) {
    Logger::Warn("Replacing existing font: " + id);
    TTF_CloseFont(it->second);
    it->second = font;
  } else {
    fonts_.emplace(id, font);
  }
  base_sizes_[id] = fontSize;
  glyph_atlases_.erase(id);

  Logger::Info("Added font: " + id);

  if (auto atlas = TryLoadGlyphAtlas(id, basePath, openIO)) {
    glyph_atlases_[id] = std::move(atlas);
  }

  return font;
}

TTF_Font* FontStore::Get(const std::string& id) const {
  const auto it = fonts_.find(id);
  return it == fonts_.end() ? nullptr : it->second;
}

float FontStore::GetBaseSize(const std::string& id) const {
  const auto it = base_sizes_.find(id);
  return it == base_sizes_.end() ? 0.0F : it->second;
}

const GlyphAtlas* FontStore::GetGlyphAtlas(const std::string& id) const {
  const auto it = glyph_atlases_.find(id);
  return it == glyph_atlases_.end() ? nullptr : it->second.get();
}

bool FontStore::Remove(const std::string& id) {
  const auto it = fonts_.find(id);
  if (it == fonts_.end()) return false;
  TTF_CloseFont(it->second);
  fonts_.erase(it);
  base_sizes_.erase(id);
  glyph_atlases_.erase(id);
  return true;
}

void FontStore::Clear() {
  for (const auto& [id, font] : fonts_) {
    TTF_CloseFont(font);
  }
  fonts_.clear();
  base_sizes_.clear();
}
