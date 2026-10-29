#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "ECS/Entity.h"
#include "UI/OssParser.h"
#include "UI/OxmlParser.h"

class AssetManager;
class Registry;

class UIRuntime {
 public:
  UIRuntime(sol::state* lua, Registry* registry, AssetManager* assetManager);

  void InstallLuaFunctions(sol::table& uiTable);

  [[nodiscard]] sol::object Mount(const sol::object& sourceObj, const sol::optional<sol::table>& opts);
  [[nodiscard]] sol::table CreateObservable(const sol::table& rawTable);
  void RegisterWidget(const std::string& name, const sol::table& descriptor);

  void PollHotReload();

  [[nodiscard]] static int ValidateCatalogUI(const AssetManager& assetManager);

  [[nodiscard]] sol::state* GetLua() const { return lua_; }
  [[nodiscard]] Registry* GetRegistry() const { return registry_; }
  [[nodiscard]] AssetManager* GetAssetManager() const;

  [[nodiscard]] std::optional<std::string> ReadTextAsset(const std::string& pathOrId,
                                                         std::string* outResolvedPath = nullptr) const;
  [[nodiscard]] const sol::table* FindCustomWidget(const std::string& name) const;

 private:
  sol::state* lua_{nullptr};
  Registry* registry_{nullptr};
  AssetManager* asset_manager_{nullptr};

  std::unordered_map<std::string, sol::table> custom_widgets_{};

  struct MountedViewRecord {
    uint64_t id = 0;
    std::string sourcePath{};
    std::vector<std::string> watchedFiles{};
    std::unordered_map<std::string, std::filesystem::file_time_type> lastWriteTimes{};
    sol::table viewTable{};
    std::function<void()> reloadFn{};
  };
  uint64_t next_view_id_{1};
  std::vector<std::shared_ptr<MountedViewRecord>> mounted_views_{};
};
