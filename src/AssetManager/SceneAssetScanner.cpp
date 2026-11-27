#include "AssetManager/SceneAssetScanner.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <set>
#include <stack>
#include <string_view>
#include <utility>

#include "AssetManager/AssetCatalog.h"
#include "AssetManager/AssetManager.h"
#include "AssetManager/SdlFileReader.h"

namespace {

void AddIfString(const sol::table& table, const char* key, const std::string& context,
                 std::vector<AssetReference>& out) {
  const sol::optional<std::string> value = table[key];
  if (value && !value->empty()) out.push_back({*value, context + key});
}

std::string ToLowerAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool HasLdtkExtension(const std::string_view path) {
  const std::string ext = ToLowerAscii(std::filesystem::path(path).extension().string());
  return ext == ".ldtk" || ext == ".ldtkl";
}

bool IsLdtklExtension(const std::string_view path) {
  return ToLowerAscii(std::filesystem::path(path).extension().string()) == ".ldtkl";
}

const AssetManager* LookupAssetManagerFromLua(lua_State* L) {
  if (L == nullptr) return nullptr;
  lua_getfield(L, LUA_REGISTRYINDEX, "octarine_assets");
  const AssetManager* am =
      lua_islightuserdata(L, -1) ? static_cast<const AssetManager*>(lua_touserdata(L, -1)) : nullptr;
  lua_pop(L, 1);
  return am;
}

const CatalogEntry* FindCatalogEntryByPathOrId(const AssetManager* am, const std::string& rawRef,
                                               const bool isTilemapFile) {
  if (am == nullptr || rawRef.empty()) return nullptr;
  const auto& catalog = am->GetCatalog();
  if (const CatalogEntry* direct = catalog.Find(rawRef)) return direct;

  const std::filesystem::path p(rawRef);
  const std::string primaryKey = isTilemapFile ? p.filename().string() : p.stem().string();
  if (!primaryKey.empty()) {
    if (const CatalogEntry* byName = catalog.Find(primaryKey)) return byName;
  }

  const std::string resolvedFull =
      p.is_absolute() ? p.lexically_normal().string() : am->GetFullPath(p.generic_string());
  if (!resolvedFull.empty()) {
    const std::string normTarget = std::filesystem::path(resolvedFull).lexically_normal().generic_string();
    for (const auto& [_, entry] : catalog.Entries()) {
      if (std::filesystem::path(entry.fullPath).lexically_normal().generic_string() == normTarget) {
        return &entry;
      }
    }
  }
  return nullptr;
}

bool ParseJsonStringLiteral(const std::string_view src, size_t& pos, std::string& out) {
  if (pos >= src.size() || src[pos] != '"') return false;
  ++pos;
  out.clear();
  while (pos < src.size()) {
    const char c = src[pos++];
    if (c == '"') return true;
    if (c == '\\' && pos < src.size()) {
      const char esc = src[pos++];
      out.push_back(esc);
    } else {
      out.push_back(c);
    }
  }
  return false;
}

void SkipJsonWhitespace(const std::string_view src, size_t& pos) noexcept {
  while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])) != 0) {
    ++pos;
  }
}

bool IsLdtkAssetPathKey(const std::string_view key) noexcept {
  return key == "relPath" || key == "__tilesetRelPath" || key == "bgRelPath" || key == "externalRelPath";
}

std::vector<std::string> ExtractLdtkDependencyPaths(const std::string_view jsonText) {
  std::vector<std::string> paths;
  size_t pos = 0;
  std::string token;
  std::string val;
  while (pos < jsonText.size()) {
    if (jsonText[pos] != '"') {
      ++pos;
      continue;
    }
    if (!ParseJsonStringLiteral(jsonText, pos, token)) break;
    SkipJsonWhitespace(jsonText, pos);
    if (pos >= jsonText.size() || jsonText[pos] != ':' || !IsLdtkAssetPathKey(token)) {
      continue;
    }
    ++pos;
    SkipJsonWhitespace(jsonText, pos);
    if (pos < jsonText.size() && jsonText[pos] == '"' && ParseJsonStringLiteral(jsonText, pos, val) && !val.empty()) {
      paths.push_back(val);
    }
  }
  return paths;
}

std::string ResolveLdtkReadPath(const AssetManager* am, const CatalogEntry* ldtkEntry, const std::string& rawLdtkRef) {
  if (ldtkEntry != nullptr && !ldtkEntry->fullPath.empty()) {
    return ldtkEntry->fullPath;
  }
  const std::filesystem::path p(rawLdtkRef);
  if (am != nullptr && !p.is_absolute()) {
    return am->GetFullPath(rawLdtkRef);
  }
  return rawLdtkRef;
}

std::string ResolveLdtkParentRelDir(const AssetManager* am, const std::string& rawLdtkRef,
                                    const std::string& readPath) {
  const std::filesystem::path rawParent = std::filesystem::path(rawLdtkRef).parent_path();
  if (!rawParent.empty() && !rawParent.is_absolute()) {
    return rawParent.lexically_normal().generic_string();
  }
  if (am != nullptr && !am->GetBasePath().empty() && !readPath.empty()) {
    const auto normParent = std::filesystem::path(readPath).parent_path().lexically_normal();
    const auto normBase = std::filesystem::path(am->GetBasePath()).lexically_normal();
    const auto rel = normParent.lexically_relative(normBase);
    if (!rel.empty() && rel != "." && *rel.begin() != "..") {
      return rel.generic_string();
    }
  }
  return {};
}

std::string ResolveDependencyId(const AssetManager* am, const std::string& resolvedDepRel, const bool isTilemapDep) {
  if (const CatalogEntry* depEntry = FindCatalogEntryByPathOrId(am, resolvedDepRel, isTilemapDep)) {
    return depEntry->id;
  }
  const std::filesystem::path depPath(resolvedDepRel);
  return isTilemapDep ? depPath.filename().string() : depPath.stem().string();
}

std::string ResolveSingleLdtkDepPath(const AssetManager* am, const std::string& baseRelDir,
                                     const std::filesystem::path& diskParent, const std::string& rawDep) {
  std::string resolvedDep = baseRelDir.empty()
                                ? std::filesystem::path(rawDep).lexically_normal().generic_string()
                                : (std::filesystem::path(baseRelDir) / rawDep).lexically_normal().generic_string();
  if (am == nullptr && !diskParent.empty()) {
    const auto absCandidate = (diskParent / rawDep).lexically_normal();
    std::error_code ec;
    if (std::filesystem::exists(absCandidate, ec)) {
      resolvedDep = absCandidate.generic_string();
    }
  }
  return resolvedDep;
}

void CollectLdtkFileAndDeps(const AssetManager* am, const std::string& rawLdtkRef, const std::string& context,
                            std::vector<AssetReference>& out) {
  if (rawLdtkRef.empty()) return;
  const CatalogEntry* ldtkEntry = FindCatalogEntryByPathOrId(am, rawLdtkRef, /*isTilemapFile=*/true);
  const std::string ldtkId =
      ldtkEntry != nullptr ? ldtkEntry->id : std::filesystem::path(rawLdtkRef).filename().string();
  if (!ldtkId.empty()) {
    out.push_back({ldtkId, context});
  }

  const std::string readPath = ResolveLdtkReadPath(am, ldtkEntry, rawLdtkRef);
  if (readPath.empty()) return;
  const auto bytes = ReadFileViaSDL(readPath, am);
  if (!bytes.has_value()) return;

  const std::string baseRelDir = ResolveLdtkParentRelDir(am, rawLdtkRef, readPath);
  const std::filesystem::path diskParent = std::filesystem::path(readPath).parent_path();
  std::set<std::string> seenDeps;

  for (const std::string& rawDep : ExtractLdtkDependencyPaths(*bytes)) {
    const std::string resolvedDep = ResolveSingleLdtkDepPath(am, baseRelDir, diskParent, rawDep);
    const std::string depId = ResolveDependencyId(am, resolvedDep, IsLdtklExtension(rawDep));
    if (!depId.empty() && seenDeps.insert(depId).second) {
      out.push_back({depId, context + " dependency"});
    }
  }
}

void CollectTilemapLdtkKeyRefs(const sol::table& tilemap, const AssetManager* am, std::vector<AssetReference>& out) {
  for (const char* key : {"ldtk", "file", "path", "map"}) {
    const sol::optional<std::string> val = tilemap[key];
    if (!val || val->empty()) continue;
    const bool explicitLdtkKey = std::string_view(key) == "ldtk";
    const CatalogEntry* catEntry = FindCatalogEntryByPathOrId(am, *val, /*isTilemapFile=*/true);
    const bool isLdtk =
        HasLdtkExtension(*val) || explicitLdtkKey || (catEntry != nullptr && catEntry->type == AssetType::Tilemap);
    if (isLdtk) {
      CollectLdtkFileAndDeps(am, *val, std::string("tilemap.") + key, out);
    }
  }
}

void CollectTilemapTableRefs(const sol::table& tilemap, const AssetManager* am, std::vector<AssetReference>& out) {
  AddIfString(tilemap, "texture_asset_id", "tilemap.", out);
  AddIfString(tilemap, "tilemap_asset_id", "tilemap.", out);
  AddIfString(tilemap, "asset_id", "tilemap.", out);

  if (const sol::optional<sol::table> tsIds = tilemap["tileset_asset_ids"]; tsIds) {
    for (const auto& [_, v] : *tsIds) {
      if (v.is<std::string>()) {
        if (std::string id = v.as<std::string>(); !id.empty()) out.push_back({std::move(id), "tilemap.tilesets"});
      }
    }
  }

  CollectTilemapLdtkKeyRefs(tilemap, am, out);
}

void CollectSceneTilemapField(const sol::table& scene, const AssetManager* am, std::vector<AssetReference>& out) {
  const sol::object tilemapObj = scene["tilemap"];
  if (tilemapObj.is<sol::table>()) {
    CollectTilemapTableRefs(tilemapObj.as<sol::table>(), am, out);
  } else if (tilemapObj.is<std::string>()) {
    const std::string tmStr = tilemapObj.as<std::string>();
    if (HasLdtkExtension(tmStr)) {
      CollectLdtkFileAndDeps(am, tmStr, "tilemap", out);
    }
  }
}

void CollectPreloadList(const sol::table& scene, std::vector<AssetReference>& out) {
  const sol::optional<sol::table> preload = scene["preload"];
  if (!preload) return;
  for (const auto& [_, value] : *preload) {
    if (!value.is<std::string>()) continue;
    if (std::string id = value.as<std::string>(); !id.empty()) {
      out.push_back({std::move(id), "preload list"});
    }
  }
}

void PushTableEntries(const sol::table& list, std::stack<sol::table>& nodes) {
  for (const auto& [_, value] : list) {
    if (value.is<sol::table>()) nodes.push(value.as<sol::table>());
  }
}

}  // namespace

void SceneAssetScanner::CollectFromComponents(const sol::table& components, const std::string& entityContext,
                                              std::vector<AssetReference>& out) {
  if (const sol::optional<sol::table> sprite = components["sprite"]; sprite) {
    AddIfString(*sprite, "texture_asset_id", entityContext + " sprite.", out);
  }
  if (const sol::optional<sol::table> text = components["text_label"]; text) {
    AddIfString(*text, "font_id", entityContext + " text_label.", out);
  }
  if (const sol::optional<sol::table> audio = components["audio_source"]; audio) {
    AddIfString(*audio, "clip_id", entityContext + " audio_source.", out);
  }
}

std::vector<AssetReference> SceneAssetScanner::CollectRefs(const sol::table& scene) {
  std::vector<AssetReference> refs;
  if (!scene.valid()) return refs;

  const AssetManager* am = LookupAssetManagerFromLua(scene.lua_state());
  CollectSceneTilemapField(scene, am, refs);
  CollectPreloadList(scene, refs);

  // Walk the entity forest.
  std::stack<sol::table> nodes;
  if (const sol::optional<sol::table> entities = scene["entities"]; entities) {
    PushTableEntries(*entities, nodes);
  }

  while (!nodes.empty()) {
    const sol::table node = nodes.top();
    nodes.pop();

    std::string entityContext = "entity";
    if (const sol::optional<std::string> name = node["name"]; name && !name->empty()) {
      entityContext = "entity '" + *name + "'";
    }

    if (const sol::optional<sol::table> components = node["components"]; components) {
      CollectFromComponents(*components, entityContext, refs);
    }
    if (const sol::optional<sol::table> children = node["entities"]; children) {
      PushTableEntries(*children, nodes);
    }
  }

  return refs;
}

std::unordered_set<std::string> SceneAssetScanner::Collect(const sol::table& scene) {
  std::unordered_set<std::string> ids;
  for (auto& ref : CollectRefs(scene)) ids.insert(std::move(ref.id));
  return ids;
}
