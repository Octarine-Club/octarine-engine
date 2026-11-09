#include "Lua/Bindings/AudioListenerComponentLuaBinding.h"
#include "Lua/Bindings/AudioSourceComponentLuaBinding.h"
#include "Lua/Bindings/BoxColliderComponentLuaBinding.h"
#include "Lua/Bindings/CapsuleColliderComponentLuaBinding.h"
#include "Lua/Bindings/CircleColliderComponentLuaBinding.h"
#include "Lua/Bindings/LineColliderComponentLuaBinding.h"
#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Lua/Bindings/RegisterAllBindings.h"
#include "Lua/Bindings/ScriptCallbackComponentLuaBinding.h"
#include "Lua/Bindings/ScriptComponentLuaBinding.h"
#include "Lua/Bindings/TriangleColliderComponentLuaBinding.h"

void RegisterPhysicsAudioScriptBindings() {
  LuaComponentRegistry::registerComponent<BoxColliderComponent>();
  LuaComponentRegistry::registerComponent<CircleColliderComponent>();
  LuaComponentRegistry::registerComponent<CapsuleColliderComponent>();
  LuaComponentRegistry::registerComponent<TriangleColliderComponent>();
  LuaComponentRegistry::registerComponent<LineColliderComponent>();
  LuaComponentRegistry::registerComponent<ScriptCallbackComponent>();
  LuaComponentRegistry::registerComponent<ScriptComponent>();
  LuaComponentRegistry::registerComponent<AudioSourceComponent>();
  LuaComponentRegistry::registerComponent<AudioListenerComponent>();
}
