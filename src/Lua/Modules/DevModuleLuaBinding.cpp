#include "Lua/Modules/DevModuleLuaBinding.h"

#include <sol/sol.hpp>
#include <string>

#include "Debug/DebugMenuRegistry.h"
#include "ECS/Registry.h"
#include "General/Logger.h"
#include "Lua/LuaBindingContext.h"

namespace {

using octarine::debug::DebugMenuRegistry;
using octarine::debug::DebugPanelDescriptor;

DebugMenuRegistry* GetDebugRegistry(LuaBindingContext& ctx) {
  auto* reg = ctx.GetRegistry();
  if (reg == nullptr) return nullptr;
  if (reg->TryGet<DebugMenuRegistry>() == nullptr) {
    reg->Set<DebugMenuRegistry>(DebugMenuRegistry());
  }
  return reg->TryGet<DebugMenuRegistry>();
}

void BindPanelFunctions(sol::table& dev, LuaBindingContext& ctx) {
  dev.set_function("register_panel", [&ctx](const std::string& id, const sol::object& optsOrFn) {
    auto* debugReg = GetDebugRegistry(ctx);
    if (!debugReg) return;

    DebugPanelDescriptor desc;
    desc.id = id;
    desc.category = "Game";
    desc.title = id;
    desc.isLuaOwned = true;
    desc.autoWindow = true;

    sol::protected_function drawFn;
    if (optsOrFn.is<sol::protected_function>()) {
      drawFn = optsOrFn.as<sol::protected_function>();
    } else if (optsOrFn.is<sol::table>()) {
      sol::table opts = optsOrFn.as<sol::table>();
      desc.title = opts.get_or("title", id);
      desc.category = opts.get_or("category", std::string("Game"));
      desc.shortcut = opts.get_or("shortcut", std::string{});
      desc.autoWindow = opts.get_or("auto_window", true);
      drawFn = opts.get<sol::protected_function>("draw");
    }

    if (drawFn.valid()) {
      desc.drawCallback = [drawFn](Registry&) {
        auto res = drawFn();
        if (!res.valid()) {
          const sol::error err = res;
          Logger::ErrorLua("dev.register_panel error in '" + std::string(err.what()) + "'");
        }
      };
    }

    debugReg->RegisterPanel(std::move(desc));
  });

  dev.set_function("unregister_panel", [&ctx](const std::string& id) -> bool {
    auto* debugReg = GetDebugRegistry(ctx);
    return debugReg != nullptr && debugReg->UnregisterPanel(id);
  });

  dev.set_function("is_panel_open", [&ctx](const std::string& id) -> bool {
    auto* debugReg = GetDebugRegistry(ctx);
    return debugReg != nullptr && debugReg->IsPanelOpen(id);
  });

  dev.set_function("set_panel_open", [&ctx](const std::string& id, bool open) {
    if (auto* debugReg = GetDebugRegistry(ctx)) {
      debugReg->SetPanelOpen(id, open);
    }
  });
}

void BindActionAndToggleFunctions(sol::table& dev, LuaBindingContext& ctx) {
  dev.set_function("register_action", [&ctx](const std::string& menuPath, sol::protected_function actionFn,
                                             sol::optional<std::string> shortcut) {
    auto* debugReg = GetDebugRegistry(ctx);
    if (!debugReg || !actionFn.valid()) return;

    debugReg->RegisterAction(
        menuPath,
        [actionFn](Registry&) {
          auto res = actionFn();
          if (!res.valid()) {
            const sol::error err = res;
            Logger::ErrorLua("dev.register_action error: " + std::string(err.what()));
          }
        },
        shortcut.value_or(""), /*isLuaOwned=*/true);
  });

  dev.set_function("register_toggle",
                   [&ctx](const std::string& menuPath, sol::protected_function getter, sol::protected_function setter) {
                     auto* debugReg = GetDebugRegistry(ctx);
                     if (!debugReg || !getter.valid() || !setter.valid()) return;

                     debugReg->RegisterToggle(
                         menuPath,
                         [getter]() -> bool {
                           auto res = getter();
                           return res.valid() && res.get_type() == sol::type::boolean && res.get<bool>();
                         },
                         [setter](bool enabled) {
                           auto res = setter(enabled);
                           if (!res.valid()) {
                             const sol::error err = res;
                             Logger::ErrorLua("dev.register_toggle error: " + std::string(err.what()));
                           }
                         },
                         /*isLuaOwned=*/true);
                   });
}

void BindWatchAndEntityFunctions(sol::table& dev, LuaBindingContext& ctx) {
  dev.set_function("watch", [&ctx](const std::string& label, sol::protected_function valueFn) {
    auto* debugReg = GetDebugRegistry(ctx);
    if (!debugReg || !valueFn.valid()) return;

    debugReg->RegisterWatch(
        label,
        [valueFn]() -> std::string {
          auto res = valueFn();
          if (!res.valid()) return "<err>";
          sol::object obj = res;
          if (obj.is<std::string>()) return obj.as<std::string>();
          sol::state_view sv(obj.lua_state());
          sol::protected_function_result strRes = sv["tostring"](obj);
          return strRes.valid() && strRes.get_type() == sol::type::string ? strRes.get<std::string>() : std::string{};
        },
        /*isLuaOwned=*/true);
  });

  dev.set_function("unwatch", [&ctx](const std::string& label) -> bool {
    auto* debugReg = GetDebugRegistry(ctx);
    return debugReg != nullptr && debugReg->UnregisterWatch(label);
  });

  dev.set_function("select_entity", [&ctx](sol::optional<Entity> entity) {
    if (auto* debugReg = GetDebugRegistry(ctx)) {
      debugReg->SelectEntity(entity.value_or(Entity{}));
    }
  });

  dev.set_function("get_selected_entity", [&ctx]() -> sol::optional<Entity> {
    auto* debugReg = GetDebugRegistry(ctx);
    if (debugReg == nullptr) return sol::nullopt;
    const Entity ent = debugReg->GetSelectedEntity();
    if (auto* reg = ctx.GetRegistry()) {
      if (reg->IsAlive(ent)) return ent;
    }
    return sol::nullopt;
  });

  dev.set_function("clear", [&ctx]() {
    if (auto* debugReg = GetDebugRegistry(ctx)) {
      debugReg->ClearLuaOwned();
    }
  });
}

}  // namespace

void LuaModuleBinding<DevModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  sol::table dev = lua.create_named_table("dev");
  BindPanelFunctions(dev, ctx);
  BindActionAndToggleFunctions(dev, ctx);
  BindWatchAndEntityFunctions(dev, ctx);
}
