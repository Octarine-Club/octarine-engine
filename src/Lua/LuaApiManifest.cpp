#include "Lua/LuaApiManifest.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <utility>
#include <vector>

#include "Components/UIStyleStateComponent.h"
#include "ECS/Entity.h"
#include "General/Color.h"
#include "General/Logger.h"
#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Lua/LuaSchemaRegistry.h"
#include "Lua/Modules/LuaModuleRegistry.h"

namespace {
struct NamedObject {
  std::string name;
  sol::object value;
};

using octarine::lua_schema::LuaClassSchema;
using octarine::lua_schema::LuaFunctionSchema;
using octarine::lua_schema::LuaParamSchema;
using octarine::lua_schema::LuaSchemaRegistry;

bool IsSolInternalName(const std::string& name) {
  return name.find("\xe2\x99\xbb") != std::string::npos  // U+267B recycle symbol
         || name.find("::") != std::string::npos || name.find('<') != std::string::npos;
}

std::vector<NamedObject> SortedEntries(const sol::table& table) {
  std::vector<NamedObject> entries;
  for (const auto& [key, value] : table) {
    if (key.get_type() != sol::type::string) continue;
    entries.push_back({key.as<std::string>(), value});
  }
  std::sort(entries.begin(), entries.end(), [](const NamedObject& a, const NamedObject& b) { return a.name < b.name; });
  return entries;
}

std::string InferObjectScalarType(lua_State* L, const sol::object& value) {
#ifndef OCTARINE_SHIPPED
  const int top = lua_gettop(L);
  value.push(L);
  std::string t = LuaSchemaRegistry::InferLuaValueType(L, -1);
  lua_settop(L, top);
  return t;
#else
  (void)L;
  switch (value.get_type()) {
    case sol::type::boolean:
      return "boolean";
    case sol::type::number:
      return "number";
    case sol::type::string:
      return "string";
    case sol::type::table:
      return "table";
    default:
      return "any";
  }
#endif
}

void EmitScalar(std::ofstream& out, lua_State* L, const std::string& name, const sol::object& value) {
  switch (value.get_type()) {
    case sol::type::number: {
      const std::string numType = InferObjectScalarType(L, value);
      out << "---@type " << numType << "\n" << name << " = 0\n\n";
      break;
    }
    case sol::type::boolean:
      out << "---@type boolean\n" << name << " = " << (value.as<bool>() ? "true" : "false") << "\n\n";
      break;
    case sol::type::string:
      out << "---@type string\n" << name << " = \"\"\n\n";
      break;
    default:
      out << "-- " << name << " (unbound-introspectable type)\n\n";
      break;
  }
}

#ifndef OCTARINE_SHIPPED
std::string FormatOverloadType(const LuaFunctionSchema& ov) {
  std::string s = "fun(";
  for (std::size_t i = 0; i < ov.params.size(); ++i) {
    if (i > 0) s += ", ";
    const auto& p = ov.params[i];
    if (p.variadic) {
      s += "...: " + p.type;
    } else {
      s += p.name + (p.optional ? "?: " : ": ") + p.type;
    }
  }
  s += ")";
  if (!ov.returns.empty()) {
    s += ": ";
    for (std::size_t i = 0; i < ov.returns.size(); ++i) {
      if (i > 0) s += ", ";
      s += ov.returns[i];
    }
  }
  return s;
}

void EmitFunctionWithSchema(std::ofstream& out, const std::string& headerName, const LuaFunctionSchema* schema) {
  if (schema == nullptr) {
    out << "function " << headerName << "(...) end\n\n";
    return;
  }
  for (const auto& ov : schema->overloads) {
    out << "---@overload " << FormatOverloadType(ov) << "\n";
  }
  std::string argList;
  for (std::size_t i = 0; i < schema->params.size(); ++i) {
    const auto& p = schema->params[i];
    if (p.variadic) {
      out << "---@param ... " << p.type << "\n";
    } else {
      out << "---@param " << p.name << (p.optional ? "? " : " ") << p.type << "\n";
    }
    if (i > 0) argList += ", ";
    argList += p.name;
  }
  if (!schema->returns.empty()) {
    out << "---@return ";
    for (std::size_t i = 0; i < schema->returns.size(); ++i) {
      if (i > 0) out << ", ";
      out << schema->returns[i];
    }
    out << "\n";
  }
  out << "function " << headerName << "(" << argList << ") end\n\n";
}

void EmitClass(std::ofstream& out, const LuaClassSchema& cls) {
  out << "---@class " << cls.name << "\n";
  for (const auto& [fieldName, fieldType] : cls.fields) {
    out << "---@field " << fieldName << " " << fieldType << "\n";
  }
  out << cls.name << " = {}\n\n";
  for (const auto& [fnName, fnSchema] : cls.staticFunctions) {
    EmitFunctionWithSchema(out, cls.name + "." + fnName, &fnSchema);
  }
  for (const auto& [methodName, methodSchema] : cls.methods) {
    EmitFunctionWithSchema(out, cls.name + ":" + methodName, &methodSchema);
  }
}

void SeedKnownSchemas() {
  LuaSchemaRegistry::QueueUsertypeIntrospection<Entity>("entity");
  LuaSchemaRegistry::QueueUsertypeIntrospection<glm::vec2>("vec2");
  LuaSchemaRegistry::QueueUsertypeIntrospection<octarine::Color>("color");
  LuaSchemaRegistry::QueueUsertypeIntrospection<UIStylePatch>("ui_style_patch");

  auto registerFallbackFn = [](const std::string& name, std::vector<LuaParamSchema> params,
                               std::vector<std::string> returns) {
    if (LuaSchemaRegistry::FindFunction(name) == nullptr) {
      LuaSchemaRegistry::RegisterFunctionSchema(
          name, LuaFunctionSchema{.params = std::move(params), .returns = std::move(returns)});
    }
  };

  registerFallbackFn("dofile", {{.name = "path", .type = "string"}}, {"any"});
  registerFallbackFn("load_scene", {{.name = "scene_path", .type = "string"}}, {"boolean"});
  registerFallbackFn("reload_scene", {}, {"boolean"});
  registerFallbackFn("stop_scene", {}, {});
  registerFallbackFn("clear_scene", {}, {});
  registerFallbackFn("load_entity", {{.name = "entity_path", .type = "string"}}, {"entity"});
  registerFallbackFn("load_asset", {{.name = "descriptor", .type = "table"}}, {"boolean"});
  registerFallbackFn("acquire_scene_assets", {{.name = "scene_path", .type = "string", .optional = true}}, {"integer"});

  for (const auto& comp : LuaComponentRegistry::all()) {
    registerFallbackFn("registry.has_" + comp.luaKey, {{.name = "arg1", .type = "entity"}}, {"boolean"});
    registerFallbackFn("registry.get_" + comp.luaKey, {{.name = "arg1", .type = "entity"}}, {comp.usertypeName});
    registerFallbackFn("registry.try_get_" + comp.luaKey, {{.name = "arg1", .type = "entity", .optional = true}},
                       {comp.usertypeName + "?"});
    registerFallbackFn("registry.add_" + comp.luaKey,
                       {{.name = "arg1", .type = "entity"}, {.name = "arg2", .type = "any", .optional = true}},
                       {comp.usertypeName + "?"});
    registerFallbackFn("registry.remove_" + comp.luaKey, {{.name = "arg1", .type = "entity", .optional = true}},
                       {"boolean"});
  }
}

void EnrichIntrospectedComponentSchemas() {
  if (auto* entCls =
          LuaSchemaRegistry::FindClass("entity") != nullptr ? &LuaSchemaRegistry::EnsureClass("entity") : nullptr) {
    entCls->methods["get_id"] = LuaFunctionSchema{.returns = {"integer"}, .isMethod = true};
  }
  if (LuaSchemaRegistry::FindClass("script_component") != nullptr) {
    LuaSchemaRegistry::EnsureClass("script_component").fields["data"] = "table";
  }
  if (LuaSchemaRegistry::FindClass("ui_button_component") != nullptr) {
    auto& btn = LuaSchemaRegistry::EnsureClass("ui_button_component");
    btn.fields["on_click"] = "fun(...)";
    btn.fields["on_focus"] = "fun(...)";
    btn.methods["trigger"] = LuaFunctionSchema{
        .params = {{.name = "arg1", .type = "entity", .optional = true}}, .returns = {"boolean"}, .isMethod = true};
  }
  if (LuaSchemaRegistry::FindClass("lifetime_component") != nullptr) {
    auto& lt = LuaSchemaRegistry::EnsureClass("lifetime_component");
    lt.methods["extend"] = LuaFunctionSchema{.params = {{.name = "arg1", .type = "number"}}, .isMethod = true};
    lt.methods["is_alive"] = LuaFunctionSchema{.returns = {"boolean"}, .isMethod = true};
    lt.methods["reset"] = LuaFunctionSchema{.isMethod = true};
  }
  if (LuaSchemaRegistry::FindClass("scale_component") != nullptr) {
    LuaSchemaRegistry::EnsureClass("scale_component").methods["is_uniform"] =
        LuaFunctionSchema{.returns = {"boolean"}, .isMethod = true};
  }
  if (LuaSchemaRegistry::FindClass("text_label_component") != nullptr) {
    LuaSchemaRegistry::EnsureClass("text_label_component").methods["set_outline"] = LuaFunctionSchema{
        .params = {{.name = "arg1", .type = "color|number"}, {.name = "arg2", .type = "number", .optional = true}},
        .isMethod = true};
  }
}
#endif

void EmitFunction(std::ofstream& out, const std::string& qualifiedName) {
#ifndef OCTARINE_SHIPPED
  EmitFunctionWithSchema(out, qualifiedName, LuaSchemaRegistry::FindFunction(qualifiedName));
#else
  out << "function " << qualifiedName << "(...) end\n\n";
#endif
}

// Emits a namespace table and its member fields/functions.
// LuaLS requires every ---@field to sit between ---@class and the table statement.
void EmitTable(std::ofstream& out, lua_State* L, const std::string& name, const sol::table& table) {
  const auto entries = SortedEntries(table);
  const auto skip = [](const NamedObject& member) {
    return member.name.rfind("__", 0) == 0 || IsSolInternalName(member.name);
  };
  out << "---@class " << name << "\n";
  for (const auto& member : entries) {
    if (skip(member) || member.value.get_type() == sol::type::function) continue;
    out << "---@field " << member.name << " " << InferObjectScalarType(L, member.value) << "\n";
  }
  out << name << " = {}\n\n";
  for (const auto& member : entries) {
    if (skip(member) || member.value.get_type() != sol::type::function) continue;
    EmitFunction(out, name + "." + member.name);
  }
  out << "\n";
}
}  // namespace

namespace LuaApiManifest {
std::unordered_set<std::string> SnapshotGlobals(sol::state& lua) {
  std::unordered_set<std::string> names;
  for (const auto& [key, value] : lua.globals()) {
    (void)value;
    if (key.get_type() == sol::type::string) names.insert(key.as<std::string>());
  }
  return names;
}

bool Write(sol::state& lua, const std::unordered_set<std::string>& before, const std::string& outPath) {
  const auto parent = std::filesystem::path(outPath).parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
  }
  std::ofstream out(outPath);
  if (!out.is_open()) {
    Logger::Error("LuaApiManifest: could not open '" + outPath + "' for writing.");
    return false;
  }

#ifndef OCTARINE_SHIPPED
  SeedKnownSchemas();
  LuaSchemaRegistry::RunPendingIntrospection(lua);
  EnrichIntrospectedComponentSchemas();
#endif

  std::vector<NamedObject> added;
  for (const auto& [key, value] : lua.globals()) {
    if (key.get_type() != sol::type::string) continue;
    const std::string name = key.as<std::string>();
    if (before.contains(name)) continue;
    if (IsSolInternalName(name)) continue;
    added.push_back({name, value});
  }
  std::sort(added.begin(), added.end(), [](const NamedObject& a, const NamedObject& b) { return a.name < b.name; });

  out << "-- AUTO-GENERATED Octarine Lua API surface. Do not edit by hand.\n"
      << "-- Introspected from the live sol::state after all bindings install.\n"
      << "-- Refreshed automatically on every dev launch (types/octarine_api.lua);\n"
      << "-- OCTARINE_DUMP_LUA_API=<path> overrides the destination.\n"
      << "-- NOTE: registry.get_<key>(e) and registry.try_get_<key>(e) return zero-copy\n"
      << "-- borrows into SoA archetype chunks; re-fetch after structural ECS mutations.\n"
      << "---@meta\n"
      << "---@diagnostic disable: lowercase-global, missing-return\n\n";

  lua_State* L = lua.lua_state();
  for (const auto& entry : added) {
#ifndef OCTARINE_SHIPPED
    if (const LuaClassSchema* cls = LuaSchemaRegistry::FindClass(entry.name); cls != nullptr) {
      EmitClass(out, *cls);
      continue;
    }
#endif
    switch (entry.value.get_type()) {
      case sol::type::function:
        EmitFunction(out, entry.name);
        break;
      case sol::type::table:
        EmitTable(out, L, entry.name, entry.value.as<sol::table>());
        break;
      case sol::type::userdata:
        out << "---@class " << entry.name << "\n" << entry.name << " = {}\n\n";
        break;
      default:
        EmitScalar(out, L, entry.name, entry.value);
        break;
    }
  }

  Logger::Info("LuaApiManifest: wrote " + std::to_string(added.size()) + " entries to " + outPath);
  return true;
}

bool MaybeDumpFromEnv(sol::state& lua, const std::unordered_set<std::string>& before) {
#ifdef _MSC_VER
  char* env = nullptr;
  size_t len = 0;
  const bool hasEnv = _dupenv_s(&env, &len, "OCTARINE_DUMP_LUA_API") == 0 && env != nullptr;
  const std::string value = hasEnv ? std::string(env) : std::string();
  free(env);
  if (value.empty()) return false;
#else
  const char* env = std::getenv("OCTARINE_DUMP_LUA_API");
  if (env == nullptr || env[0] == '\0') return false;
  const std::string value(env);
#endif
  const std::string outPath = (value == "1") ? "lua_api.lua" : value;
  return Write(lua, before, outPath);
}

bool WriteProjectStub(sol::state& lua, const std::unordered_set<std::string>& before, const std::string& assetPath) {
  const std::filesystem::path dir = std::filesystem::path(assetPath) / "types";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    Logger::Error("LuaApiManifest: could not create '" + dir.string() + "': " + ec.message());
    return false;
  }
  return Write(lua, before, (dir / "octarine_api.lua").string());
}

namespace {
// Minimal JSON string escaping. Component/module identifiers and global names are plain
// ASCII Lua keys so this only ever needs to deal with the standard control chars; spelling
// out a few cases beats pulling in a JSON dep just for this manifest.
void WriteJsonString(std::ofstream& out, const std::string& s) {
  out << '"';
  for (const char c : s) {
    switch (c) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out << buf;
        } else {
          out << c;
        }
    }
  }
  out << '"';
}
}  // namespace

bool WriteComponentsJson(const std::string& outPath) {
  const auto parent = std::filesystem::path(outPath).parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
  }
  std::ofstream out(outPath);
  if (!out.is_open()) {
    Logger::Error("LuaApiManifest: could not open '" + outPath + "' for writing.");
    return false;
  }

  // Snapshot entries and sort by lua_key for stable output.
  auto entries = LuaComponentRegistry::all();
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.luaKey < b.luaKey; });

  out << "{\n  \"_generated_by\": \"OctarineLuaApiTest via LuaApiManifest::WriteComponentsJson\",\n";
  out << "  \"components\": [\n";
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];
    out << "    {\n      \"lua_key\": ";
    WriteJsonString(out, e.luaKey);
    out << ",\n      \"usertype\": ";
    WriteJsonString(out, e.usertypeName);
    out << ",\n      \"has_accessor\": ";
    WriteJsonString(out, "registry.has_" + e.luaKey);
    out << ",\n      \"get_accessor\": ";
    WriteJsonString(out, "registry.get_" + e.luaKey);
    out << ",\n      \"try_get_accessor\": ";
    WriteJsonString(out, "registry.try_get_" + e.luaKey);
    out << ",\n      \"add_accessor\": ";
    WriteJsonString(out, "registry.add_" + e.luaKey);
    out << ",\n      \"remove_accessor\": ";
    WriteJsonString(out, "registry.remove_" + e.luaKey);
    out << "\n    }";
    if (i + 1 < entries.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n}\n";

  Logger::Info("LuaApiManifest: wrote " + std::to_string(entries.size()) + " components to " + outPath);
  return true;
}

bool WriteModulesJson(const std::string& outPath) {
  const auto parent = std::filesystem::path(outPath).parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
  }
  std::ofstream out(outPath);
  if (!out.is_open()) {
    Logger::Error("LuaApiManifest: could not open '" + outPath + "' for writing.");
    return false;
  }

  // Preserve install order — that order matters for setup-ordering gotchas, so capturing it
  // makes modules.json useful when diagnosing "what installed before what."
  const auto& modules = LuaModuleRegistry::all();

  out << "{\n  \"_generated_by\": \"OctarineLuaApiTest via LuaApiManifest::WriteModulesJson\",\n";
  out << "  \"modules\": [\n";
  for (std::size_t i = 0; i < modules.size(); ++i) {
    const auto& m = modules[i];
    out << "    {\n      \"name\": ";
    WriteJsonString(out, m.name);
    out << ",\n      \"binding_header\": ";
    WriteJsonString(out, "src/Lua/Modules/" + m.name + "ModuleLuaBinding.h");
    out << ",\n      \"globals\": [";
    // Sort globals within a module for stable output (module order is install order; globals within are unordered).
    std::vector<std::string> globals = m.globals;
    std::sort(globals.begin(), globals.end());
    for (std::size_t g = 0; g < globals.size(); ++g) {
      if (g > 0) out << ", ";
      WriteJsonString(out, globals[g]);
    }
    out << "]\n    }";
    if (i + 1 < modules.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n}\n";

  Logger::Info("LuaApiManifest: wrote " + std::to_string(modules.size()) + " modules to " + outPath);
  return true;
}
}  // namespace LuaApiManifest
