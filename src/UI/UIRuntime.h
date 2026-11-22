#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ECS/Entity.h"
#include "UI/OssParser.h"
#include "UI/OxmlParser.h"

class AssetManager;
class Registry;

struct UIDiagnostics {
  bool lastParseOk = true;
  std::string lastError{};
  std::string lastErrorFile{};
  int lastErrorLine = 0;
  std::uint64_t reloadCount = 0;
  std::vector<std::string> watchedFiles{};
};

#ifndef OCTARINE_SHIPPED
struct OxmlContract;
#endif

class UIRuntime {
 public:
  UIRuntime(sol::state* lua, Registry* registry, AssetManager* assetManager);

  void InstallLuaFunctions(sol::table& uiTable);

  [[nodiscard]] sol::object Mount(const sol::object& sourceObj, const sol::optional<sol::table>& opts);
  [[nodiscard]] sol::table CreateObservable(const sol::table& rawTable);
  void RegisterWidget(const std::string& name, const sol::table& descriptor);

  void PollHotReload(float deltaTime = 0.0f, float pollIntervalSeconds = 0.0f);

  [[nodiscard]] static int ValidateCatalogUI(const AssetManager& assetManager);

  [[nodiscard]] sol::state* GetLua() const { return lua_; }
  [[nodiscard]] Registry* GetRegistry() const { return registry_; }
  [[nodiscard]] AssetManager* GetAssetManager() const;

  [[nodiscard]] std::optional<std::string> ReadTextAsset(const std::string& pathOrId,
                                                         std::string* outResolvedPath = nullptr) const;
  [[nodiscard]] const sol::table* FindCustomWidget(const std::string& name) const;

  struct MountedViewDebugInfo {
    uint64_t id = 0;
    std::string sourcePath{};
    std::vector<std::string> watchedFiles{};
    bool isMounted = false;
  };
  [[nodiscard]] std::vector<MountedViewDebugInfo> GetMountedViewsDebugInfo() const;
  [[nodiscard]] std::optional<uint64_t> FindViewIdByRoot(Entity rootEntity) const;
  [[nodiscard]] bool IsViewMounted(uint64_t id) const;
  [[nodiscard]] Entity GetViewRoot(uint64_t id) const;
  void UnmountView(uint64_t id);
  [[nodiscard]] std::vector<std::pair<uint64_t, bool>> SetNonPreviewViewsInert(uint64_t previewViewId, bool inert);
  void RestoreViewsInert(const std::vector<std::pair<uint64_t, bool>>& prevStates);

  [[nodiscard]] const UIDiagnostics& GetDiagnostics() const noexcept { return diagnostics_; }
  void RecordDiagnosticsError(std::string file, int line, std::string error);
  void RecordDiagnosticsSuccess(std::vector<std::string> watchedFiles, bool isReload);

  bool ReloadMountedView(uint64_t id);
#ifndef OCTARINE_SHIPPED
  [[nodiscard]] bool InspectMountedViewContract(uint64_t id, OxmlContract& outContract) const;
  bool SetMountedViewParam(uint64_t id, const std::string& name, const std::string& value);
  [[nodiscard]] sol::object GetMountedViewParams(uint64_t id) const;
  bool InvokeMountedViewController(uint64_t id, const std::string& methodName);
#endif
  void ReloadAllMountedViews();

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
    std::function<bool()> isMountedFn{};
    std::function<Entity()> rootEntityFn{};
    std::function<bool()> isInertFn{};
    std::function<void(bool)> setInertFn{};
    std::function<void()> unmountFn{};
    std::function<bool(const std::shared_ptr<MountedViewRecord>&)> reloadFn{};
#ifndef OCTARINE_SHIPPED
    std::function<bool(OxmlContract&)> inspectContractFn{};
    std::function<bool(const std::string&, const std::string&)> setParamFn{};
    std::function<sol::object()> getParamsFn{};
    std::function<bool(const std::string&)> invokeControllerFn{};
#endif
  };
  uint64_t RegisterMountedViewWatch(const std::string& sourceStr, std::vector<std::string> watchedPaths,
                                    const std::shared_ptr<void>& viewErased, const std::optional<Entity>& parentEntity);
  void WireMountedViewLifecycleCallbacks(MountedViewRecord& rec, const std::shared_ptr<void>& viewErased,
                                         const std::string& sourceStr, const std::optional<Entity>& parentEntity);
#ifndef OCTARINE_SHIPPED
  void WireMountedViewPreviewCallbacks(MountedViewRecord& rec, const std::shared_ptr<void>& viewErased);
#endif
  uint64_t next_view_id_{1};
  float hot_reload_accum_ = 0.0f;
  std::vector<std::shared_ptr<MountedViewRecord>> mounted_views_{};
  UIDiagnostics diagnostics_{};
};
