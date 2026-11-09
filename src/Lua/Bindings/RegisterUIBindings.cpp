#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Lua/Bindings/RegisterAllBindings.h"
#include "Lua/Bindings/UIAnchorComponentLuaBinding.h"
#include "Lua/Bindings/UIButtonComponentLuaBinding.h"
#include "Lua/Bindings/UICanvasComponentLuaBinding.h"
#include "Lua/Bindings/UIClipComponentLuaBinding.h"
#include "Lua/Bindings/UIFlexComponentLuaBinding.h"
#include "Lua/Bindings/UIFocusComponentLuaBinding.h"
#include "Lua/Bindings/UILayoutBoxComponentLuaBinding.h"
#include "Lua/Bindings/UINineSliceComponentLuaBinding.h"
#include "Lua/Bindings/UIRectComponentLuaBinding.h"
#include "Lua/Bindings/UIScrollComponentLuaBinding.h"
#include "Lua/Bindings/UIStyleStateComponentLuaBinding.h"
#include "Lua/Bindings/UIZIndexComponentLuaBinding.h"

void RegisterUIBindings() {
  LuaComponentRegistry::registerComponent<UIButtonComponent>();
  LuaComponentRegistry::registerComponent<UICanvasComponent>();
  LuaComponentRegistry::registerComponent<UIAnchorComponent>();
  LuaComponentRegistry::registerComponent<UIClipComponent>();
  LuaComponentRegistry::registerComponent<UIFlexComponent>();
  LuaComponentRegistry::registerComponent<UIFocusComponent>();
  LuaComponentRegistry::registerComponent<UILayoutBoxComponent>();
  LuaComponentRegistry::registerComponent<UINineSliceComponent>();
  LuaComponentRegistry::registerComponent<UIRectComponent>();
  LuaComponentRegistry::registerComponent<UIScrollComponent>();
  LuaComponentRegistry::registerComponent<UIStyleStateComponent>();
  LuaComponentRegistry::registerComponent<UIZIndexComponent>();
}
