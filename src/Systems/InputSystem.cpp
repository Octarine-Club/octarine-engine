#include "Systems/InputSystem.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <string>
#include <string_view>

#include "Components/ViewportInfo.h"
#include "Game/GameConfig.h"

void InputSystem::BeginFrame() {
  float mx = 0.0f;
  float my = 0.0f;
  SDL_GetMouseState(&mx, &my);

  if (registry_) {
    const auto& viewport = registry_->Get<ViewportInfo>();
    const auto& config = registry_->Get<GameConfig>();
    const glm::vec2 transformed = viewport.TransformCoordinates(mx, my, config.windowWidth, config.windowHeight);
    mouseX_ = transformed.x;
    mouseY_ = transformed.y;
  } else {
    mouseX_ = mx;
    mouseY_ = my;
  }

  if (!connectedGamepads_.empty() || SDL_HasGamepad()) {
    PollGamepadAxes();
  }
}

#ifndef OCTARINE_SHIPPED
void InputSystem::WarnUnknownToken(const std::string_view kind, const std::string_view token) const {
  if (token.empty()) return;
  const std::string key = std::string(kind) + ":" + std::string(token);
  if (warnedUnknownTokens_.insert(key).second) {
    Logger::Warn("InputSystem: unrecognized gamepad " + std::string(kind) + " '" + std::string(token) + "'");
  }
}
#endif

#ifndef OCTARINE_SHIPPED
bool InputSystem::ShouldLogInput() const {
  if (!registry_) return false;
  const auto* config = registry_->TryGet<GameConfig>();
  return config && config->GetEngineOptions().logInputEvents;
}
#endif

void InputSystem::OnKeyInput(const KeyInputEvent& event) {
  if (registry_ && event.isPressed) {
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isFocused) {
      return;
    }
  }

  const std::string key = MakeKey(SDL_GetKeyName(event.inputKey));
#ifndef OCTARINE_SHIPPED
  if (ShouldLogInput()) {
    Logger::Info("[input] key " + std::string(event.isPressed ? "down" : "up") + ": " + key);
  }
#endif
  if (event.isPressed) {
    if (heldKeys_.find(key) == heldKeys_.end()) {
      pressedKeys_.insert(key);
      Dispatch(onKeyDown_, key);
    }
    heldKeys_.insert(key);
  } else {
    if (heldKeys_.erase(key) > 0) {
      pressedKeys_.erase(key);
      releasedKeys_.insert(key);
      Dispatch(onKeyUp_, key);
    }
  }
}

void InputSystem::OnMouseInput(const MouseInputEvent& event) {
  if (registry_ && (event.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isHovered) {
      return;
    }
  }

  const int btn = static_cast<int>(event.event.button);
  float x = event.event.x;
  float y = event.event.y;

  if (registry_) {
    const auto& viewport = registry_->Get<ViewportInfo>();
    const auto& config = registry_->Get<GameConfig>();
    const glm::vec2 transformed = viewport.TransformCoordinates(x, y, config.windowWidth, config.windowHeight);
    x = transformed.x;
    y = transformed.y;
  }

#ifndef OCTARINE_SHIPPED
  if (ShouldLogInput()) {
    const bool down = event.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    Logger::Info("[input] mouse " + std::string(down ? "down" : "up") + " btn " + std::to_string(btn) + " at (" +
                 std::to_string(x) + ", " + std::to_string(y) + ")");
  }
#endif

  if (event.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    if (heldMouseButtons_.find(btn) == heldMouseButtons_.end()) {
      pressedMouseButtons_.insert(btn);
      Dispatch(onMouseDown_, btn, x, y);
    }
    heldMouseButtons_.insert(btn);
  } else if (event.event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
    heldMouseButtons_.erase(btn);
    pressedMouseButtons_.erase(btn);
    releasedMouseButtons_.insert(btn);
    Dispatch(onMouseUp_, btn, x, y);
  }
}

void InputSystem::OnMouseWheel(const MouseWheelEvent& event) {
  if (registry_) {
    const auto& viewport = registry_->Get<ViewportInfo>();
    if (!viewport.isHovered) {
      return;
    }
  }

#ifndef OCTARINE_SHIPPED
  if (ShouldLogInput()) {
    Logger::Info("[input] wheel " + std::to_string(event.dx) + ", " + std::to_string(event.dy));
  }
#endif

  wheelDx_ += event.dx;
  wheelDy_ += event.dy;
  Dispatch(onMouseWheel_, event.dx, event.dy);
}

void InputSystem::InitGamepadAliases() {
  gamepadAliases_["a"] = {"south", "a", "cross"};
  gamepadAliases_["b"] = {"east", "b", "circle"};
  gamepadAliases_["x"] = {"west", "x", "square"};
  gamepadAliases_["y"] = {"north", "y", "triangle"};
  gamepadAliases_["south"] = {"south", "a", "cross"};
  gamepadAliases_["east"] = {"east", "b", "circle"};
  gamepadAliases_["west"] = {"west", "x", "square"};
  gamepadAliases_["north"] = {"north", "y", "triangle"};
  gamepadAliases_["cross"] = {"south", "a", "cross"};
  gamepadAliases_["circle"] = {"east", "b", "circle"};
  gamepadAliases_["square"] = {"west", "x", "square"};
  gamepadAliases_["triangle"] = {"north", "y", "triangle"};

  gamepadAliases_["lb"] = {"leftshoulder", "lb", "l1", "left shoulder"};
  gamepadAliases_["rb"] = {"rightshoulder", "rb", "r1", "right shoulder"};
  gamepadAliases_["leftshoulder"] = {"leftshoulder", "lb", "l1", "left shoulder"};
  gamepadAliases_["rightshoulder"] = {"rightshoulder", "rb", "r1", "right shoulder"};
  gamepadAliases_["left shoulder"] = {"leftshoulder", "lb", "l1", "left shoulder"};
  gamepadAliases_["right shoulder"] = {"rightshoulder", "rb", "r1", "right shoulder"};
  gamepadAliases_["l1"] = {"leftshoulder", "lb", "l1", "left shoulder"};
  gamepadAliases_["r1"] = {"rightshoulder", "rb", "r1", "right shoulder"};

  gamepadAliases_["ls"] = {"leftstick", "ls", "l3", "left stick"};
  gamepadAliases_["rs"] = {"rightstick", "rs", "r3", "right stick"};
  gamepadAliases_["leftstick"] = {"leftstick", "ls", "l3", "left stick"};
  gamepadAliases_["rightstick"] = {"rightstick", "rs", "r3", "right stick"};
  gamepadAliases_["left stick"] = {"leftstick", "ls", "l3", "left stick"};
  gamepadAliases_["right stick"] = {"rightstick", "rs", "r3", "right stick"};
  gamepadAliases_["l3"] = {"leftstick", "ls", "l3", "left stick"};
  gamepadAliases_["r3"] = {"rightstick", "rs", "r3", "right stick"};

  gamepadAliases_["start"] = {"start", "menu", "options"};
  gamepadAliases_["menu"] = {"start", "menu", "options"};
  gamepadAliases_["options"] = {"start", "menu", "options"};

  gamepadAliases_["back"] = {"back", "select", "share", "view"};
  gamepadAliases_["select"] = {"back", "select", "share", "view"};
  gamepadAliases_["share"] = {"back", "select", "share", "view"};
  gamepadAliases_["view"] = {"back", "select", "share", "view"};

  gamepadAliases_["guide"] = {"guide", "home"};
  gamepadAliases_["home"] = {"guide", "home"};

  gamepadAliases_["dpad_up"] = {"dpup", "dpad_up", "up", "dpad up"};
  gamepadAliases_["dpup"] = {"dpup", "dpad_up", "up", "dpad up"};
  gamepadAliases_["up"] = {"dpup", "dpad_up", "up", "dpad up"};
  gamepadAliases_["dpad up"] = {"dpup", "dpad_up", "up", "dpad up"};

  gamepadAliases_["dpad_down"] = {"dpdown", "dpad_down", "down", "dpad down"};
  gamepadAliases_["dpdown"] = {"dpdown", "dpad_down", "down", "dpad down"};
  gamepadAliases_["down"] = {"dpdown", "dpad_down", "down", "dpad down"};
  gamepadAliases_["dpad down"] = {"dpdown", "dpad_down", "down", "dpad down"};

  gamepadAliases_["dpad_left"] = {"dpleft", "dpad_left", "left", "dpad left"};
  gamepadAliases_["dpleft"] = {"dpleft", "dpad_left", "left", "dpad left"};
  gamepadAliases_["left"] = {"dpleft", "dpad_left", "left", "dpad left"};
  gamepadAliases_["dpad left"] = {"dpleft", "dpad_left", "left", "dpad left"};

  gamepadAliases_["dpad_right"] = {"dpright", "dpad_right", "right", "dpad right"};
  gamepadAliases_["dpright"] = {"dpright", "dpad_right", "right", "dpad right"};
  gamepadAliases_["right"] = {"dpright", "dpad_right", "right", "dpad right"};
  gamepadAliases_["dpad right"] = {"dpright", "dpad_right", "right", "dpad right"};

  gamepadAliases_["lt"] = {"lt", "l2", "lefttrigger", "left_trigger", "left trigger"};
  gamepadAliases_["l2"] = {"lt", "l2", "lefttrigger", "left_trigger", "left trigger"};
  gamepadAliases_["lefttrigger"] = {"lt", "l2", "lefttrigger", "left_trigger", "left trigger"};
  gamepadAliases_["left_trigger"] = {"lt", "l2", "lefttrigger", "left_trigger", "left trigger"};
  gamepadAliases_["left trigger"] = {"lt", "l2", "lefttrigger", "left_trigger", "left trigger"};

  gamepadAliases_["rt"] = {"rt", "r2", "righttrigger", "right_trigger", "right trigger"};
  gamepadAliases_["r2"] = {"rt", "r2", "righttrigger", "right_trigger", "right trigger"};
  gamepadAliases_["righttrigger"] = {"rt", "r2", "righttrigger", "right_trigger", "right trigger"};
  gamepadAliases_["right_trigger"] = {"rt", "r2", "righttrigger", "right_trigger", "right trigger"};
  gamepadAliases_["right trigger"] = {"rt", "r2", "righttrigger", "right_trigger", "right trigger"};
}

void InputSystem::ApplyGamepadButtonState(const std::string& btn, const bool isPressed) {
  if (isPressed) {
    if (heldGamepadButtons_.find(btn) == heldGamepadButtons_.end()) {
      pressedGamepadButtons_.insert(btn);
      Dispatch(onGamepadDown_, btn);
    }
    heldGamepadButtons_.insert(btn);
    return;
  }
  if (heldGamepadButtons_.erase(btn) > 0) {
    pressedGamepadButtons_.erase(btn);
    releasedGamepadButtons_.insert(btn);
    Dispatch(onGamepadUp_, btn);
  }
}

void InputSystem::OnGamepadButton(const GamepadButtonEvent& event) {
  if (event.isPressed && !IsViewportFocused()) return;

  const std::string btn = CanonicalizeGamepadButton(event.buttonName);
  if (btn.empty()) return;

  if (event.isPressed && event.which != 0) {
    NoteGamepadActivity(event.which);
  }

#ifndef OCTARINE_SHIPPED
  if (ShouldLogInput()) {
    Logger::Info("[input] gamepad " + std::string(event.isPressed ? "down" : "up") + ": " + btn);
  }
#endif

  ApplyGamepadButtonState(btn, event.isPressed);
}

bool InputSystem::IsKeyDown(const std::string& key) const {
  if (registry_) {
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isFocused) return false;
  }
  return MatchesSet(key, heldKeys_);
}

bool InputSystem::IsKeyPressed(const std::string& key) const {
  if (registry_) {
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isFocused) return false;
  }
  return MatchesSet(key, pressedKeys_);
}

bool InputSystem::IsKeyReleased(const std::string& key) const {
  if (registry_) {
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isFocused) return false;
  }
  return MatchesSet(key, releasedKeys_);
}

bool InputSystem::MatchesSet(const std::string& key, const std::unordered_set<std::string>& set) const {
  if (key.empty()) return false;
  const std::string lower = MakeKey(key);
  if (const auto it = keyAliases_.find(lower); it != keyAliases_.end()) {
    for (const auto& alias : it->second) {
      if (set.find(alias) != set.end()) return true;
    }
  }
  return set.find(lower) != set.end();
}

bool InputSystem::IsGamepadButtonDown(const std::string& button) const {
  if (registry_) {
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isFocused) return false;
  }
  return MatchesGamepadSet(button, heldGamepadButtons_);
}

bool InputSystem::IsGamepadButtonPressed(const std::string& button) const {
  if (registry_) {
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isFocused) return false;
  }
  return MatchesGamepadSet(button, pressedGamepadButtons_);
}

bool InputSystem::IsGamepadButtonReleased(const std::string& button) const {
  if (registry_) {
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isFocused) return false;
  }
  return MatchesGamepadSet(button, releasedGamepadButtons_);
}

bool InputSystem::IsGamepadConnected() const { return !connectedGamepads_.empty() || SDL_HasGamepad(); }

void InputSystem::SyncConnectedGamepads() {
  int count = 0;
  SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
  if (!gamepads) return;
  for (int i = 0; i < count; ++i) {
    OnGamepadAdded(gamepads[i]);
  }
  SDL_free(gamepads);
}

void InputSystem::OnGamepadAdded(const SDL_JoystickID id, SDL_Gamepad* pad) {
  if (id == 0) return;
  if (!pad) {
    pad = SDL_GetGamepadFromID(id);
    if (!pad) pad = SDL_OpenGamepad(id);
  }
  if (!pad) return;
  for (auto& entry : connectedGamepads_) {
    if (entry.id == id) {
      entry.handle = pad;
      return;
    }
  }
  connectedGamepads_.push_back({id, pad});
  if (activeGamepadId_ == 0) {
    activeGamepadId_ = id;
  }
}

void InputSystem::OnGamepadRemoved(const SDL_JoystickID id) {
  std::erase_if(connectedGamepads_, [id](const ConnectedGamepad& entry) { return entry.id == id; });
  if (activeGamepadId_ == id) {
    activeGamepadId_ = connectedGamepads_.empty() ? 0 : connectedGamepads_.front().id;
  }
  if (connectedGamepads_.empty()) {
    heldGamepadButtons_.clear();
    gamepadAxes_.fill(0.0f);
    triggerDigitalDown_.fill(false);
  }
}

void InputSystem::NoteGamepadActivity(const SDL_JoystickID id) {
  if (id == 0) return;
  for (const auto& entry : connectedGamepads_) {
    if (entry.id == id) {
      activeGamepadId_ = id;
      return;
    }
  }
}

SDL_Gamepad* InputSystem::FindActiveGamepad() const noexcept {
  for (const auto& entry : connectedGamepads_) {
    if (entry.id == activeGamepadId_ && entry.handle != nullptr) {
      return entry.handle;
    }
  }
  for (const auto& entry : connectedGamepads_) {
    if (entry.handle != nullptr) return entry.handle;
  }
  return nullptr;
}

bool InputSystem::IsViewportFocused() const {
  if (!registry_) return true;
  const auto* viewport = registry_->TryGet<ViewportInfo>();
  return !viewport || viewport->isFocused;
}

void InputSystem::UpdateTriggerDigitalState(const SDL_GamepadAxis axis, const float normalizedValue) {
  if (axis != SDL_GAMEPAD_AXIS_LEFT_TRIGGER && axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
    return;
  }
  const size_t idx = (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) ? 0u : 1u;
  const char* trigName = (idx == 0u) ? "lt" : "rt";
  const bool wasDown = triggerDigitalDown_[idx];
  constexpr float kTriggerPressThreshold = 0.45f;
  constexpr float kTriggerReleaseThreshold = 0.35f;
  const bool nowDown =
      wasDown ? (normalizedValue >= kTriggerReleaseThreshold) : (normalizedValue >= kTriggerPressThreshold);
  if (nowDown == wasDown || (nowDown && !IsViewportFocused())) {
    return;
  }
  triggerDigitalDown_[idx] = nowDown;
  const GamepadButtonEvent trigEvent(activeGamepadId_, SDL_GAMEPAD_BUTTON_INVALID, trigName, nowDown);
  if (eventBus_ != nullptr) {
    eventBus_->EmitEvent<GamepadButtonEvent>(trigEvent);
  } else {
    OnGamepadButton(trigEvent);
  }
}

void InputSystem::SetGamepadAxis(const SDL_GamepadAxis axis, const float normalizedValue) {
  if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) return;
  const bool isTrigger = (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
  const float clamped = std::clamp(normalizedValue, isTrigger ? 0.0f : -1.0f, 1.0f);
  gamepadAxes_[static_cast<size_t>(axis)] = clamped;
  UpdateTriggerDigitalState(axis, clamped);
}

void InputSystem::PollGamepadAxes() {
  if (connectedGamepads_.empty() && SDL_HasGamepad()) {
    SyncConnectedGamepads();
  }
  SDL_Gamepad* pad = FindActiveGamepad();
  if (!pad) return;

  static constexpr std::array<SDL_GamepadAxis, 6> kAxes = {
      SDL_GAMEPAD_AXIS_LEFTX,  SDL_GAMEPAD_AXIS_LEFTY,        SDL_GAMEPAD_AXIS_RIGHTX,
      SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
  };
  for (const SDL_GamepadAxis axis : kAxes) {
    const Sint16 raw = SDL_GetGamepadAxis(pad, axis);
    const float norm = (raw < 0) ? (static_cast<float>(raw) / 32768.0f) : (static_cast<float>(raw) / 32767.0f);
    SetGamepadAxis(axis, norm);
  }
}

namespace {
struct CanonicalEntry {
  std::string_view alias;
  std::string_view canonical;
};

constexpr std::array<CanonicalEntry, 62> kCanonicalGamepadButtons = {{
    {"south", "south"},
    {"a", "south"},
    {"cross", "south"},
    {"east", "east"},
    {"b", "east"},
    {"circle", "east"},
    {"west", "west"},
    {"x", "west"},
    {"square", "west"},
    {"north", "north"},
    {"y", "north"},
    {"triangle", "north"},
    {"lb", "lb"},
    {"leftshoulder", "lb"},
    {"l1", "lb"},
    {"left shoulder", "lb"},
    {"left_shoulder", "lb"},
    {"rb", "rb"},
    {"rightshoulder", "rb"},
    {"r1", "rb"},
    {"right shoulder", "rb"},
    {"right_shoulder", "rb"},
    {"lt", "lt"},
    {"l2", "lt"},
    {"lefttrigger", "lt"},
    {"left_trigger", "lt"},
    {"left trigger", "lt"},
    {"rt", "rt"},
    {"r2", "rt"},
    {"righttrigger", "rt"},
    {"right_trigger", "rt"},
    {"right trigger", "rt"},
    {"ls", "ls"},
    {"leftstick", "ls"},
    {"l3", "ls"},
    {"left stick", "ls"},
    {"left_stick", "ls"},
    {"rs", "rs"},
    {"rightstick", "rs"},
    {"r3", "rs"},
    {"right stick", "rs"},
    {"right_stick", "rs"},
    {"dpad_up", "dpad_up"},
    {"dpup", "dpad_up"},
    {"up", "dpad_up"},
    {"dpad up", "dpad_up"},
    {"dpad_down", "dpad_down"},
    {"dpdown", "dpad_down"},
    {"down", "dpad_down"},
    {"dpad down", "dpad_down"},
    {"dpad_left", "dpad_left"},
    {"dpleft", "dpad_left"},
    {"left", "dpad_left"},
    {"dpad left", "dpad_left"},
    {"dpad_right", "dpad_right"},
    {"dpright", "dpad_right"},
    {"right", "dpad_right"},
    {"dpad right", "dpad_right"},
    {"start", "start"},
    {"menu", "start"},
    {"options", "start"},
    {"back", "back"},
}};

constexpr std::array<CanonicalEntry, 13> kCanonicalExtraGamepadButtons = {{
    {"select", "back"},
    {"share", "back"},
    {"view", "back"},
    {"guide", "guide"},
    {"home", "guide"},
    {"misc1", "misc1"},
    {"touchpad", "touchpad"},
    {"paddle1", "paddle1"},
    {"paddle2", "paddle2"},
    {"paddle3", "paddle3"},
    {"paddle4", "paddle4"},
    {"left_stick_click", "ls"},
    {"right_stick_click", "rs"},
}};

std::string_view TrimAsciiSv(std::string_view s) noexcept {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
    s.remove_suffix(1);
  }
  return s;
}

bool IsAmbiguousKeyboardKeyName(const std::string_view lower) {
  return lower == "a" || lower == "b" || lower == "x" || lower == "y" || lower == "home" || lower == "menu" ||
         lower == "select";
}

bool IsSharedDirectionToken(const std::string_view lower) {
  return lower == "up" || lower == "down" || lower == "left" || lower == "right";
}

bool IsExtraGamepadOnlyToken(const std::string_view lower) {
  return lower == "misc1" || lower == "touchpad" || lower == "paddle1" || lower == "paddle2" || lower == "paddle3" ||
         lower == "paddle4";
}

bool StripPrefix(std::string_view& token, const std::string_view prefix) {
  if (!token.starts_with(prefix)) return false;
  token.remove_prefix(prefix.size());
  return true;
}

std::string_view StripAnyDevicePrefix(std::string_view token) {
  token = TrimAsciiSv(token);
  if (StripPrefix(token, "gamepad:") || StripPrefix(token, "pad:") || StripPrefix(token, "gp:")) {
    return TrimAsciiSv(token);
  }
  (void)(StripPrefix(token, "key:") || StripPrefix(token, "kb:"));
  return TrimAsciiSv(token);
}

SDL_GamepadAxis ResolveAxisAlias(const std::string_view lower) {
  if (lower == "leftx" || lower == "left_x" || lower == "lx") return SDL_GAMEPAD_AXIS_LEFTX;
  if (lower == "lefty" || lower == "left_y" || lower == "ly") return SDL_GAMEPAD_AXIS_LEFTY;
  if (lower == "rightx" || lower == "right_x" || lower == "rx") return SDL_GAMEPAD_AXIS_RIGHTX;
  if (lower == "righty" || lower == "right_y" || lower == "ry") return SDL_GAMEPAD_AXIS_RIGHTY;
  if (lower == "lt" || lower == "l2" || lower == "lefttrigger" || lower == "left_trigger") {
    return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
  }
  if (lower == "rt" || lower == "r2" || lower == "righttrigger" || lower == "right_trigger") {
    return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
  }
  return SDL_GAMEPAD_AXIS_INVALID;
}
}  // namespace

std::string_view InputSystem::LookupCanonicalGamepadButton(const std::string_view lower) noexcept {
  for (const auto& entry : kCanonicalGamepadButtons) {
    if (entry.alias == lower) return entry.canonical;
  }
  for (const auto& entry : kCanonicalExtraGamepadButtons) {
    if (entry.alias == lower) return entry.canonical;
  }
  return {};
}

std::string InputSystem::CanonicalizeGamepadButton(const std::string_view raw) {
  std::string lower = MakeKey(std::string(StripAnyDevicePrefix(raw)));
  if (const std::string_view canon = LookupCanonicalGamepadButton(lower); !canon.empty()) {
    return std::string(canon);
  }
  return lower;
}

std::string InputSystem::CanonicalizeKeyName(const std::string_view raw) {
  std::string lower = MakeKey(std::string(StripAnyDevicePrefix(raw)));
  if (lower == "esc") return "escape";
  if (lower == "enter" || lower == "keypad enter") return "return";
  if (lower == "spacebar") return "space";
  return lower;
}

float InputSystem::GamepadAxis(const std::string& axisName, const float deadzone) const {
  if (!IsViewportFocused()) return 0.0f;
  const SDL_GamepadAxis axis = ResolveAxisAlias(MakeKey(axisName));
  if (axis == SDL_GAMEPAD_AXIS_INVALID || axis >= SDL_GAMEPAD_AXIS_COUNT) {
#ifndef OCTARINE_SHIPPED
    WarnUnknownToken("axis", axisName);
#endif
    return 0.0f;
  }
  const float raw = gamepadAxes_[static_cast<size_t>(axis)];
  const float dz = std::clamp(deadzone, 0.0f, 0.99f);
  const float mag = std::abs(raw);
  if (mag <= dz) return 0.0f;
  const float scaled = std::min(1.0f, (mag - dz) / (1.0f - dz));
  return (raw < 0.0f) ? -scaled : scaled;
}

glm::vec2 InputSystem::GamepadStick(const std::string& stickName, const float deadzone) const {
  if (!IsViewportFocused()) return {0.0f, 0.0f};
  const std::string lower = MakeKey(stickName);
  glm::vec2 raw{0.0f, 0.0f};
  if (lower == "left" || lower == "left_stick" || lower == "leftstick" || lower == "ls") {
    raw = {gamepadAxes_[SDL_GAMEPAD_AXIS_LEFTX], gamepadAxes_[SDL_GAMEPAD_AXIS_LEFTY]};
  } else if (lower == "right" || lower == "right_stick" || lower == "rightstick" || lower == "rs") {
    raw = {gamepadAxes_[SDL_GAMEPAD_AXIS_RIGHTX], gamepadAxes_[SDL_GAMEPAD_AXIS_RIGHTY]};
  } else {
#ifndef OCTARINE_SHIPPED
    WarnUnknownToken("stick", stickName);
#endif
    return {0.0f, 0.0f};
  }
  const float len = glm::length(raw);
  const float dz = std::clamp(deadzone, 0.0f, 0.99f);
  if (len <= dz || len <= 0.0001f) return {0.0f, 0.0f};
  const float scaled = std::min(1.0f, (len - dz) / (1.0f - dz));
  return (raw / len) * scaled;
}

bool InputSystem::MatchesGamepadSet(const std::string& button, const std::unordered_set<std::string>& set) const {
  if (button.empty()) return false;
  const std::string canon = CanonicalizeGamepadButton(button);
  if (set.contains(canon)) return true;
  if (const auto it = gamepadAliases_.find(MakeKey(button)); it != gamepadAliases_.end()) {
    for (const auto& alias : it->second) {
      if (set.contains(alias)) return true;
    }
  }
  return false;
}

bool InputSystem::MatchesGamepadAlias(const std::string& requested, const std::string& actual) const {
  if (requested.empty() || actual.empty()) return false;
  const std::string reqCanon = CanonicalizeGamepadButton(requested);
  const std::string actCanon = CanonicalizeGamepadButton(actual);
  if (reqCanon == actCanon) return true;
  if (const auto it = gamepadAliases_.find(MakeKey(requested)); it != gamepadAliases_.end()) {
    if (it->second.contains(MakeKey(actual)) || it->second.contains(actCanon)) return true;
  }
  return false;
}

bool InputSystem::MatchesKeyAlias(const std::string& requested, const std::string& actual) const {
  if (requested.empty() || actual.empty()) return false;
  const std::string reqLower = MakeKey(requested);
  const std::string actLower = MakeKey(actual);
  if (reqLower == actLower) return true;
  if (const auto it = keyAliases_.find(reqLower); it != keyAliases_.end()) {
    if (it->second.find(actLower) != it->second.end()) return true;
  }
  if (const auto it = keyAliases_.find(actLower); it != keyAliases_.end()) {
    if (it->second.find(reqLower) != it->second.end()) return true;
  }
  return false;
}

// Entries in actions_ use "key:" / "gamepad:" prefixes when explicitly bound by device;
// unprefixed legacy entries disambiguate ambiguous names ("a", "b", "x", "y", "up", ...) as keyboard/shared.
std::string_view InputSystem::ResolveKeyToken(std::string_view token) const {
  if (StripPrefix(token, "gamepad:") || StripPrefix(token, "pad:") || StripPrefix(token, "gp:")) return {};
  if (StripPrefix(token, "key:") || StripPrefix(token, "kb:")) return token;
  if (IsAmbiguousKeyboardKeyName(token) || IsSharedDirectionToken(token)) return token;
  if (!LookupCanonicalGamepadButton(token).empty() || IsExtraGamepadOnlyToken(token)) return {};
  return token;
}

std::string_view InputSystem::ResolveGamepadToken(std::string_view token) const {
  if (StripPrefix(token, "key:") || StripPrefix(token, "kb:")) return {};
  if (StripPrefix(token, "gamepad:") || StripPrefix(token, "pad:") || StripPrefix(token, "gp:")) {
    return token;
  }
  if (IsAmbiguousKeyboardKeyName(token)) return {};
  if (IsSharedDirectionToken(token) || !LookupCanonicalGamepadButton(token).empty() || IsExtraGamepadOnlyToken(token)) {
    return token;
  }
  return {};
}

bool InputSystem::IsActionBoundToKey(const std::string& action, const std::string& key) const {
  const auto it = actions_.find(MakeKey(action));
  if (it == actions_.end()) return false;
  return std::ranges::any_of(it->second, [this, &key](const auto& k) {
    const std::string_view keyToken = ResolveKeyToken(k);
    return !keyToken.empty() && MatchesKeyAlias(std::string(keyToken), key);
  });
}

bool InputSystem::IsActionBoundToGamepadButton(const std::string& action, const std::string& button) const {
  const auto it = actions_.find(MakeKey(action));
  if (it == actions_.end()) return false;
  return std::ranges::any_of(it->second, [this, &button](const auto& k) {
    const std::string_view padToken = ResolveGamepadToken(k);
    return !padToken.empty() && MatchesGamepadAlias(std::string(padToken), button);
  });
}

bool InputSystem::IsActionBoundToInput(const std::string& action, const std::string& input) const {
  return IsActionBoundToKey(action, input) || IsActionBoundToGamepadButton(action, input);
}

bool InputSystem::IsMouseDown(const int btn) const {
  if (registry_) {
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isHovered) return false;
  }
  return btn > 0 && heldMouseButtons_.find(btn) != heldMouseButtons_.end();
}

bool InputSystem::IsMousePressed(const int btn) const {
  if (registry_) {
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isHovered) return false;
  }
  return btn > 0 && pressedMouseButtons_.find(btn) != pressedMouseButtons_.end();
}

bool InputSystem::IsMouseReleased(const int btn) const {
  if (registry_) {
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isHovered) return false;
  }
  return btn > 0 && releasedMouseButtons_.find(btn) != releasedMouseButtons_.end();
}

void InputSystem::Bind(const std::string& action, const std::string& key) {
  auto& keys = actions_[MakeKey(action)];
  const std::string lower = MakeKey(key);
  if (std::find(keys.begin(), keys.end(), lower) == keys.end()) {
    keys.push_back(lower);
  }
}

void InputSystem::Unbind(const std::string& action, const std::string& key) {
  const auto it = actions_.find(MakeKey(action));
  if (it == actions_.end()) return;
  const std::string lower = MakeKey(key);
  auto& keys = it->second;
  keys.erase(std::remove(keys.begin(), keys.end(), lower), keys.end());
  if (keys.empty()) actions_.erase(it);
}

void InputSystem::BindKey(const std::string& action, const std::string& key) {
  const std::string clean = CanonicalizeKeyName(key);
  if (clean.empty()) return;
  auto& entries = actions_[MakeKey(action)];
  const std::string prefixed = "key:" + clean;
  if (std::ranges::find(entries, prefixed) == entries.end()) {
    entries.push_back(prefixed);
  }
}

void InputSystem::UnbindKey(const std::string& action, const std::string& key) {
  const auto it = actions_.find(MakeKey(action));
  if (it == actions_.end()) return;
  const std::string clean = CanonicalizeKeyName(key);
  if (clean.empty()) return;

  std::vector<std::string> kept;
  kept.reserve(it->second.size());
  for (const auto& entry : it->second) {
    const std::string_view keyToken = ResolveKeyToken(entry);
    if (!keyToken.empty() && MatchesKeyAlias(std::string(keyToken), clean)) {
      if (const std::string_view padToken = ResolveGamepadToken(entry); !padToken.empty()) {
        kept.push_back("gamepad:" + std::string(padToken));
      }
      continue;
    }
    kept.push_back(entry);
  }
  if (kept.empty()) {
    actions_.erase(it);
  } else {
    it->second = std::move(kept);
  }
}

void InputSystem::UnbindActionKeys(const std::string& action) {
  const auto it = actions_.find(MakeKey(action));
  if (it == actions_.end()) return;

  std::vector<std::string> kept;
  kept.reserve(it->second.size());
  for (const auto& entry : it->second) {
    const std::string_view padToken = ResolveGamepadToken(entry);
    if (padToken.empty()) continue;
    kept.push_back(ResolveKeyToken(entry).empty() ? entry : ("gamepad:" + std::string(padToken)));
  }
  if (kept.empty()) {
    actions_.erase(it);
  } else {
    it->second = std::move(kept);
  }
}

void InputSystem::BindGamepad(const std::string& action, const std::string& button) {
  const std::string stripped = MakeKey(std::string(StripAnyDevicePrefix(button)));
  if (stripped.empty()) return;
  const std::string_view canonView = LookupCanonicalGamepadButton(stripped);
#ifndef OCTARINE_SHIPPED
  if (canonView.empty()) {
    WarnUnknownToken("button", button);
  }
#endif
  const std::string canon = canonView.empty() ? stripped : std::string(canonView);
  auto& entries = actions_[MakeKey(action)];
  const std::string prefixed = "gamepad:" + canon;
  if (std::ranges::find(entries, prefixed) == entries.end()) {
    entries.push_back(prefixed);
  }
}

void InputSystem::UnbindGamepad(const std::string& action, const std::string& button) {
  const auto it = actions_.find(MakeKey(action));
  if (it == actions_.end()) return;
  const std::string stripped = MakeKey(std::string(StripAnyDevicePrefix(button)));
  if (stripped.empty()) return;
  const std::string_view canonView = LookupCanonicalGamepadButton(stripped);
#ifndef OCTARINE_SHIPPED
  if (canonView.empty()) {
    WarnUnknownToken("button", button);
  }
#endif
  const std::string canon = canonView.empty() ? stripped : std::string(canonView);

  std::vector<std::string> kept;
  kept.reserve(it->second.size());
  for (const auto& entry : it->second) {
    const std::string_view padToken = ResolveGamepadToken(entry);
    if (!padToken.empty() && MatchesGamepadAlias(std::string(padToken), canon)) {
      if (const std::string_view keyToken = ResolveKeyToken(entry); !keyToken.empty()) {
        kept.push_back("key:" + std::string(keyToken));
      }
      continue;
    }
    kept.push_back(entry);
  }
  if (kept.empty()) {
    actions_.erase(it);
  } else {
    it->second = std::move(kept);
  }
}

void InputSystem::UnbindActionGamepad(const std::string& action) {
  const auto it = actions_.find(MakeKey(action));
  if (it == actions_.end()) return;

  std::vector<std::string> kept;
  kept.reserve(it->second.size());
  for (const auto& entry : it->second) {
    const std::string_view keyToken = ResolveKeyToken(entry);
    if (keyToken.empty()) continue;
    kept.push_back(ResolveGamepadToken(entry).empty() ? entry : ("key:" + std::string(keyToken)));
  }
  if (kept.empty()) {
    actions_.erase(it);
  } else {
    it->second = std::move(kept);
  }
}

std::vector<std::string> InputSystem::GetActionKeyBindings(const std::string& action) const {
  std::vector<std::string> out;
  const auto it = actions_.find(MakeKey(action));
  if (it == actions_.end()) return out;
  for (const auto& entry : it->second) {
    const std::string_view keyToken = ResolveKeyToken(entry);
    if (keyToken.empty()) continue;
    const std::string keyStr(keyToken);
    if (std::ranges::find(out, keyStr) == out.end()) {
      out.push_back(keyStr);
    }
  }
  return out;
}

std::vector<std::string> InputSystem::GetActionGamepadBindings(const std::string& action) const {
  std::vector<std::string> out;
  const auto it = actions_.find(MakeKey(action));
  if (it == actions_.end()) return out;
  for (const auto& entry : it->second) {
    const std::string_view padToken = ResolveGamepadToken(entry);
    if (padToken.empty()) continue;
    const std::string canon = CanonicalizeGamepadButton(padToken);
    if (std::ranges::find(out, canon) == out.end()) {
      out.push_back(canon);
    }
  }
  return out;
}

std::vector<std::string> InputSystem::GetBoundActions() const {
  std::vector<std::string> out;
  out.reserve(actions_.size());
  for (const auto& [action, _] : actions_) {
    out.push_back(action);
  }
  std::ranges::sort(out);
  return out;
}

bool InputSystem::IsActionDown(const std::string& action) const {
  return AnyBoundKey(action, [this](const std::string& k) {
    const std::string_view keyToken = ResolveKeyToken(k);
    const std::string_view padToken = ResolveGamepadToken(k);
    return (!keyToken.empty() && IsKeyDown(std::string(keyToken))) ||
           (!padToken.empty() && IsGamepadButtonDown(std::string(padToken)));
  });
}

bool InputSystem::IsActionPressed(const std::string& action) const {
  return AnyBoundKey(action, [this](const std::string& k) {
    const std::string_view keyToken = ResolveKeyToken(k);
    const std::string_view padToken = ResolveGamepadToken(k);
    return (!keyToken.empty() && IsKeyPressed(std::string(keyToken))) ||
           (!padToken.empty() && IsGamepadButtonPressed(std::string(padToken)));
  });
}

bool InputSystem::IsActionReleased(const std::string& action) const {
  return AnyBoundKey(action, [this](const std::string& k) {
    const std::string_view keyToken = ResolveKeyToken(k);
    const std::string_view padToken = ResolveGamepadToken(k);
    return (!keyToken.empty() && IsKeyReleased(std::string(keyToken))) ||
           (!padToken.empty() && IsGamepadButtonReleased(std::string(padToken)));
  });
}
