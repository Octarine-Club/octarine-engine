#include "Lua/Modules/AssetsModuleLuaBinding.h"

#include <string>

#include "AssetManager/AssetManager.h"
#include "ECS/Registry.h"
#include "Lua/LuaBindingContext.h"
#include "Lua/LuaSchemaRegistry.h"

void LuaModuleBinding<AssetsModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  using octarine::lua_schema::LuaSchemaRegistry;
  LuaSchemaRegistry::SetFunction(lua, "get_asset_path", [&ctx](const std::string& relativePath) -> std::string {
    return ctx.GetRegistry()->Get<AssetManager>().GetFullPath(relativePath);
  });
  LuaSchemaRegistry::SetFunction(lua, "has_catalog_asset", [&ctx](const std::string& assetId) -> bool {
    if (assetId.empty()) return false;
    auto* registry = ctx.GetRegistry();
    if (registry == nullptr) return false;
    const auto* assetManager = registry->TryGet<AssetManager>();
    if (assetManager == nullptr) return false;
    return assetManager->GetCatalog().Find(assetId) != nullptr;
  });
}
