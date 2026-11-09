#include "Lua/Bindings/RegisterAllBindings.h"

#include "Lua/Bindings/LuaComponentRegistry.h"

void RegisterAllLuaBindings() {
  LuaComponentRegistry::clear();
  RegisterTransformCoreBindings();
  RegisterRenderBindings();
  RegisterPhysicsAudioScriptBindings();
  RegisterUIBindings();
}
