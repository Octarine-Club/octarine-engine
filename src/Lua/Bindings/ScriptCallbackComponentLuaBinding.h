#pragma once

#include <sol/sol.hpp>

#include "Components/ScriptCallbackComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<ScriptCallbackComponent> {
  static constexpr const char* kLuaKey = "script_callbacks";
  static constexpr const char* kUsertypeName = "script_callback_component";

  static ScriptCallbackComponent fromLua(const sol::object& data) {
    const auto t = data.as<sol::table>();
    ScriptCallbackComponent comp;
    comp.script_table = t.get_or<sol::object>("script_table", sol::lua_nil);
    comp.on_hit_function = t.get_or<sol::object>("on_hit", sol::lua_nil);
    comp.on_death_function = t.get_or<sol::object>("on_death", sol::lua_nil);
    return comp;
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<ScriptCallbackComponent>(kUsertypeName, "script_table", &ScriptCallbackComponent::script_table,
                                              "on_hit", &ScriptCallbackComponent::on_hit_function, "on_death",
                                              &ScriptCallbackComponent::on_death_function);
  }
};
