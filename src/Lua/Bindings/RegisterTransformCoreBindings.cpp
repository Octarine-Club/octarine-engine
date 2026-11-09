#include "Lua/Bindings/CameraFollowComponentLuaBinding.h"
#include "Lua/Bindings/ChunkResidentComponentLuaBinding.h"
#include "Lua/Bindings/LifetimeComponentLuaBinding.h"
#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Lua/Bindings/NameComponentLuaBinding.h"
#include "Lua/Bindings/PivotComponentLuaBinding.h"
#include "Lua/Bindings/PositionComponentLuaBinding.h"
#include "Lua/Bindings/RegisterAllBindings.h"
#include "Lua/Bindings/RigidBodyComponentLuaBinding.h"
#include "Lua/Bindings/RotationComponentLuaBinding.h"
#include "Lua/Bindings/ScaleComponentLuaBinding.h"
#include "Lua/Bindings/TextLabelComponentLuaBinding.h"

void RegisterTransformCoreBindings() {
  LuaComponentRegistry::registerComponent<RigidBodyComponent>();
  LuaComponentRegistry::registerComponent<NameComponent>();
  LuaComponentRegistry::registerComponent<ScaleComponent>();
  LuaComponentRegistry::registerComponent<PositionComponent>();
  LuaComponentRegistry::registerComponent<RotationComponent>();
  LuaComponentRegistry::registerComponent<PivotComponent>();
  LuaComponentRegistry::registerComponent<LifetimeComponent>();
  LuaComponentRegistry::registerComponent<CameraFollowComponent>();
  LuaComponentRegistry::registerComponent<ChunkResidentComponent>();
  LuaComponentRegistry::registerComponent<TextLabelComponent>();
}
