#pragma once

#include "Lua/Modules/LuaModule.h"

struct TilemapModule {};

template <>
struct LuaModuleBinding<TilemapModule> {
  static void install(sol::state& lua, LuaBindingContext& ctx);
};
