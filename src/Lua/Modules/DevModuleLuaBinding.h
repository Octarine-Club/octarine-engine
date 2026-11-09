#pragma once

#include "Lua/Modules/LuaModule.h"

struct DevModule {};

template <>
struct LuaModuleBinding<DevModule> {
  static void install(sol::state& lua, LuaBindingContext& ctx);
};
