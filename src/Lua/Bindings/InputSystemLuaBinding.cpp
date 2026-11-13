#include "Lua/Bindings/InputSystemLuaBinding.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <glm/glm.hpp>
#include <string>

#include "Components/ViewportInfo.h"
#include "ECS/Registry.h"
#include "General/Logger.h"
#include "Systems/InputSystem.h"

namespace {
int ToMouseButton(const sol::object& btn) {
  if (btn.is<int>()) return btn.as<int>();
  if (btn.is<std::string>()) {
    const std::string name = InputSystem::MakeKey(btn.as<std::string>());
    if (name == "left") return SDL_BUTTON_LEFT;
    if (name == "middle") return SDL_BUTTON_MIDDLE;
    if (name == "right") return SDL_BUTTON_RIGHT;
    if (name == "x1") return SDL_BUTTON_X1;
    if (name == "x2") return SDL_BUTTON_X2;
  }
  return 0;
}

sol::table MakeActionBindingsTable(sol::state_view lua, const InputSystem& system, const std::string& action) {
  sol::table entry = lua.create_table();
  sol::table keys = lua.create_table();
  int keyIdx = 1;
  for (const auto& k : system.GetActionKeyBindings(action)) {
    keys[keyIdx++] = k;
  }
  sol::table gamepad = lua.create_table();
  int padIdx = 1;
  for (const auto& btn : system.GetActionGamepadBindings(action)) {
    gamepad[padIdx++] = btn;
  }
  entry["keys"] = keys;
  entry["gamepad"] = gamepad;
  return entry;
}

template <typename BindFn>
void ApplyBindingList(const sol::object& listOrStr, const std::string& action, BindFn&& bindFn) {
  if (listOrStr.is<std::string>()) {
    bindFn(action, listOrStr.as<std::string>());
    return;
  }
  if (!listOrStr.is<sol::table>()) return;
  const sol::table arr = listOrStr.as<sol::table>();
  const std::size_t count = arr.size();
  for (std::size_t i = 1; i <= count; ++i) {
    const sol::object item = arr.get<sol::object>(i);
    if (item.is<std::string>()) {
      bindFn(action, item.as<std::string>());
    }
  }
}

void ApplyActionSpec(InputSystem& system, const std::string& action, const sol::table& spec) {
  const sol::object keysObj = spec.get<sol::object>("keys");
  if (keysObj.valid() && keysObj != sol::lua_nil) {
    system.UnbindActionKeys(action);
    ApplyBindingList(keysObj, action, [&system](const std::string& a, const std::string& k) { system.BindKey(a, k); });
  }
  const sol::object gamepadObj = spec.get<sol::object>("gamepad");
  if (gamepadObj.valid() && gamepadObj != sol::lua_nil) {
    system.UnbindActionGamepad(action);
    ApplyBindingList(gamepadObj, action,
                     [&system](const std::string& a, const std::string& b) { system.BindGamepad(a, b); });
  }
}

void ApplyBindingsTable(InputSystem& system, const sol::object& tblObj, const sol::optional<sol::table>& opts) {
  if (!tblObj.is<sol::table>()) {
#ifndef OCTARINE_SHIPPED
    Logger::Warn("input.set_bindings expects a table of { action = { keys = {...}, gamepad = {...} } }");
#endif
    return;
  }
  bool merge = false;
  if (opts) {
    if (const sol::object m = opts->get<sol::object>("merge"); m.is<bool>()) {
      merge = m.as<bool>();
    }
  }
  if (!merge) {
    system.ClearAllBindings();
  }
  const sol::table tbl = tblObj.as<sol::table>();
  for (const auto& [k, v] : tbl) {
    if (!k.is<std::string>()) continue;
    const std::string action = k.as<std::string>();
    if (!v.is<sol::table>()) {
#ifndef OCTARINE_SHIPPED
      Logger::Warn("input.set_bindings entry for '" + action + "' is not a table; skipping");
#endif
      continue;
    }
    ApplyActionSpec(system, action, v.as<sol::table>());
  }
}
void BindActionAndGamepadApi(sol::table& input, InputSystem& system) {
  input.set_function("bind",
                     [&system](const std::string& action, const std::string& key) { system.Bind(action, key); });
  input.set_function("unbind", [&system](const std::string& action, sol::optional<std::string> key) {
    if (key)
      system.Unbind(action, *key);
    else
      system.UnbindAction(action);
  });
  input.set_function("bind_key",
                     [&system](const std::string& action, const std::string& key) { system.BindKey(action, key); });
  input.set_function("unbind_key", [&system](const std::string& action, sol::optional<std::string> key) {
    if (key)
      system.UnbindKey(action, *key);
    else
      system.UnbindActionKeys(action);
  });
  input.set_function("bind_gamepad", [&system](const std::string& action, const std::string& button) {
    system.BindGamepad(action, button);
  });
  input.set_function("unbind_gamepad", [&system](const std::string& action, sol::optional<std::string> button) {
    if (button)
      system.UnbindGamepad(action, *button);
    else
      system.UnbindActionGamepad(action);
  });
  input.set_function("set_bindings", [&system](const sol::object& tbl, const sol::optional<sol::table>& opts) {
    ApplyBindingsTable(system, tbl, opts);
  });
  input.set_function("get_bindings", [&system](sol::optional<std::string> action, sol::this_state s) {
    sol::state_view luaView(s);
    if (action) return MakeActionBindingsTable(luaView, system, *action);
    sol::table all = luaView.create_table();
    for (const auto& name : system.GetBoundActions()) {
      all[name] = MakeActionBindingsTable(luaView, system, name);
    }
    return all;
  });
  input.set_function("is_action_down", [&system](const std::string& a) { return system.IsActionDown(a); });
  input.set_function("is_action_pressed", [&system](const std::string& a) { return system.IsActionPressed(a); });
  input.set_function("is_action_released", [&system](const std::string& a) { return system.IsActionReleased(a); });
}

void BindGamepadAndCallbackApi(sol::table& input, InputSystem& system) {
  input.set_function("is_gamepad_down", [&system](const std::string& btn) { return system.IsGamepadButtonDown(btn); });
  input.set_function("is_gamepad_pressed",
                     [&system](const std::string& btn) { return system.IsGamepadButtonPressed(btn); });
  input.set_function("is_gamepad_released",
                     [&system](const std::string& btn) { return system.IsGamepadButtonReleased(btn); });
  input.set_function("is_gamepad_connected", [&system]() { return system.IsGamepadConnected(); });
  input.set_function("gamepad_axis", [&system](const std::string& axis, const sol::optional<float> deadzone) {
    return system.GamepadAxis(axis, deadzone.value_or(0.15f));
  });
  input.set_function("gamepad_stick", [&system](const std::string& stick, const sol::optional<float> deadzone) {
    return system.GamepadStick(stick, deadzone.value_or(0.18f));
  });
  input.set_function("on_key_down", [&system](sol::protected_function fn) { system.AddOnKeyDown(std::move(fn)); });
  input.set_function("on_key_up", [&system](sol::protected_function fn) { system.AddOnKeyUp(std::move(fn)); });
  input.set_function("on_mouse_down", [&system](sol::protected_function fn) { system.AddOnMouseDown(std::move(fn)); });
  input.set_function("on_mouse_up", [&system](sol::protected_function fn) { system.AddOnMouseUp(std::move(fn)); });
  input.set_function("on_mouse_wheel",
                     [&system](sol::protected_function fn) { system.AddOnMouseWheel(std::move(fn)); });
  input.set_function("on_gamepad_down",
                     [&system](sol::protected_function fn) { system.AddOnGamepadDown(std::move(fn)); });
  input.set_function("on_gamepad_up", [&system](sol::protected_function fn) { system.AddOnGamepadUp(std::move(fn)); });
}
}  // namespace

void LuaSystemBinding<InputSystem>::bind(sol::state& lua, InputSystem& system) {
  lua["input"] = lua.create_table();
  sol::table input = lua["input"];

  input.set_function("is_key_down", [&system](const std::string& name) { return system.IsKeyDown(name); });
  input.set_function("is_key_pressed", [&system](const std::string& name) { return system.IsKeyPressed(name); });
  input.set_function("is_key_released", [&system](const std::string& name) { return system.IsKeyReleased(name); });
  input.set_function("is_mouse_down",
                     [&system](const sol::object& btn) { return system.IsMouseDown(ToMouseButton(btn)); });
  input.set_function("is_mouse_pressed",
                     [&system](const sol::object& btn) { return system.IsMousePressed(ToMouseButton(btn)); });
  input.set_function("is_mouse_released",
                     [&system](const sol::object& btn) { return system.IsMouseReleased(ToMouseButton(btn)); });
  input.set_function("is_hovered", [&system]() {
    return system.GetRegistry() ? system.GetRegistry()->Get<ViewportInfo>().isHovered : true;
  });
  input.set_function("is_focused", [&system]() {
    return system.GetRegistry() ? system.GetRegistry()->Get<ViewportInfo>().isFocused : true;
  });
  input.set_function("mouse_position", [&system]() { return glm::vec2(system.MouseX(), system.MouseY()); });
  input.set_function("mouse_wheel", [&system]() { return glm::vec2(system.WheelDx(), system.WheelDy()); });

  BindActionAndGamepadApi(input, system);
  BindGamepadAndCallbackApi(input, system);
}
