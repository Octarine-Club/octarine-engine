#include "Lua/Modules/LogModuleLuaBinding.h"

#include <cstddef>
#include <exception>
#include <string>

#include "General/Logger.h"
#include "Lua/LuaSchemaRegistry.h"

namespace {
using octarine::lua_schema::LuaSchemaRegistry;

int LuaHandler(lua_State* /*lua_state*/, sol::optional<const std::exception&> exception,
               const sol::string_view string_view) {
  if (exception) {
    const std::exception& err = exception.value();
    Logger::ErrorLua(std::string(err.what()));
  } else if (!string_view.empty()) {
    Logger::ErrorLua(std::string(string_view.data(), string_view.size()));
  } else {
    Logger::ErrorLua("Unknown Lua Error!");
  }
  return 0;
}

int ProtectedTolString(lua_State* L) {
  std::size_t len = 0;
  const char* str = luaL_tolstring(L, 1, &len);
  if (str == nullptr) {
    lua_pushliteral(L, "");
  }
  return 1;
}

void AppendPrintValue(std::string& out, const sol::stack_object& v) {
  if (v.get_type() == sol::type::lua_nil) {
    out += "nil";
    return;
  }
  if (v.get_type() == sol::type::boolean) {
    out += v.as<bool>() ? "true" : "false";
    return;
  }
  if (v.get_type() == sol::type::string) {
    out += v.as<std::string_view>();
    return;
  }
  lua_State* L = v.lua_state();
  const int topBefore = lua_gettop(L);
  lua_pushcfunction(L, &ProtectedTolString);
  lua_pushvalue(L, v.stack_index());
  if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
    std::size_t len = 0;
    if (const char* s = lua_tolstring(L, -1, &len)) {
      out.append(s, len);
    }
  } else {
    out += "<tostring error>";
  }
  lua_settop(L, topBefore);
}
}  // namespace

void LuaModuleBinding<LogModule>::install(sol::state& lua, LuaBindingContext& /*ctx*/) {
  LuaSchemaRegistry::SetFunction(lua, "log", &Logger::LogLua);
  LuaSchemaRegistry::SetFunction(lua, "log_e", &Logger::ErrorLua);
  LuaSchemaRegistry::SetFunction(lua, "log_w", &Logger::WarnLua);
  LuaSchemaRegistry::SetFunction(lua, "log_i", &Logger::InfoLua);
  LuaSchemaRegistry::SetFunction(lua, "print", [](sol::variadic_args va) {
    std::string msg;
    bool first = true;
    for (auto v : va) {
      if (!first) msg += "\t";
      first = false;
      AppendPrintValue(msg, sol::stack_object(v.lua_state(), v.stack_index()));
    }
    Logger::LogLua(msg);
  });

  lua.set_exception_handler(&LuaHandler);
}
