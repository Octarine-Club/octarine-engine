#pragma once

#include <sol/sol.hpp>
#include <utility>

struct ScriptCallbackComponent {
  sol::object script_table = sol::lua_nil;
  sol::object on_hit_function = sol::lua_nil;
  sol::object on_death_function = sol::lua_nil;

  ScriptCallbackComponent() = default;
  explicit ScriptCallbackComponent(sol::object t_script_table, sol::object t_on_hit_function = sol::lua_nil,
                                   sol::object t_on_death_function = sol::lua_nil)
      : script_table(std::move(t_script_table)),
        on_hit_function(std::move(t_on_hit_function)),
        on_death_function(std::move(t_on_death_function)) {}
};
