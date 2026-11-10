#include <SDL3/SDL.h>

#include <memory>
#include <sol/sol.hpp>
#include <string>

#include "Components/ViewportInfo.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Events/GamepadButtonEvent.h"
#include "Events/KeyInputEvent.h"
#include "Events/MouseInputEvent.h"
#include "Events/MouseWheelEvent.h"
#include "Game/GameConfig.h"
#include "Systems/InputSystem.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

void SetupRegistry(Registry& registry) {
  GameConfig config;
  config.windowWidth = 800;
  config.windowHeight = 600;
  registry.Set<GameConfig>(config);

  ViewportInfo viewport;
  viewport.isFocused = true;
  viewport.isHovered = true;
  viewport.width = 800.0f;
  viewport.height = 600.0f;
  registry.Set<ViewportInfo>(viewport);
}

void TestKeyStateTransitions() {
  auto eventBus = std::make_unique<EventBus>();
  Registry registry;
  SetupRegistry(registry);

  InputSystem inputSystem;
  inputSystem.SubscribeToEvents(eventBus, &registry);

  // Initial state: nothing down
  Check(!inputSystem.IsKeyDown("w"), "w not down initially");
  Check(!inputSystem.IsKeyPressed("w"), "w not pressed initially");
  Check(!inputSystem.IsKeyReleased("w"), "w not released initially");

  // Frame 1: Key Down 'w'
  eventBus->EmitEvent<KeyInputEvent>(SDLK_W, SDL_KMOD_NONE, true);
  Check(inputSystem.IsKeyDown("w"), "w is down on first frame");
  Check(inputSystem.IsKeyPressed("w"), "w is pressed (edge) on first frame");
  Check(!inputSystem.IsKeyReleased("w"), "w is not released on first frame");

  // Frame 2: End of frame clear
  inputSystem.ClearPerFrameInput();
  Check(inputSystem.IsKeyDown("w"), "w remains held/down across frames");
  Check(!inputSystem.IsKeyPressed("w"), "w is no longer pressed (edge cleared)");
  Check(!inputSystem.IsKeyReleased("w"), "w is not released");

  // Frame 3: Key Up 'w'
  eventBus->EmitEvent<KeyInputEvent>(SDLK_W, SDL_KMOD_NONE, false);
  Check(!inputSystem.IsKeyDown("w"), "w is no longer down after release");
  Check(!inputSystem.IsKeyPressed("w"), "w is not pressed after release");
  Check(inputSystem.IsKeyReleased("w"), "w is released (edge) on release frame");

  // Frame 4: End of frame clear
  inputSystem.ClearPerFrameInput();
  Check(!inputSystem.IsKeyDown("w"), "w is not down");
  Check(!inputSystem.IsKeyPressed("w"), "w is not pressed");
  Check(!inputSystem.IsKeyReleased("w"), "w release edge cleared");
}

void TestKeyAliasesAndCaseInsensitivity() {
  auto eventBus = std::make_unique<EventBus>();
  Registry registry;
  SetupRegistry(registry);

  InputSystem inputSystem;
  inputSystem.SubscribeToEvents(eventBus, &registry);

  // Press "left ctrl" -> "ctrl" alias should match
  eventBus->EmitEvent<KeyInputEvent>(SDLK_LCTRL, SDL_KMOD_NONE, true);
  Check(inputSystem.IsKeyDown("ctrl"), "ctrl alias matches left ctrl");
  Check(inputSystem.IsKeyDown("LEFT CTRL"), "uppercase input matches");
  Check(inputSystem.IsKeyDown("Left Ctrl"), "mixed case input matches");
  Check(inputSystem.IsKeyPressed("ctrl"), "ctrl pressed alias matches");

  // Press "return" -> "enter" alias matches
  eventBus->EmitEvent<KeyInputEvent>(SDLK_RETURN, SDL_KMOD_NONE, true);
  Check(inputSystem.IsKeyDown("enter"), "enter alias matches return");
  Check(inputSystem.IsKeyDown("return"), "return matches itself");

  // Press "escape" -> "esc" alias matches
  eventBus->EmitEvent<KeyInputEvent>(SDLK_ESCAPE, SDL_KMOD_NONE, true);
  Check(inputSystem.IsKeyDown("esc"), "esc alias matches escape");
}

void TestActionMapping() {
  auto eventBus = std::make_unique<EventBus>();
  Registry registry;
  SetupRegistry(registry);

  InputSystem inputSystem;
  inputSystem.SubscribeToEvents(eventBus, &registry);

  inputSystem.Bind("jump", "space");
  inputSystem.Bind("jump", "w");

  Check(!inputSystem.IsActionDown("jump"), "jump action not down initially");
  Check(!inputSystem.IsActionPressed("jump"), "jump action not pressed initially");

  // Press space -> jump active
  eventBus->EmitEvent<KeyInputEvent>(SDLK_SPACE, SDL_KMOD_NONE, true);
  Check(inputSystem.IsActionDown("jump"), "jump action is down via space");
  Check(inputSystem.IsActionPressed("jump"), "jump action is pressed via space");

  inputSystem.ClearPerFrameInput();
  Check(inputSystem.IsActionDown("jump"), "jump action remains held");
  Check(!inputSystem.IsActionPressed("jump"), "jump action edge cleared");

  // Release space -> jump inactive
  eventBus->EmitEvent<KeyInputEvent>(SDLK_SPACE, SDL_KMOD_NONE, false);
  Check(inputSystem.IsActionReleased("jump"), "jump action released via space");

  // Unbind 'w' from 'jump'
  inputSystem.Unbind("jump", "w");
  eventBus->EmitEvent<KeyInputEvent>(SDLK_W, SDL_KMOD_NONE, true);
  Check(!inputSystem.IsActionDown("jump"), "w does not trigger jump after unbind");

  // Unbind entire action
  inputSystem.UnbindAction("jump");
  eventBus->EmitEvent<KeyInputEvent>(SDLK_SPACE, SDL_KMOD_NONE, true);
  Check(!inputSystem.IsActionDown("jump"), "space does not trigger jump after UnbindAction");
}

void TestMouseAndWheelInput() {
  auto eventBus = std::make_unique<EventBus>();
  Registry registry;
  SetupRegistry(registry);

  InputSystem inputSystem;
  inputSystem.SubscribeToEvents(eventBus, &registry);

  // Mouse button 1 (left click) down
  SDL_MouseButtonEvent btnDown{};
  btnDown.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  btnDown.button = SDL_BUTTON_LEFT;
  btnDown.x = 100.0f;
  btnDown.y = 100.0f;
  eventBus->EmitEvent<MouseInputEvent>(btnDown);

  Check(inputSystem.IsMouseDown(SDL_BUTTON_LEFT), "mouse left is down");
  Check(inputSystem.IsMousePressed(SDL_BUTTON_LEFT), "mouse left is pressed");

  inputSystem.ClearPerFrameInput();
  Check(inputSystem.IsMouseDown(SDL_BUTTON_LEFT), "mouse left remains held");
  Check(!inputSystem.IsMousePressed(SDL_BUTTON_LEFT), "mouse left pressed edge cleared");

  // Mouse button 1 up
  SDL_MouseButtonEvent btnUp{};
  btnUp.type = SDL_EVENT_MOUSE_BUTTON_UP;
  btnUp.button = SDL_BUTTON_LEFT;
  btnUp.x = 100.0f;
  btnUp.y = 100.0f;
  eventBus->EmitEvent<MouseInputEvent>(btnUp);

  Check(!inputSystem.IsMouseDown(SDL_BUTTON_LEFT), "mouse left is no longer down");
  Check(inputSystem.IsMouseReleased(SDL_BUTTON_LEFT), "mouse left is released");

  // Mouse wheel
  eventBus->EmitEvent<MouseWheelEvent>(2.5f, -1.0f);
  CheckEq(inputSystem.WheelDx(), 2.5f, "WheelDx accumulated");
  CheckEq(inputSystem.WheelDy(), -1.0f, "WheelDy accumulated");

  inputSystem.ClearPerFrameInput();
  CheckEq(inputSystem.WheelDx(), 0.0f, "WheelDx cleared per frame");
  CheckEq(inputSystem.WheelDy(), 0.0f, "WheelDy cleared per frame");
}

void TestLuaScriptCallbacks() {
  sol::state lua;
  lua.open_libraries(sol::lib::base);

  auto eventBus = std::make_unique<EventBus>();
  Registry registry;
  SetupRegistry(registry);

  InputSystem inputSystem;
  inputSystem.SubscribeToEvents(eventBus, &registry);

  lua.script(R"(
    key_down_log = ""
    key_up_log = ""
    function on_key_down(key)
      key_down_log = key
    end
    function on_key_up(key)
      key_up_log = key
    end
  )");

  inputSystem.AddOnKeyDown(lua["on_key_down"]);
  inputSystem.AddOnKeyUp(lua["on_key_up"]);

  eventBus->EmitEvent<KeyInputEvent>(SDLK_ESCAPE, SDL_KMOD_NONE, true);
  CheckEq(lua["key_down_log"].get<std::string>(), "escape", "on_key_down Lua callback received key");

  eventBus->EmitEvent<KeyInputEvent>(SDLK_ESCAPE, SDL_KMOD_NONE, false);
  CheckEq(lua["key_up_log"].get<std::string>(), "escape", "on_key_up Lua callback received key");

  inputSystem.ResetLuaState();
}

}  // namespace

int main() {
  TestKeyStateTransitions();
  TestKeyAliasesAndCaseInsensitivity();
  TestActionMapping();
  TestMouseAndWheelInput();
  TestLuaScriptCallbacks();
  return octarine::test::ReportSummary("OctarineInputSystemTest");
}
