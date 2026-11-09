#include "Lua/Bindings/AnimationComponentLuaBinding.h"
#include "Lua/Bindings/CapsulePrimitiveComponentLuaBinding.h"
#include "Lua/Bindings/CirclePrimitiveComponentLuaBinding.h"
#include "Lua/Bindings/ColorGridComponentLuaBinding.h"
#include "Lua/Bindings/LinePrimitiveComponentLuaBinding.h"
#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Lua/Bindings/RegisterAllBindings.h"
#include "Lua/Bindings/SpriteComponentLuaBinding.h"
#include "Lua/Bindings/SquarePrimitiveComponentLuaBinding.h"
#include "Lua/Bindings/TrianglePrimitiveComponentLuaBinding.h"

void RegisterRenderBindings() {
  LuaComponentRegistry::registerComponent<SpriteComponent>();
  LuaComponentRegistry::registerComponent<SquarePrimitiveComponent>();
  LuaComponentRegistry::registerComponent<CirclePrimitiveComponent>();
  LuaComponentRegistry::registerComponent<LinePrimitiveComponent>();
  LuaComponentRegistry::registerComponent<TrianglePrimitiveComponent>();
  LuaComponentRegistry::registerComponent<CapsulePrimitiveComponent>();
  LuaComponentRegistry::registerComponent<ColorGridComponent>();
  LuaComponentRegistry::registerComponent<AnimationComponent>();
}
