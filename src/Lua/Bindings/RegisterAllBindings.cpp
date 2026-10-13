#include "Lua/Bindings/RegisterAllBindings.h"

#include "Lua/Bindings/AnimationComponentLuaBinding.h"
#include "Lua/Bindings/AudioListenerComponentLuaBinding.h"
#include "Lua/Bindings/AudioSourceComponentLuaBinding.h"
#include "Lua/Bindings/BoxColliderComponentLuaBinding.h"
#include "Lua/Bindings/CameraFollowComponentLuaBinding.h"
#include "Lua/Bindings/ColorGridComponentLuaBinding.h"
#include "Lua/Bindings/ContactDamageComponentLuaBinding.h"
#include "Lua/Bindings/EnemySteeringComponentLuaBinding.h"
#include "Lua/Bindings/HealthComponentLuaBinding.h"
#include "Lua/Bindings/HomingComponentLuaBinding.h"
#include "Lua/Bindings/LifetimeComponentLuaBinding.h"
#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Lua/Bindings/MagnetComponentLuaBinding.h"
#include "Lua/Bindings/NameComponentLuaBinding.h"
#include "Lua/Bindings/PivotComponentLuaBinding.h"
#include "Lua/Bindings/PositionComponentLuaBinding.h"
#include "Lua/Bindings/ProjectileEmitterComponentLuaBinding.h"
#include "Lua/Bindings/RetaliationComponentLuaBinding.h"
#include "Lua/Bindings/RigidBodyComponentLuaBinding.h"
#include "Lua/Bindings/RotationComponentLuaBinding.h"
#include "Lua/Bindings/ScaleComponentLuaBinding.h"
#include "Lua/Bindings/ScriptCallbackComponentLuaBinding.h"
#include "Lua/Bindings/ScriptComponentLuaBinding.h"
#include "Lua/Bindings/SeparationComponentLuaBinding.h"
#include "Lua/Bindings/SpriteComponentLuaBinding.h"
#include "Lua/Bindings/SquarePrimitiveComponentLuaBinding.h"
#include "Lua/Bindings/TextLabelComponentLuaBinding.h"
#include "Lua/Bindings/UIAnchorComponentLuaBinding.h"
#include "Lua/Bindings/UIButtonComponentLuaBinding.h"
#include "Lua/Bindings/UICanvasComponentLuaBinding.h"
#include "Lua/Bindings/UIRectComponentLuaBinding.h"
#include "Lua/Bindings/UIZIndexComponentLuaBinding.h"
#include "Lua/Bindings/WeaponZoneComponentLuaBinding.h"

void RegisterAllLuaBindings() {
  LuaComponentRegistry::clear();
  LuaComponentRegistry::registerComponent<RigidBodyComponent>();
  LuaComponentRegistry::registerComponent<SpriteComponent>();
  LuaComponentRegistry::registerComponent<SquarePrimitiveComponent>();
  LuaComponentRegistry::registerComponent<ColorGridComponent>();
  LuaComponentRegistry::registerComponent<AnimationComponent>();
  LuaComponentRegistry::registerComponent<BoxColliderComponent>();
  LuaComponentRegistry::registerComponent<HealthComponent>();
  LuaComponentRegistry::registerComponent<LifetimeComponent>();
  LuaComponentRegistry::registerComponent<ProjectileEmitterComponent>();
  LuaComponentRegistry::registerComponent<RetaliationComponent>();
  LuaComponentRegistry::registerComponent<MagnetComponent>();
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
  LuaComponentRegistry::registerComponent<EnemySteeringComponent>();
  LuaComponentRegistry::registerComponent<SeparationComponent>();
  LuaComponentRegistry::registerComponent<HomingComponent>();
  LuaComponentRegistry::registerComponent<WeaponZoneComponent>();
  LuaComponentRegistry::registerComponent<ContactDamageComponent>();
}
