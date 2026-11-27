#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <optional>
#include <sol/sol.hpp>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Components/UIStyleStateComponent.h"
#include "ECS/Entity.h"
#include "General/Color.h"
#include "Lua/Bindings/LuaBinding.h"

namespace octarine::lua_schema {

namespace detail {

template <typename T>
struct IsStdOptional : std::false_type {};
template <typename U>
struct IsStdOptional<std::optional<U>> : std::true_type {
  using Inner = U;
};

template <typename T>
struct IsSolOptional : std::false_type {};
template <typename U>
struct IsSolOptional<sol::optional<U>> : std::true_type {
  using Inner = U;
};

template <typename T>
struct IsStdVector : std::false_type {};
template <typename U, typename A>
struct IsStdVector<std::vector<U, A>> : std::true_type {
  using Inner = U;
};

template <typename T>
struct IsStdTuple : std::false_type {};
template <typename... Ts>
struct IsStdTuple<std::tuple<Ts...>> : std::true_type {};

template <typename T>
struct IsSolOverloadSet : std::false_type {};
template <typename... Fns>
struct IsSolOverloadSet<sol::overload_set<Fns...>> : std::true_type {};

template <typename T>
concept HasLuaBindingName = requires {
  { LuaBinding<T>::kUsertypeName } -> std::convertible_to<std::string_view>;
};

}  // namespace detail

// Maps C++ types bound via sol2 to LuaLS / EmmyLua type names at compile time.
template <typename T>
struct LuaTypeName {
  static std::string Get() {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_void_v<U>) {
      return {};
    } else if constexpr (std::is_same_v<U, bool>) {
      return "boolean";
    } else if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, std::string_view> ||
                         std::is_same_v<U, sol::string_view> || std::is_same_v<U, const char*> ||
                         std::is_same_v<U, char*>) {
      return "string";
    } else if constexpr (std::integral<U>) {
      return "integer";
    } else if constexpr (std::floating_point<U>) {
      return "number";
    } else if constexpr (std::is_enum_v<U>) {
      return "integer";
    } else if constexpr (std::is_same_v<U, Entity>) {
      return "entity";
    } else if constexpr (std::is_same_v<U, glm::vec2>) {
      return "vec2";
    } else if constexpr (std::is_same_v<U, octarine::Color>) {
      return "color";
    } else if constexpr (std::is_same_v<U, UIStylePatch>) {
      return "ui_style_patch";
    } else if constexpr (detail::HasLuaBindingName<U>) {
      return std::string(LuaBinding<U>::kUsertypeName);
    } else if constexpr (std::is_pointer_v<U> &&
                         detail::HasLuaBindingName<std::remove_cv_t<std::remove_pointer_t<U>>>) {
      return std::string(LuaBinding<std::remove_cv_t<std::remove_pointer_t<U>>>::kUsertypeName) + "?";
    } else if constexpr (std::is_same_v<U, sol::table> || std::is_same_v<U, sol::lua_table>) {
      return "table";
    } else if constexpr (std::is_same_v<U, sol::protected_function> || std::is_same_v<U, sol::function> ||
                         std::is_same_v<U, sol::unsafe_function>) {
      return "fun(...)";
    } else if constexpr (detail::IsStdOptional<U>::value) {
      return LuaTypeName<typename detail::IsStdOptional<U>::Inner>::Get() + "?";
    } else if constexpr (detail::IsSolOptional<U>::value) {
      return LuaTypeName<typename detail::IsSolOptional<U>::Inner>::Get() + "?";
    } else if constexpr (detail::IsStdVector<U>::value) {
      return LuaTypeName<typename detail::IsStdVector<U>::Inner>::Get() + "[]";
    } else {
      return "any";
    }
  }
};

struct LuaParamSchema {
  std::string name;
  std::string type;
  bool optional = false;
  bool variadic = false;
};

struct LuaFunctionSchema {
  std::vector<LuaParamSchema> params;
  std::vector<std::string> returns;
  std::vector<LuaFunctionSchema> overloads;
  bool isMethod = false;
};

struct LuaClassSchema {
  std::string name;
  std::map<std::string, std::string> fields;
  std::map<std::string, LuaFunctionSchema> methods;
  std::map<std::string, LuaFunctionSchema> staticFunctions;
};

#ifndef OCTARINE_SHIPPED

namespace detail {

template <typename Arg>
void AppendParamSchema(std::vector<LuaParamSchema>& out) {
  using U = std::remove_cvref_t<Arg>;
  if constexpr (std::is_same_v<U, sol::this_state> || std::is_same_v<U, sol::this_environment>) {
    return;
  } else if constexpr (std::is_same_v<U, sol::variadic_args>) {
    out.push_back(LuaParamSchema{.name = "...", .type = "any", .optional = false, .variadic = true});
  } else if constexpr (IsStdOptional<U>::value) {
    const std::string idx = "arg" + std::to_string(out.size() + 1);
    out.push_back(LuaParamSchema{.name = idx,
                                 .type = LuaTypeName<typename IsStdOptional<U>::Inner>::Get(),
                                 .optional = true,
                                 .variadic = false});
  } else if constexpr (IsSolOptional<U>::value) {
    const std::string idx = "arg" + std::to_string(out.size() + 1);
    out.push_back(LuaParamSchema{.name = idx,
                                 .type = LuaTypeName<typename IsSolOptional<U>::Inner>::Get(),
                                 .optional = true,
                                 .variadic = false});
  } else {
    const std::string idx = "arg" + std::to_string(out.size() + 1);
    out.push_back(LuaParamSchema{.name = idx, .type = LuaTypeName<U>::Get(), .optional = false, .variadic = false});
  }
}

template <typename Tuple, std::size_t... Is>
void AppendTupleReturns(std::vector<std::string>& out, std::index_sequence<Is...>) {
  (out.push_back(LuaTypeName<std::tuple_element_t<Is, Tuple>>::Get()), ...);
}

template <typename Ret>
void PopulateReturnSchema(std::vector<std::string>& out) {
  using U = std::remove_cvref_t<Ret>;
  if constexpr (std::is_void_v<U>) {
    return;
  } else if constexpr (IsStdTuple<U>::value) {
    AppendTupleReturns<U>(out, std::make_index_sequence<std::tuple_size_v<U>>{});
  } else {
    out.push_back(LuaTypeName<U>::Get());
  }
}

template <typename Ret, typename... Args>
LuaFunctionSchema BuildSignatureSchema(bool skipFirstSelf = false) {
  LuaFunctionSchema schema;
  schema.isMethod = skipFirstSelf;
  if constexpr (sizeof...(Args) > 0) {
    if (skipFirstSelf) {
      bool first = true;
      auto appendRest = [&]<typename A>() {
        if (first) {
          first = false;
          return;
        }
        AppendParamSchema<A>(schema.params);
      };
      (appendRest.template operator()<Args>(), ...);
    } else {
      (AppendParamSchema<Args>(schema.params), ...);
    }
  }
  PopulateReturnSchema<Ret>(schema.returns);
  return schema;
}

template <typename T>
struct CallableTraits : std::false_type {};

template <typename R, typename... Args>
struct CallableTraits<R (*)(Args...)> : std::true_type {
  static LuaFunctionSchema Extract(bool skipSelf = false) { return BuildSignatureSchema<R, Args...>(skipSelf); }
};

template <typename R, typename... Args>
struct CallableTraits<R (*)(Args...) noexcept> : std::true_type {
  static LuaFunctionSchema Extract(bool skipSelf = false) { return BuildSignatureSchema<R, Args...>(skipSelf); }
};

template <typename R, typename C, typename... Args>
struct CallableTraits<R (C::*)(Args...)> : std::true_type {
  static LuaFunctionSchema Extract(bool skipSelf = false) { return BuildSignatureSchema<R, Args...>(skipSelf); }
};

template <typename R, typename C, typename... Args>
struct CallableTraits<R (C::*)(Args...) const> : std::true_type {
  static LuaFunctionSchema Extract(bool skipSelf = false) { return BuildSignatureSchema<R, Args...>(skipSelf); }
};

template <typename R, typename C, typename... Args>
struct CallableTraits<R (C::*)(Args...) noexcept> : std::true_type {
  static LuaFunctionSchema Extract(bool skipSelf = false) { return BuildSignatureSchema<R, Args...>(skipSelf); }
};

template <typename R, typename C, typename... Args>
struct CallableTraits<R (C::*)(Args...) const noexcept> : std::true_type {
  static LuaFunctionSchema Extract(bool skipSelf = false) { return BuildSignatureSchema<R, Args...>(skipSelf); }
};

template <typename Fn>
concept HasCallOperator = requires { &std::remove_cvref_t<Fn>::operator(); };

template <typename Fn>
bool TryExtractSingleCallable(LuaFunctionSchema& out, bool skipSelf = false) {
  using U = std::remove_cvref_t<Fn>;
  if constexpr (std::is_same_v<U, lua_CFunction>) {
    out.params.push_back({.name = "...", .type = "any", .optional = false, .variadic = true});
    return true;
  } else if constexpr (CallableTraits<U>::value) {
    out = CallableTraits<U>::Extract(skipSelf);
    return true;
  } else if constexpr (std::is_function_v<std::remove_pointer_t<U>>) {
    out = CallableTraits<std::add_pointer_t<std::remove_pointer_t<U>>>::Extract(skipSelf);
    return true;
  } else if constexpr (HasCallOperator<U>) {
    using OpPtr = decltype(&U::operator());
    if constexpr (CallableTraits<OpPtr>::value) {
      out = CallableTraits<OpPtr>::Extract(skipSelf);
      return true;
    }
  }
  return false;
}

template <typename... Fns, std::size_t... Is>
bool ExtractOverloadSet(LuaFunctionSchema& out, std::index_sequence<Is...>) {
  std::vector<LuaFunctionSchema> extracted;
  extracted.reserve(sizeof...(Fns));
  auto collectOne = [&]<typename F>() {
    LuaFunctionSchema s;
    if (TryExtractSingleCallable<F>(s, false)) {
      extracted.push_back(std::move(s));
    }
  };
  (collectOne.template operator()<Fns>(), ...);
  if (extracted.empty()) return false;
  out = std::move(extracted.front());
  for (std::size_t i = 1; i < extracted.size(); ++i) {
    out.overloads.push_back(std::move(extracted[i]));
  }
  return true;
}

template <typename... Fns>
bool ExtractOverloadSetPack(const sol::overload_set<Fns...>&, LuaFunctionSchema& out) {
  return ExtractOverloadSet<Fns...>(out, std::index_sequence_for<Fns...>{});
}

template <typename Fn>
bool TryExtractFunctionSchema(const Fn& fn, LuaFunctionSchema& out, bool skipSelf = false) {
  using U = std::remove_cvref_t<Fn>;
  if constexpr (IsSolOverloadSet<U>::value) {
    return ExtractOverloadSetPack(fn, out);
  } else {
    (void)fn;
    return TryExtractSingleCallable<U>(out, skipSelf);
  }
}

inline int ProtectedFieldProbe(lua_State* L) {
  const char* key = lua_tostring(L, 2);
  lua_settop(L, 1);
  lua_getfield(L, 1, key);
  return 1;
}

}  // namespace detail

class LuaSchemaRegistry {
 public:
  using ProbeCallback = std::function<void(sol::state&)>;

  struct UsertypeProbeEntry {
    std::string className;
    ProbeCallback registerMetatables;
    ProbeCallback introspectMembers;
  };

  static void Clear() {
    functions().clear();
    classes().clear();
    metatableNames().clear();
    probes().clear();
  }

  static void RegisterFunctionSchema(std::string qualifiedName, LuaFunctionSchema schema) {
    functions()[std::move(qualifiedName)] = std::move(schema);
  }

  static const LuaFunctionSchema* FindFunction(const std::string& qualifiedName) {
    const auto it = functions().find(qualifiedName);
    return it != functions().end() ? &it->second : nullptr;
  }

  static LuaClassSchema& EnsureClass(const std::string& className) {
    auto& cls = classes()[className];
    cls.name = className;
    return cls;
  }

  static const LuaClassSchema* FindClass(const std::string& className) {
    const auto it = classes().find(className);
    return it != classes().end() ? &it->second : nullptr;
  }

  static void RegisterMetatableName(const void* mtPtr, std::string className) {
    if (mtPtr != nullptr && !className.empty()) {
      metatableNames()[mtPtr] = std::move(className);
    }
  }

  static std::string LookupMetatableName(const void* mtPtr) {
    if (mtPtr == nullptr) return {};
    const auto it = metatableNames().find(mtPtr);
    return it != metatableNames().end() ? it->second : std::string{};
  }

  template <typename Fn>
  static void SetFunction(sol::state& lua, std::string_view name, Fn&& fn) {
    LuaFunctionSchema schema;
    if (detail::TryExtractFunctionSchema(fn, schema, false)) {
      RegisterFunctionSchema(std::string(name), std::move(schema));
    }
    lua.set_function(std::string(name), std::forward<Fn>(fn));
  }

  template <typename Fn>
  static void SetFunction(sol::table& tbl, std::string_view tablePrefix, std::string_view name, Fn&& fn) {
    LuaFunctionSchema schema;
    if (detail::TryExtractFunctionSchema(fn, schema, false)) {
      RegisterFunctionSchema(std::string(tablePrefix) + "." + std::string(name), std::move(schema));
    }
    tbl.set_function(std::string(name), std::forward<Fn>(fn));
  }

  template <typename T>
  static void RegisterUsertypeMetatables(sol::state& lua, std::string_view usertypeName) {
    lua_State* L = lua.lua_state();
    auto maybeUts = sol::u_detail::maybe_get_usertype_storage<T>(L);
    if (!maybeUts) return;
    auto& uts = *maybeUts;
    const std::string clsName(usertypeName);
    EnsureClass(clsName);

    auto registerRef = [&](sol::stateless_reference& ref) {
      const int pushed = ref.push(L);
      if (pushed > 0) {
        RegisterMetatableName(lua_topointer(L, -1), clsName);
        lua_pop(L, pushed);
      }
    };
    registerRef(uts.value_index_table);
    registerRef(uts.reference_index_table);
    registerRef(uts.const_reference_index_table);
    registerRef(uts.const_value_index_table);
    registerRef(uts.unique_index_table);
  }

  static std::string InferLuaValueType(lua_State* L, int idx) {
    const int absIdx = lua_absindex(L, idx);
    switch (lua_type(L, absIdx)) {
      case LUA_TBOOLEAN:
        return "boolean";
      case LUA_TNUMBER:
        return lua_isinteger(L, absIdx) != 0 ? "integer" : "number";
      case LUA_TSTRING:
        return "string";
      case LUA_TTABLE:
        return "table";
      case LUA_TFUNCTION:
        return "fun(...)";
      case LUA_TUSERDATA: {
        if (lua_getmetatable(L, absIdx) != 0) {
          const void* mtPtr = lua_topointer(L, -1);
          lua_pop(L, 1);
          const std::string mapped = LookupMetatableName(mtPtr);
          if (!mapped.empty()) return mapped;
        }
        return "any";
      }
      default:
        return "any";
    }
  }

  static void IntrospectFromStorageBase(lua_State* L, sol::u_detail::usertype_storage_base& usb,
                                        const std::string& className, const sol::object& sampleObj) {
    auto& cls = EnsureClass(className);
    for (const auto& [keySv, ics] : usb.string_keys) {
      const std::string key(keySv.data(), keySv.size());
      if (key.empty() || key.rfind("__", 0) == 0) continue;
      if (key == "new") {
        if (!cls.staticFunctions.contains("new")) {
          LuaFunctionSchema ctor;
          ctor.params.push_back({.name = "...", .type = "any", .optional = false, .variadic = true});
          ctor.returns.push_back(className);
          cls.staticFunctions["new"] = std::move(ctor);
        }
        continue;
      }

      const int topBefore = lua_gettop(L);
      bool isMethod = false;
      std::string inferred = "any";
      if (sampleObj.valid() && sampleObj != sol::lua_nil) {
        lua_pushcfunction(L, &detail::ProtectedFieldProbe);
        sampleObj.push(L);
        lua_pushlstring(L, key.data(), key.size());
        if (lua_pcall(L, 2, 1, 0) == LUA_OK) {
          if (lua_isfunction(L, -1)) {
            isMethod = true;
          } else {
            inferred = InferLuaValueType(L, -1);
          }
        }
        lua_settop(L, topBefore);
      }

      if (isMethod) {
        if (!cls.methods.contains(key)) {
          LuaFunctionSchema fn;
          fn.isMethod = true;
          fn.params.push_back({.name = "...", .type = "any", .optional = false, .variadic = true});
          cls.methods[key] = std::move(fn);
        }
        continue;
      }

      if (cls.fields.contains(key) && cls.fields[key] != "any") {
        continue;
      }
      cls.fields[key] = std::move(inferred);
    }
  }

  template <typename T>
  static void QueueUsertypeIntrospection(std::string_view usertypeName) {
    const std::string clsName(usertypeName);
    for (const auto& existing : probes()) {
      if (existing.className == clsName) return;
    }
    UsertypeProbeEntry entry;
    entry.className = clsName;
    entry.registerMetatables = [clsName](sol::state& lua) { RegisterUsertypeMetatables<T>(lua, clsName); };
    entry.introspectMembers = [clsName](sol::state& lua) {
      lua_State* L = lua.lua_state();
      auto maybeUts = sol::u_detail::maybe_get_usertype_storage<T>(L);
      if (!maybeUts) return;
      sol::object sampleObj = sol::lua_nil;
      if constexpr (std::is_default_constructible_v<T>) {
        sampleObj = sol::make_object(lua, T{});
      }
      IntrospectFromStorageBase(L, *maybeUts, clsName, sampleObj);
    };
    probes().push_back(std::move(entry));
  }

  static void RunPendingIntrospection(sol::state& lua) {
    metatableNames().clear();
    for (const auto& entry : probes()) {
      entry.registerMetatables(lua);
    }
    for (const auto& entry : probes()) {
      entry.introspectMembers(lua);
    }
  }

 private:
  static std::unordered_map<std::string, LuaFunctionSchema>& functions() {
    static std::unordered_map<std::string, LuaFunctionSchema> instance;
    return instance;
  }

  static std::unordered_map<std::string, LuaClassSchema>& classes() {
    static std::unordered_map<std::string, LuaClassSchema> instance;
    return instance;
  }

  static std::unordered_map<const void*, std::string>& metatableNames() {
    static std::unordered_map<const void*, std::string> instance;
    return instance;
  }

  static std::vector<UsertypeProbeEntry>& probes() {
    static std::vector<UsertypeProbeEntry> instance;
    return instance;
  }
};

#else

class LuaSchemaRegistry {
 public:
  template <typename Fn>
  static void SetFunction(sol::state& lua, std::string_view name, Fn&& fn) {
    lua.set_function(std::string(name), std::forward<Fn>(fn));
  }

  template <typename Fn>
  static void SetFunction(sol::table& tbl, std::string_view /*tablePrefix*/, std::string_view name, Fn&& fn) {
    tbl.set_function(std::string(name), std::forward<Fn>(fn));
  }

  template <typename T>
  static void QueueUsertypeIntrospection(std::string_view /*usertypeName*/) {}

  static void RunPendingIntrospection(sol::state& /*lua*/) {}
};

#endif

}  // namespace octarine::lua_schema
