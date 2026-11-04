#pragma once

#include <sol/sol.hpp>

#include "Components/ChunkResidentComponent.h"
#include "Lua/Bindings/LuaBinding.h"

template <>
struct LuaBinding<ChunkResidentComponent> {
  static constexpr const char* kLuaKey = "chunk_resident";
  static constexpr const char* kUsertypeName = "chunk_resident_component";

  static ChunkResidentComponent fromLua(const sol::object& data) {
    if (data.is<sol::table>()) {
      const auto t = data.as<sol::table>();
      const auto cx = LuaComponentHelpers::SafeGetOptionalValue<int32_t>(t, "chunk_x", 0);
      const auto cy = LuaComponentHelpers::SafeGetOptionalValue<int32_t>(t, "chunk_y", 0);
      const auto idx = LuaComponentHelpers::SafeGetOptionalValue<uint32_t>(t, "prop_index", 0u);
      return {cx, cy, idx};
    }
    return {};
  }

  static void bindUsertype(sol::state& lua) {
    lua.new_usertype<ChunkResidentComponent>(kUsertypeName, "chunk_x", &ChunkResidentComponent::chunkX, "chunk_y",
                                             &ChunkResidentComponent::chunkY, "prop_index",
                                             &ChunkResidentComponent::propIndex);
  }
};
