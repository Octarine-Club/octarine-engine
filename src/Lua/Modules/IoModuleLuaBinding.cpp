#include "Lua/Modules/IoModuleLuaBinding.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "AssetManager/AssetManager.h"
#include "AssetManager/SdlFileReader.h"
#include "Crypto/LuaProtect.h"
#include "ECS/Registry.h"
#include "Game/EngineContext.h"
#include "Game/GameConfig.h"
#include "General/FileSandbox.h"
#include "General/Logger.h"
#include "Lua/LuaBindingContext.h"
#include "Lua/LuaSchemaRegistry.h"

namespace {
using octarine::lua_schema::LuaSchemaRegistry;

constexpr const char* kImportCacheKey = "__octarine_import_cache";
constexpr const char* kImportLoadingKey = "__octarine_import_loading";

const AssetManager* TryGetAssetManager(LuaBindingContext& ctx) {
  if (const Registry* reg = ctx.GetRegistry()) {
    if (const auto* am = reg->TryGet<AssetManager>()) {
      return am;
    }
    if (const auto* ectx = reg->TryGet<EngineContext>()) {
      return ectx->assets;
    }
  }
  return nullptr;
}

std::string ResolveSandboxRoot(LuaBindingContext& ctx, const AssetManager* assets) {
  if (assets != nullptr && !assets->GetBasePath().empty()) {
    return assets->GetBasePath();
  }
  if (const Registry* reg = ctx.GetRegistry()) {
    if (const auto* cfg = reg->TryGet<GameConfig>()) {
      if (cfg->HasLoadedConfig() && !cfg->GetAssetPath().empty()) {
        return cfg->GetAssetPath();
      }
      return {};
    }
    return {};
  }
  return ctx.GetProjectPath();
}

std::string ResolveSandboxedAssetPath(LuaBindingContext& ctx, const AssetManager* assets, const std::string& rawPath,
                                      const char* caller) {
  if (rawPath.empty()) return {};
  const std::string root = ResolveSandboxRoot(ctx, assets);
  const std::filesystem::path inputPath(rawPath);

  if (inputPath.is_absolute()) {
    if (root.empty()) {
      Logger::Error(std::string(caller) + ": absolute path rejected without project root: " + rawPath);
      return {};
    }
    const auto normBase = std::filesystem::path(root).lexically_normal();
    const auto normTarget = inputPath.lexically_normal();
    const auto rel = normTarget.lexically_relative(normBase);
    if (rel.empty() || rel == "." || *rel.begin() == "..") {
      Logger::Error(std::string(caller) + ": path outside project root rejected: " + rawPath);
      return {};
    }
    return normTarget.string();
  }

  if (assets != nullptr && (!assets->GetBasePath().empty() || assets->GetAssetPak() != nullptr)) {
    return assets->GetFullPath(rawPath);
  }
  if (!root.empty()) {
    const std::string resolved = file_sandbox::Resolve(root, rawPath);
    if (resolved.empty()) {
      Logger::Error(std::string(caller) + ": path traversal rejected: " + rawPath);
    }
    return resolved;
  }

  const auto norm = inputPath.lexically_normal();
  if (norm.empty() || norm == "." || *norm.begin() == "..") {
    Logger::Error(std::string(caller) + ": path traversal rejected: " + rawPath);
    return {};
  }
  return norm.string();
}

std::vector<std::string> ReadFileLines(LuaBindingContext& ctx, const std::string& filename) {
  std::vector<std::string> lines;
  const AssetManager* assets = TryGetAssetManager(ctx);
  const std::string resolvedPath = ResolveSandboxedAssetPath(ctx, assets, filename, "read_file_lines");
  if (resolvedPath.empty()) {
    return lines;
  }

  const auto data = ReadFileViaSDL(resolvedPath, assets);
  if (!data.has_value()) {
    return lines;
  }

  std::istringstream stream(*data);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

std::string NormalizeImportPath(const std::string& modulePath) {
  if (modulePath.empty()) return {};
  const std::filesystem::path p(modulePath);
  if (!p.has_extension()) {
    return modulePath + ".lua";
  }
  return modulePath;
}

sol::table EnsureRegistryTable(sol::state_view lua, const char* regKey) {
  sol::object existing = lua.registry().get<sol::object>(regKey);
  if (existing.is<sol::table>()) {
    return existing.as<sol::table>();
  }
  sol::table created = lua.create_table();
  lua.registry()[regKey] = created;
  return created;
}

bool ExecuteImportModule(lua_State* L, const std::string& fullPath, const AssetManager* assets, sol::object& outResult,
                         std::string& outError) {
  auto bytes = ReadFileViaSDL(fullPath, assets);
  if (!bytes.has_value()) {
    outError = "import: cannot open '" + fullPath + "'";
    return false;
  }
  const bool wasScrambledOct = bytes->size() >= 4 && std::memcmp(bytes->data(), "\x1bOCT", 4) == 0;
  const bool requireProtected = (assets != nullptr && assets->GetAssetPak() != nullptr);
  if (!DecryptLuaBytes(*bytes, requireProtected)) {
    outError = "import: rejected unprotected or corrupted script '" + fullPath + "'";
    return false;
  }

  const std::string chunkName = "@" + fullPath;
  const char* loadMode = wasScrambledOct ? "b" : "t";
  const int loadRc = luaL_loadbufferx(L, bytes->data(), bytes->size(), chunkName.c_str(), loadMode);
  std::fill(bytes->begin(), bytes->end(), '\0');
  bytes->clear();
  if (loadRc != LUA_OK) {
    const std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "syntax error";
    lua_pop(L, 1);
    outError = "import load '" + fullPath + "': " + err;
    return false;
  }

  if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
    const std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "runtime error";
    lua_pop(L, 1);
    outError = "import run '" + fullPath + "': " + err;
    return false;
  }

  sol::object ret = sol::stack::pop<sol::object>(L);
  outResult = (!ret.valid() || ret == sol::lua_nil) ? sol::make_object(L, true) : ret;
  return true;
}

sol::object ImportModule(LuaBindingContext& ctx, const std::string& relPath, const sol::optional<bool>& forceReload,
                         sol::this_state s) {
  lua_State* L = s;
  sol::state_view lua(s);
  const AssetManager* assets = TryGetAssetManager(ctx);
  const std::string normalizedReq = NormalizeImportPath(relPath);
  const std::string fullPath = ResolveSandboxedAssetPath(ctx, assets, normalizedReq, "import");
  if (fullPath.empty()) {
    luaL_error(L, "import: rejected path '%s'", relPath.c_str());
    return sol::lua_nil;
  }

  sol::table cache = EnsureRegistryTable(lua, kImportCacheKey);
  sol::table loading = EnsureRegistryTable(lua, kImportLoadingKey);

  if (!forceReload.value_or(false)) {
    sol::object cached = cache.raw_get<sol::object>(fullPath);
    if (cached.valid() && cached != sol::lua_nil) {
      return cached;
    }
  }

  if (loading.raw_get_or(fullPath, false)) {
    luaL_error(L, "import: circular dependency detected while loading '%s'", relPath.c_str());
    return sol::lua_nil;
  }

  loading.raw_set(fullPath, true);
  struct LoadingGuard {
    sol::table tbl{};
    std::string key{};
    LoadingGuard(sol::table t, std::string k) : tbl(std::move(t)), key(std::move(k)) {}
    LoadingGuard(const LoadingGuard&) = delete;
    LoadingGuard& operator=(const LoadingGuard&) = delete;
    LoadingGuard(LoadingGuard&&) = delete;
    LoadingGuard& operator=(LoadingGuard&&) = delete;
    void Reset() {
      if (tbl.valid()) tbl.raw_set(key, sol::lua_nil);
      tbl = sol::lua_nil;
    }
    ~LoadingGuard() { Reset(); }
  } guard{loading, fullPath};

  sol::object result = sol::lua_nil;
  std::string errMsg;
  const bool ok = ExecuteImportModule(L, fullPath, assets, result, errMsg);
  // Explicitly clear before luaL_error because C Lua longjmp unwinds past C++ stack destructors.
  guard.Reset();
  if (!ok) {
    luaL_error(L, "%s", errMsg.c_str());
    return sol::lua_nil;
  }
  cache.raw_set(fullPath, result);
  return result;
}

void ClearImportCache(LuaBindingContext& ctx, const sol::optional<std::string>& relPath, sol::this_state s) {
  sol::state_view lua(s);
  sol::object existing = lua.registry().get<sol::object>(kImportCacheKey);
  if (!existing.is<sol::table>()) return;
  sol::table cache = existing.as<sol::table>();
  if (!relPath.has_value() || relPath->empty()) {
    lua.registry()[kImportCacheKey] = lua.create_table();
    return;
  }
  const AssetManager* assets = TryGetAssetManager(ctx);
  const std::string fullPath = ResolveSandboxedAssetPath(ctx, assets, NormalizeImportPath(*relPath), "import");
  if (!fullPath.empty()) {
    cache.raw_set(fullPath, sol::lua_nil);
  }
}
}  // namespace

void LuaModuleBinding<IoModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  LuaSchemaRegistry::SetFunction(lua, "read_file_lines",
                                 [&ctx](const std::string& filename) { return ReadFileLines(ctx, filename); });
  LuaSchemaRegistry::SetFunction(
      lua, "import", [&ctx](const std::string& relPath, const sol::optional<bool> forceReload, sol::this_state s) {
        return ImportModule(ctx, relPath, forceReload, s);
      });
  LuaSchemaRegistry::SetFunction(
      lua, "clear_import_cache",
      [&ctx](const sol::optional<std::string>& relPath, sol::this_state s) { ClearImportCache(ctx, relPath, s); });
}
