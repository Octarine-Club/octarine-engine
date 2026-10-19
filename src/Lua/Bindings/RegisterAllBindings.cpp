#include "Lua/Bindings/RegisterAllBindings.h"

#include "Engine/GameModuleHooks.h"
#include "Lua/Bindings/AnimationComponentLuaBinding.h"
#include "Lua/Bindings/AudioListenerComponentLuaBinding.h"
#include "Lua/Bindings/AudioSourceComponentLuaBinding.h"
#include "Lua/Bindings/BoxColliderComponentLuaBinding.h"
#include "Lua/Bindings/CameraFollowComponentLuaBinding.h"
#include "Lua/Bindings/CapsuleColliderComponentLuaBinding.h"
#include "Lua/Bindings/CapsulePrimitiveComponentLuaBinding.h"
#include "Lua/Bindings/CircleColliderComponentLuaBinding.h"
#include "Lua/Bindings/CirclePrimitiveComponentLuaBinding.h"
#include "Lua/Bindings/ColorGridComponentLuaBinding.h"
#include "Lua/Bindings/LifetimeComponentLuaBinding.h"
#include "Lua/Bindings/LineColliderComponentLuaBinding.h"
#include "Lua/Bindings/LinePrimitiveComponentLuaBinding.h"
#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Lua/Bindings/NameComponentLuaBinding.h"
#include "Lua/Bindings/PivotComponentLuaBinding.h"
#include "Lua/Bindings/PositionComponentLuaBinding.h"
#include "Lua/Bindings/RigidBodyComponentLuaBinding.h"
#include "Lua/Bindings/RotationComponentLuaBinding.h"
#include "Lua/Bindings/ScaleComponentLuaBinding.h"
#include "Lua/Bindings/ScriptCallbackComponentLuaBinding.h"
#include "Lua/Bindings/ScriptComponentLuaBinding.h"
#include "Lua/Bindings/SpriteComponentLuaBinding.h"
#include "Lua/Bindings/SquarePrimitiveComponentLuaBinding.h"
#include "Lua/Bindings/TextLabelComponentLuaBinding.h"
#include "Lua/Bindings/TriangleColliderComponentLuaBinding.h"
#include "Lua/Bindings/TrianglePrimitiveComponentLuaBinding.h"
#include "Lua/Bindings/UIAnchorComponentLuaBinding.h"
#include "Lua/Bindings/UIButtonComponentLuaBinding.h"
#include "Lua/Bindings/UICanvasComponentLuaBinding.h"
#include "Lua/Bindings/UIRectComponentLuaBinding.h"
#include "Lua/Bindings/UIZIndexComponentLuaBinding.h"

void RegisterAllLuaBindings(bool includeGameModule) {
  LuaComponentRegistry::clear();
  LuaComponentRegistry::registerComponent<RigidBodyComponent>();
  LuaComponentRegistry::registerComponent<SpriteComponent>();
  LuaComponentRegistry::registerComponent<SquarePrimitiveComponent>();
  LuaComponentRegistry::registerComponent<CirclePrimitiveComponent>();
  LuaComponentRegistry::registerComponent<LinePrimitiveComponent>();
  LuaComponentRegistry::registerComponent<TrianglePrimitiveComponent>();
  LuaComponentRegistry::registerComponent<CapsulePrimitiveComponent>();
  LuaComponentRegistry::registerComponent<ColorGridComponent>();
  LuaComponentRegistry::registerComponent<AnimationComponent>();
  LuaComponentRegistry::registerComponent<BoxColliderComponent>();
  LuaComponentRegistry::registerComponent<CircleColliderComponent>();
  LuaComponentRegistry::registerComponent<CapsuleColliderComponent>();
  LuaComponentRegistry::registerComponent<TriangleColliderComponent>();
  LuaComponentRegistry::registerComponent<LineColliderComponent>();
  LuaComponentRegistry::registerComponent<LifetimeComponent>();
  LuaComponentRegistry::registerComponent<CameraFollowComponent>();
  LuaComponentRegistry::registerComponent<ScriptCallbackComponent>();
  LuaComponentRegistry::registerComponent<ScriptComponent>();
  LuaComponentRegistry::registerComponent<UIButtonComponent>();
  LuaComponentRegistry::registerComponent<TextLabelComponent>();
  LuaComponentRegistry::registerComponent<AudioSourceComponent>();
  LuaComponentRegistry::registerComponent<AudioListenerComponent>();
  LuaComponentRegistry::registerComponent<NameComponent>();
  LuaComponentRegistry::registerComponent<ScaleComponent>();
  LuaComponentRegistry::registerComponent<PositionComponent>();
  LuaComponentRegistry::registerComponent<RotationComponent>();
  LuaComponentRegistry::registerComponent<PivotComponent>();
  LuaComponentRegistry::registerComponent<UICanvasComponent>();
  LuaComponentRegistry::registerComponent<UIAnchorComponent>();
  LuaComponentRegistry::registerComponent<UIRectComponent>();
  LuaComponentRegistry::registerComponent<UIZIndexComponent>();

  if (includeGameModule) {
    game_module::RegisterComponentBindings();
  }
}
