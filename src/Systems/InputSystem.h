#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <glm/glm.hpp>
#include <memory>
#include <sol/sol.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Events/GamepadButtonEvent.h"
#include "Events/KeyInputEvent.h"
#include "Events/MouseInputEvent.h"
#include "Events/MouseWheelEvent.h"
#include "General/Logger.h"

// Per-frame input state machine: held/pressed/released sets for keys + mouse buttons, accumulated
// mouse wheel delta, action map (named action → list of keys), and the script-installed
// callback vectors that fire on each edge. EventBus subscriptions feed OnKeyInput / OnMouseInput
// / OnMouseWheel; the Lua `input.*` surface (is_key_*, on_key_*, bind, etc.) lives in
// `Lua/Bindings/InputSystemLuaBinding.cpp` — InputSystem just exposes the public accessors +
// mutators that binding's lambdas call into.
class InputSystem {
 public:
  InputSystem() : registry_(nullptr) {
    keyAliases_["ctrl"] = {"left ctrl", "right ctrl", "ctrl"};
    keyAliases_["shift"] = {"left shift", "right shift", "shift"};
    keyAliases_["alt"] = {"left alt", "right alt", "alt"};
    keyAliases_["enter"] = {"return", "enter", "keypad enter"};
    keyAliases_["return"] = {"return", "enter", "keypad enter"};
    keyAliases_["esc"] = {"escape", "esc"};
    keyAliases_["escape"] = {"escape", "esc"};
    keyAliases_["grave"] = {"`", "grave"};
    keyAliases_["`"] = {"`", "grave"};
    InitGamepadAliases();
  }

  static std::string MakeKey(const std::string& key) {
    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
  }

  void SubscribeToEvents(const std::unique_ptr<EventBus>& eventBus, Registry* registry) {
    registry_ = registry;
    eventBus_ = eventBus.get();
    subscriptions_.clear();
    subscriptions_.push_back(eventBus->SubscribeEvent<InputSystem, KeyInputEvent>(this, &InputSystem::OnKeyInput));
    subscriptions_.push_back(eventBus->SubscribeEvent<InputSystem, MouseInputEvent>(this, &InputSystem::OnMouseInput));
    subscriptions_.push_back(eventBus->SubscribeEvent<InputSystem, MouseWheelEvent>(this, &InputSystem::OnMouseWheel));
    subscriptions_.push_back(
        eventBus->SubscribeEvent<InputSystem, GamepadButtonEvent>(this, &InputSystem::OnGamepadButton));
  }

  // Called near the top of Game::Update so polling APIs see a fresh cursor position
  // for systems that run this frame. Reads viewport + window dims to map raw SDL coords
  // to game-space (letterboxing and viewport scaling would alias raw coords).
  void BeginFrame();

  // Called at the end of Game::Update — pressed/released are per-frame edges, wheel
  // delta is a per-frame accumulator.
  void ClearPerFrameInput() {
    pressedKeys_.clear();
    releasedKeys_.clear();
    pressedMouseButtons_.clear();
    releasedMouseButtons_.clear();
    pressedGamepadButtons_.clear();
    releasedGamepadButtons_.clear();
    wheelDx_ = 0.0f;
    wheelDy_ = 0.0f;
  }

  // Called by Game on scene unload to drop callbacks/bindings owned by the previous scene's
  // scripts. Held state is also cleared so a tap during a transition doesn't stick.
  void ResetLuaState() {
    onKeyDown_.clear();
    onKeyUp_.clear();
    onMouseDown_.clear();
    onMouseUp_.clear();
    onMouseWheel_.clear();
    onGamepadDown_.clear();
    onGamepadUp_.clear();
    pressedKeys_.clear();
    releasedKeys_.clear();
    heldKeys_.clear();
    pressedMouseButtons_.clear();
    releasedMouseButtons_.clear();
    heldMouseButtons_.clear();
    pressedGamepadButtons_.clear();
    releasedGamepadButtons_.clear();
    heldGamepadButtons_.clear();
    gamepadAxes_.fill(0.0f);
    triggerDigitalDown_.fill(false);
    wheelDx_ = 0.0f;
    wheelDy_ = 0.0f;
  }

  void ClearAllBindings() { actions_.clear(); }

  void SyncConnectedGamepads();
  void OnGamepadAdded(SDL_JoystickID id, SDL_Gamepad* pad = nullptr);
  void OnGamepadRemoved(SDL_JoystickID id);
  void NoteGamepadActivity(SDL_JoystickID id);

  [[nodiscard]] static std::string_view LookupCanonicalGamepadButton(std::string_view lower) noexcept;
  [[nodiscard]] static std::string CanonicalizeGamepadButton(std::string_view raw);
  [[nodiscard]] static std::string CanonicalizeKeyName(std::string_view raw);

#ifndef OCTARINE_SHIPPED
  // Diagnostic logging for input events in dev builds. Gated behind EngineOptions::logInputEvents.
  [[nodiscard]] bool ShouldLogInput() const;
#endif

  // Accessors used by Lua/Bindings/InputSystemLuaBinding.cpp. Plain getters / mutators so the
  // binding lambdas can read state and append callbacks without poking at internals directly.
  [[nodiscard]] Registry* GetRegistry() const { return registry_; }
  [[nodiscard]] float MouseX() const { return mouseX_; }
  [[nodiscard]] float MouseY() const { return mouseY_; }
  [[nodiscard]] float WheelDx() const { return wheelDx_; }
  [[nodiscard]] float WheelDy() const { return wheelDy_; }

  [[nodiscard]] bool IsKeyDown(const std::string& key) const;
  [[nodiscard]] bool IsKeyPressed(const std::string& key) const;
  [[nodiscard]] bool IsKeyReleased(const std::string& key) const;
  [[nodiscard]] bool IsMouseDown(int btn) const;
  [[nodiscard]] bool IsMousePressed(int btn) const;
  [[nodiscard]] bool IsMouseReleased(int btn) const;
  [[nodiscard]] bool IsActionDown(const std::string& action) const;
  [[nodiscard]] bool IsActionPressed(const std::string& action) const;
  [[nodiscard]] bool IsActionReleased(const std::string& action) const;

  [[nodiscard]] bool IsGamepadButtonDown(const std::string& button) const;
  [[nodiscard]] bool IsGamepadButtonPressed(const std::string& button) const;
  [[nodiscard]] bool IsGamepadButtonReleased(const std::string& button) const;
  [[nodiscard]] bool IsGamepadConnected() const;
  void SetGamepadAxis(SDL_GamepadAxis axis, float normalizedValue);
  [[nodiscard]] float GamepadAxis(const std::string& axisName, float deadzone = 0.15f) const;
  [[nodiscard]] glm::vec2 GamepadStick(const std::string& stickName, float deadzone = 0.18f) const;

  [[nodiscard]] bool MatchesGamepadAlias(const std::string& requested, const std::string& actual) const;
  [[nodiscard]] bool MatchesKeyAlias(const std::string& requested, const std::string& actual) const;
  [[nodiscard]] bool IsActionBoundToKey(const std::string& action, const std::string& key) const;
  [[nodiscard]] bool IsActionBoundToGamepadButton(const std::string& action, const std::string& button) const;
  [[nodiscard]] bool IsActionBoundToInput(const std::string& action, const std::string& input) const;

  void Bind(const std::string& action, const std::string& key);
  void Unbind(const std::string& action, const std::string& key);
  // Clear all bindings for an action; the Lua `input.unbind(action)` form (no key arg) maps to this.
  void UnbindAction(const std::string& action) { actions_.erase(MakeKey(action)); }

  void BindKey(const std::string& action, const std::string& key);
  void UnbindKey(const std::string& action, const std::string& key);
  void UnbindActionKeys(const std::string& action);
  void BindGamepad(const std::string& action, const std::string& button);
  void UnbindGamepad(const std::string& action, const std::string& button);
  void UnbindActionGamepad(const std::string& action);
  [[nodiscard]] std::vector<std::string> GetActionKeyBindings(const std::string& action) const;
  [[nodiscard]] std::vector<std::string> GetActionGamepadBindings(const std::string& action) const;
  [[nodiscard]] std::vector<std::string> GetBoundActions() const;

  void AddOnKeyDown(sol::protected_function fn) { onKeyDown_.push_back(std::move(fn)); }
  void AddOnKeyUp(sol::protected_function fn) { onKeyUp_.push_back(std::move(fn)); }
  void AddOnMouseDown(sol::protected_function fn) { onMouseDown_.push_back(std::move(fn)); }
  void AddOnMouseUp(sol::protected_function fn) { onMouseUp_.push_back(std::move(fn)); }
  void AddOnMouseWheel(sol::protected_function fn) { onMouseWheel_.push_back(std::move(fn)); }
  void AddOnGamepadDown(sol::protected_function fn) { onGamepadDown_.push_back(std::move(fn)); }
  void AddOnGamepadUp(sol::protected_function fn) { onGamepadUp_.push_back(std::move(fn)); }

 private:
  void OnKeyInput(const KeyInputEvent& event);
  void OnMouseInput(const MouseInputEvent& event);
  void OnMouseWheel(const MouseWheelEvent& event);
  void OnGamepadButton(const GamepadButtonEvent& event);
  void InitGamepadAliases();
  void PollGamepadAxes();
  void UpdateTriggerDigitalState(SDL_GamepadAxis axis, float normalizedValue);
  void ApplyGamepadButtonState(const std::string& btn, bool isPressed);
  [[nodiscard]] bool IsViewportFocused() const;

  template <typename... Args>
  static void Dispatch(const std::vector<sol::protected_function>& fns, const Args&... args) {
    for (const auto& fn : fns) {
      if (fn == sol::lua_nil) continue;
      auto result = fn(args...);
      if (!result.valid()) {
        const sol::error err = result;
        Logger::ErrorLua(std::string(err.what()));
      }
    }
  }

  [[nodiscard]] bool MatchesSet(const std::string& key, const std::unordered_set<std::string>& set) const;
  [[nodiscard]] bool MatchesGamepadSet(const std::string& button, const std::unordered_set<std::string>& set) const;
  [[nodiscard]] std::string_view ResolveKeyToken(std::string_view token) const;
  [[nodiscard]] std::string_view ResolveGamepadToken(std::string_view token) const;
  [[nodiscard]] SDL_Gamepad* FindActiveGamepad() const noexcept;
#ifndef OCTARINE_SHIPPED
  void WarnUnknownToken(std::string_view kind, std::string_view token) const;
#endif

  template <typename Pred>
  bool AnyBoundKey(const std::string& action, Pred&& pred) const {
    const auto it = actions_.find(MakeKey(action));
    if (it == actions_.end()) return false;
    return std::ranges::any_of(it->second, pred);
  }

  Registry* registry_;
  EventBus* eventBus_ = nullptr;
  // One handle per SubscribeToEvents subscription; their destructors drop the bus callbacks when
  // this system is torn down, regardless of whether the bus outlives it.
  std::vector<EventBus::SubscriptionHandle> subscriptions_;
  std::unordered_set<std::string> pressedKeys_;
  std::unordered_set<std::string> releasedKeys_;
  std::unordered_set<std::string> heldKeys_;
  std::unordered_map<std::string, std::unordered_set<std::string>> keyAliases_;

  std::unordered_set<int> pressedMouseButtons_;
  std::unordered_set<int> releasedMouseButtons_;
  std::unordered_set<int> heldMouseButtons_;

  std::unordered_set<std::string> pressedGamepadButtons_;
  std::unordered_set<std::string> releasedGamepadButtons_;
  std::unordered_set<std::string> heldGamepadButtons_;
  struct ConnectedGamepad {
    SDL_JoystickID id = 0;
    SDL_Gamepad* handle = nullptr;
  };
  std::vector<ConnectedGamepad> connectedGamepads_;
  SDL_JoystickID activeGamepadId_ = 0;
  std::unordered_map<std::string, std::unordered_set<std::string>> gamepadAliases_;
  std::array<float, SDL_GAMEPAD_AXIS_COUNT> gamepadAxes_{};
  std::array<bool, 2> triggerDigitalDown_{false, false};
#ifndef OCTARINE_SHIPPED
  mutable std::unordered_set<std::string> warnedUnknownTokens_;
#endif

  float mouseX_ = 0.0f;
  float mouseY_ = 0.0f;
  float wheelDx_ = 0.0f;
  float wheelDy_ = 0.0f;

  std::unordered_map<std::string, std::vector<std::string>> actions_;

  std::vector<sol::protected_function> onKeyDown_;
  std::vector<sol::protected_function> onKeyUp_;
  std::vector<sol::protected_function> onMouseDown_;
  std::vector<sol::protected_function> onMouseUp_;
  std::vector<sol::protected_function> onMouseWheel_;
  std::vector<sol::protected_function> onGamepadDown_;
  std::vector<sol::protected_function> onGamepadUp_;
};
