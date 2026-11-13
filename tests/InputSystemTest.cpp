#include <SDL3/SDL.h>

#include <cmath>
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
#include "Lua/Bindings/InputSystemLuaBinding.h"
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

void TestGamepadAnalogSticksAndTriggerHysteresis() {
  auto eventBus = std::make_unique<EventBus>();
  Registry registry;
  SetupRegistry(registry);

  InputSystem inputSystem;
  inputSystem.SubscribeToEvents(eventBus, &registry);
  inputSystem.Bind("raise", "rt");

  // Deadzone zeroing on left stick
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX, 0.10f);
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTY, 0.10f);
  const glm::vec2 dead = inputSystem.GamepadStick("left", 0.18f);
  CheckEq(dead.x, 0.0f, "Left stick X inside radial deadzone is 0");
  CheckEq(dead.y, 0.0f, "Left stick Y inside radial deadzone is 0");

  // Mid-deflection scales smoothly between deadzone and 1.0
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX, 0.59f);
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTY, 0.0f);
  const glm::vec2 mid = inputSystem.GamepadStick("left", 0.18f);
  Check(std::abs(mid.x - 0.5f) < 0.01f && std::abs(mid.y) < 0.001f,
        "Left stick half-range above 0.18 deadzone rescales to ~0.5");

  // Full diagonal clamps magnitude to <= 1.0
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX, 1.0f);
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTY, -1.0f);
  const glm::vec2 diag = inputSystem.GamepadStick("left", 0.18f);
  Check(glm::length(diag) <= 1.0001f && glm::length(diag) > 0.99f, "Radial diagonal clamps to length 1.0");

  // Axis aliases
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_RIGHTY, -0.60f);
  Check(std::abs(inputSystem.GamepadAxis("ry", 0.15f)) > 0.5f, "GamepadAxis supports 'ry' alias");
  Check(inputSystem.GamepadAxis("left_x", 0.15f) > 0.9f, "GamepadAxis supports 'left_x' alias");

  // Trigger hysteresis: press >= 0.45, hold >= 0.35, release < 0.35
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.50f);
  Check(inputSystem.IsGamepadButtonPressed("rt"), "RT pressed at 0.50");
  Check(inputSystem.IsGamepadButtonDown("r2"), "r2 alias matches RT held");
  Check(inputSystem.IsActionPressed("raise"), "Bound action 'raise' pressed via RT");

  inputSystem.ClearPerFrameInput();
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.40f);
  Check(inputSystem.IsGamepadButtonDown("rt"), "RT remains down at 0.40 due to hysteresis");
  Check(!inputSystem.IsGamepadButtonPressed("rt"), "RT does not re-fire pressed edge at 0.40");

  inputSystem.ClearPerFrameInput();
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.20f);
  Check(!inputSystem.IsGamepadButtonDown("rt"), "RT releases below 0.35 threshold");
  Check(inputSystem.IsGamepadButtonReleased("rt"), "RT released edge fires below 0.35");
}

void TestActionBindingKeyVsGamepadDisambiguation() {
  auto eventBus = std::make_unique<EventBus>();
  Registry registry;
  SetupRegistry(registry);

  InputSystem inputSystem;
  inputSystem.SubscribeToEvents(eventBus, &registry);

  inputSystem.Bind("move_left", "a");
  inputSystem.Bind("banish", "b");
  inputSystem.Bind("raise", "south");
  inputSystem.Bind("pad_a_only", "gamepad:a");
  inputSystem.Bind("key_up_only", "key:up");

  eventBus->EmitEvent<GamepadButtonEvent>(0, SDL_GAMEPAD_BUTTON_SOUTH, "a", true);
  eventBus->EmitEvent<GamepadButtonEvent>(0, SDL_GAMEPAD_BUTTON_EAST, "b", true);
  eventBus->EmitEvent<GamepadButtonEvent>(0, SDL_GAMEPAD_BUTTON_DPAD_UP, "dpup", true);

  Check(!inputSystem.IsActionDown("move_left"), "Gamepad 'a' must not trigger keyboard 'a' action move_left");
  Check(!inputSystem.IsActionPressed("move_left"), "Gamepad 'a' must not press keyboard 'a' action move_left");
  Check(!inputSystem.IsActionBoundToGamepadButton("move_left", "a"),
        "Unprefixed single-letter 'a' binding must not match gamepad 'a'");
  Check(!inputSystem.IsActionDown("banish"), "Gamepad 'b' must not trigger keyboard 'b' action banish");
  Check(!inputSystem.IsActionDown("key_up_only"), "Gamepad dpup must not trigger key:up binding");

  Check(inputSystem.IsGamepadButtonDown("a"), "Raw IsGamepadButtonDown('a') still works");
  Check(inputSystem.IsGamepadButtonDown("south"), "Raw IsGamepadButtonDown('south') still works");
  Check(inputSystem.IsActionDown("raise"), "Explicit gamepad alias 'south' still triggers raise");
  Check(inputSystem.IsActionDown("pad_a_only"), "Explicit gamepad:a prefix triggers on gamepad A");

  inputSystem.ClearPerFrameInput();
  eventBus->EmitEvent<GamepadButtonEvent>(0, SDL_GAMEPAD_BUTTON_SOUTH, "a", false);
  eventBus->EmitEvent<GamepadButtonEvent>(0, SDL_GAMEPAD_BUTTON_EAST, "b", false);
  eventBus->EmitEvent<GamepadButtonEvent>(0, SDL_GAMEPAD_BUTTON_DPAD_UP, "dpup", false);
  eventBus->EmitEvent<KeyInputEvent>(SDLK_A, SDL_KMOD_NONE, true);

  Check(inputSystem.IsActionDown("move_left"), "Keyboard A still triggers move_left");
  Check(!inputSystem.IsActionDown("pad_a_only"), "Keyboard A must not trigger gamepad:a binding");
}

void TestExplicitDeviceBindingsAndQuery() {
  auto eventBus = std::make_unique<EventBus>();
  Registry registry;
  SetupRegistry(registry);

  InputSystem inputSystem;
  inputSystem.SubscribeToEvents(eventBus, &registry);

  inputSystem.BindKey("move_left", "a");
  inputSystem.BindGamepad("confirm", "a");
  inputSystem.BindKey("raise", "space");
  inputSystem.BindGamepad("raise", "south");
  inputSystem.BindGamepad("raise", "rt");

  Check(inputSystem.GetActionKeyBindings("raise") == std::vector<std::string>{"space"},
        "GetActionKeyBindings('raise') returns {'space'}");
  Check(inputSystem.GetActionGamepadBindings("raise") == std::vector<std::string>{"south", "rt"},
        "GetActionGamepadBindings('raise') returns {'south', 'rt'}");
  Check(inputSystem.GetActionKeyBindings("confirm").empty(), "GetActionKeyBindings('confirm') is empty");
  Check(inputSystem.GetActionGamepadBindings("confirm") == std::vector<std::string>{"south"},
        "BindGamepad('confirm', 'a') canonicalizes to {'south'}");

  inputSystem.Bind("legacy_pad", "south");
  inputSystem.Bind("legacy_key", "space");
  Check(inputSystem.GetActionKeyBindings("legacy_pad").empty(),
        "Unprefixed 'south' excluded from GetActionKeyBindings");
  Check(inputSystem.GetActionGamepadBindings("legacy_pad") == std::vector<std::string>{"south"},
        "Unprefixed 'south' appears in GetActionGamepadBindings");
  Check(inputSystem.GetActionKeyBindings("legacy_key") == std::vector<std::string>{"space"},
        "Unprefixed 'space' appears in GetActionKeyBindings");
  Check(inputSystem.GetActionGamepadBindings("legacy_key").empty(),
        "Unprefixed 'space' excluded from GetActionGamepadBindings");

  eventBus->EmitEvent<GamepadButtonEvent>(0, SDL_GAMEPAD_BUTTON_SOUTH, "a", true);
  Check(!inputSystem.IsActionDown("move_left"), "Gamepad 'a' does not trigger BindKey('move_left', 'a')");
  Check(inputSystem.IsActionDown("confirm"), "Gamepad 'a' triggers BindGamepad('confirm', 'a')");
  Check(inputSystem.IsActionDown("raise"), "Gamepad 'a' triggers BindGamepad('raise', 'south')");

  inputSystem.UnbindGamepad("raise", "rt");
  Check(inputSystem.GetActionGamepadBindings("raise") == std::vector<std::string>{"south"},
        "UnbindGamepad removes only 'rt'");
  Check(inputSystem.GetActionKeyBindings("raise") == std::vector<std::string>{"space"},
        "UnbindGamepad preserves key bindings");

  inputSystem.UnbindActionGamepad("raise");
  Check(inputSystem.GetActionGamepadBindings("raise").empty(), "UnbindActionGamepad clears gamepad bindings");
  Check(inputSystem.GetActionKeyBindings("raise") == std::vector<std::string>{"space"},
        "UnbindActionGamepad preserves key bindings");

  inputSystem.UnbindActionKeys("raise");
  Check(inputSystem.GetActionKeyBindings("raise").empty(), "UnbindActionKeys clears key bindings");
  const auto remaining = inputSystem.GetBoundActions();
  Check(std::find(remaining.begin(), remaining.end(), "raise") == remaining.end(),
        "Empty action 'raise' removed from GetBoundActions");

  sol::state lua;
  lua.open_libraries(sol::lib::base);
  LuaSystemBinding<InputSystem>::bind(lua, inputSystem);
  const auto res = lua.safe_script(R"(
    input.bind_key("act", "k")
    input.bind_gamepad("act", "west")
    local single = input.get_bindings("act")
    assert(single and #single.keys == 1 and single.keys[1] == "k")
    assert(#single.gamepad == 1 and single.gamepad[1] == "west")
    local all = input.get_bindings()
    assert(all.act and all.act.gamepad[1] == "west")
    input.unbind_gamepad("act", "west")
    local after_gp = input.get_bindings("act")
    assert(#after_gp.gamepad == 0 and after_gp.keys[1] == "k")
    input.unbind_key("act")
    local after_key = input.get_bindings("act")
    assert(#after_key.keys == 0 and #after_key.gamepad == 0)
  )");
  Check(res.valid(), "Lua input.bind_key/bind_gamepad/unbind_key/unbind_gamepad/get_bindings work");
}

void TestCanonicalGamepadNamesSetBindingsAndFocusGuard() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table);

  auto eventBus = std::make_unique<EventBus>();
  Registry registry;
  SetupRegistry(registry);

  InputSystem inputSystem;
  inputSystem.SubscribeToEvents(eventBus, &registry);
  LuaSystemBinding<InputSystem>::bind(lua, inputSystem);

  // 1. Canonicalization across bind_gamepad, unbind_gamepad aliases, get_bindings, and callbacks
  const auto res = lua.safe_script(R"(
    pad_down_log = {}
    pad_up_log = {}
    input.on_gamepad_down(function(btn) table.insert(pad_down_log, btn) end)
    input.on_gamepad_up(function(btn) table.insert(pad_up_log, btn) end)

    input.bind("legacy", "space")
    input.set_bindings({
      raise = { keys = { "space", "esc" }, gamepad = { "a", "r2" } },
      nav_up = { keys = { "w", "up" }, gamepad = { "dpup" } },
    })
    assert(input.get_bindings("legacy").keys[1] == nil, "set_bindings replaces previous bindings by default")
    local raise = input.get_bindings("raise")
    assert(raise.keys[1] == "space" and raise.keys[2] == "escape", "key aliases canonicalize cleanly")
    assert(raise.gamepad[1] == "south" and raise.gamepad[2] == "rt", "gamepad aliases canonicalize cleanly")
    assert(input.get_bindings("nav_up").gamepad[1] == "dpad_up", "dpup canonicalizes to dpad_up")

    -- Merge mode preserves unrelated actions and supports partial device override
    input.set_bindings({
      raise = { gamepad = { "cross", "leftshoulder" } },
      pause = { keys = { "p" }, gamepad = { "menu" } },
    }, { merge = true })
    local merged_raise = input.get_bindings("raise")
    assert(merged_raise.keys[1] == "space" and merged_raise.keys[2] == "escape", "merge keeps keys when omitted")
    assert(merged_raise.gamepad[1] == "south" and merged_raise.gamepad[2] == "lb", "merge replaces gamepad when given")
    assert(input.get_bindings("pause").gamepad[1] == "start", "menu canonicalizes to start")
    assert(input.get_bindings("nav_up").keys[1] == "w", "merge preserves other actions")

    -- Alias-agnostic unbind_gamepad
    input.unbind_gamepad("raise", "a")
    assert(#input.get_bindings("raise").gamepad == 1 and input.get_bindings("raise").gamepad[1] == "lb")
  )");
  Check(res.valid(), "input.set_bindings + canonicalization + alias-agnostic unbind_gamepad work");

  eventBus->EmitEvent<GamepadButtonEvent>(0, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "leftshoulder", true);
  eventBus->EmitEvent<GamepadButtonEvent>(0, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "leftshoulder", false);
  CheckEq(lua["pad_down_log"][1].get<std::string>(), std::string("lb"), "on_gamepad_down receives canonical 'lb'");
  CheckEq(lua["pad_up_log"][1].get<std::string>(), std::string("lb"), "on_gamepad_up receives canonical 'lb'");

  // 2. Unfocused trigger guard prevents triggerDigitalDown_ / heldGamepadButtons_ desync
  registry.Get<ViewportInfo>().isFocused = false;
  inputSystem.ClearPerFrameInput();
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.70f);
  Check(!inputSystem.IsGamepadButtonDown("rt"), "Unfocused trigger press does not latch held RT");
  registry.Get<ViewportInfo>().isFocused = true;
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.70f);
  Check(inputSystem.IsGamepadButtonPressed("rt"), "Focused trigger press latches RT without stale trigger state");
  registry.Get<ViewportInfo>().isFocused = false;
  inputSystem.SetGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.05f);
  registry.Get<ViewportInfo>().isFocused = true;
  Check(!inputSystem.IsGamepadButtonDown("rt"), "Trigger release while unfocused cleanly clears RT");
  inputSystem.ResetLuaState();
}

void TestUnbindBranchesAndLuaCoverage() {
  // Exercise SetGamepadAxis without EventBus (direct OnGamepadButton fallback) and gamepad lifecycle no-ops
  {
    InputSystem standalone;
    standalone.OnGamepadAdded(0);
    standalone.NoteGamepadActivity(0);
    standalone.OnGamepadRemoved(0);
    Check(!standalone.IsGamepadConnected(), "IsGamepadConnected is false with no gamepads");
    standalone.SetGamepadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.8f);
    Check(standalone.IsGamepadButtonDown("lt"), "Direct SetGamepadAxis triggers lt without EventBus");
    Check(standalone.GamepadAxis("lt", 0.1f) > 0.5f, "GamepadAxis supports 'lt'");
    CheckEq(standalone.GamepadAxis("rt", 0.1f), 0.0f, "GamepadAxis supports 'rt'");
    CheckEq(standalone.GamepadAxis("unknown_axis", 0.1f), 0.0f, "GamepadAxis returns 0 on unknown axis");
    CheckEq(standalone.GamepadStick("unknown_stick", 0.1f).x, 0.0f, "GamepadStick returns 0 on unknown stick");
    standalone.BindGamepad("trimmed", "  gamepad:a  ");
    Check(standalone.GetActionGamepadBindings("trimmed") == std::vector<std::string>{"south"},
          "BindGamepad trims whitespace around gamepad:a");
  }

  auto eventBus = std::make_unique<EventBus>();
  Registry registry;
  SetupRegistry(registry);

  InputSystem inputSystem;
  inputSystem.SubscribeToEvents(eventBus, &registry);

  // UnbindKey / UnbindActionKeys / UnbindGamepad / UnbindActionGamepad branch coverage
  inputSystem.BindKey("combo", "a");
  inputSystem.BindKey("combo", "b");
  inputSystem.BindGamepad("combo", "south");
  inputSystem.UnbindKey("combo", "a");
  Check(inputSystem.GetActionKeyBindings("combo") == std::vector<std::string>{"b"},
        "UnbindKey removes only matching key and preserves other key + gamepad");
  inputSystem.UnbindActionKeys("combo");
  Check(inputSystem.GetActionKeyBindings("combo").empty(), "UnbindActionKeys clears keys");
  Check(inputSystem.GetActionGamepadBindings("combo") == std::vector<std::string>{"south"},
        "UnbindActionKeys preserves gamepad bindings");
  inputSystem.UnbindActionGamepad("combo");
  Check(inputSystem.GetActionGamepadBindings("combo").empty(),
        "UnbindActionGamepad removes action when no key bindings remain");

  inputSystem.BindKey("solo_key", "k");
  inputSystem.UnbindKey("solo_key", "k");
  Check(inputSystem.GetActionKeyBindings("solo_key").empty(), "UnbindKey erases action when last binding removed");

  inputSystem.BindKey("mix", "m");
  inputSystem.BindGamepad("mix", "south");
  inputSystem.BindGamepad("mix", "east");
  inputSystem.UnbindGamepad("mix", "south");
  Check(inputSystem.GetActionGamepadBindings("mix") == std::vector<std::string>{"east"},
        "UnbindGamepad removes only matching gamepad button and preserves key");
  inputSystem.UnbindGamepad("mix", "unknown_pad_btn");
  inputSystem.BindGamepad("mix", "unknown_pad_btn");
  inputSystem.UnbindActionKeys("mix");
  inputSystem.UnbindGamepad("mix", "east");
  inputSystem.UnbindGamepad("mix", "unknown_pad_btn");
  Check(inputSystem.GetActionGamepadBindings("mix").empty(),
        "UnbindGamepad erases action when last gamepad binding removed");

  // Lua binding edge cases and remaining API surface
  sol::state lua;
  lua.open_libraries(sol::lib::base);
  LuaSystemBinding<InputSystem>::bind(lua, inputSystem);
  const auto res = lua.safe_script(R"(
    input.set_bindings("not_a_table")
    input.set_bindings({
      bad_entry = 123,
      good_entry = { keys = "space", gamepad = "south" },
    })
    assert(input.get_bindings("good_entry").keys[1] == "space")
    assert(input.get_bindings("good_entry").gamepad[1] == "south")

    input.bind("temp", "z")
    input.unbind("temp", "z")
    input.bind("temp2", "x")
    input.unbind("temp2")
    input.bind_key("temp3", "c")
    input.unbind_key("temp3", "c")
    input.bind_gamepad("temp4", "north")
    input.unbind_gamepad("temp4")

    local _ = input.is_gamepad_connected()
    local _ = input.is_hovered()
    local _ = input.is_focused()
    local _ = input.is_gamepad_down("south")
    local _ = input.is_gamepad_pressed("south")
    local _ = input.is_gamepad_released("south")
    local ax = input.gamepad_axis("leftx", 0.15)
    assert(ax == 0.0)
    local st = input.gamepad_stick("left", 0.18)
    assert(st ~= nil)
  )");
  Check(res.valid(), "Lua input unbind, set_bindings edge cases, and gamepad query wrappers work");
  inputSystem.ResetLuaState();
}

}  // namespace

int main() {
  TestKeyStateTransitions();
  TestKeyAliasesAndCaseInsensitivity();
  TestActionMapping();
  TestMouseAndWheelInput();
  TestLuaScriptCallbacks();
  TestGamepadAnalogSticksAndTriggerHysteresis();
  TestActionBindingKeyVsGamepadDisambiguation();
  TestExplicitDeviceBindingsAndQuery();
  TestCanonicalGamepadNamesSetBindingsAndFocusGuard();
  TestUnbindBranchesAndLuaCoverage();
  return octarine::test::ReportSummary("OctarineInputSystemTest");
}
