#include "Lua/Modules/AssetsModuleLuaBinding.h"

#include <string>

#include "AssetManager/AssetManager.h"
#include "ECS/Registry.h"
#include "Lua/LuaBindingContext.h"

void LuaModuleBinding<AssetsModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  lua.set_function("get_asset_path", [&ctx](const std::string& relativePath) {
    return ctx.GetRegistry()->Get<AssetManager>().GetFullPath(relativePath);
  });
  lua.set_function("has_catalog_asset", [&ctx](const std::string& assetId) -> bool {
    if (assetId.empty()) return false;
    auto* registry = ctx.GetRegistry();
    if (registry == nullptr) return false;
    const auto* assetManager = registry->TryGet<AssetManager>();
    if (assetManager == nullptr) return false;
    return assetManager->GetCatalog().Find(assetId) != nullptr;
  });
}
