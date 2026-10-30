#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <sol/sol.hpp>
#include <string>
#include <utility>
#include <vector>

#include "Components/BoxColliderComponent.h"
#include "Components/CameraComponents.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/UIButtonComponent.h"
#include "Components/UIFocusComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/UIScrollComponent.h"
#include "Components/ViewportInfo.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Events/AudioPlayEvent.h"
#include "Events/GamepadButtonEvent.h"
#include "Events/KeyInputEvent.h"
#include "Events/MouseInputEvent.h"
#include "Events/MouseWheelEvent.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "Renderer/RenderQueue.h"
#include "Systems/InputSystem.h"
#include "Systems/KDTreeCollisionSystem.h"
#include "Systems/RenderUIPrimitiveSystem.h"

class UIButtonSystem {
 public:
  void Init(Registry* registry, const std::unique_ptr<EventBus>& eventBus) {
    registry_ = registry;
    event_bus_ = eventBus.get();
    if (registry_) {
      mouseQuery_ =
          registry_->CreateQuery<UIButtonComponent, Opt<GlobalTransformComponent>, Opt<BoxColliderComponent>>();
      buttonQuery_ = registry_->CreateQuery<UIButtonComponent>();
      focusQuery_ = registry_->CreateQuery<UIFocusComponent>();
      scrollQuery_ = registry_->CreateQuery<UIRectComponent, UIScrollComponent>();
    }
    subscriptions_.clear();
    subscriptions_.push_back(
        eventBus->SubscribeEvent<UIButtonSystem, MouseInputEvent>(this, &UIButtonSystem::OnMouseInput));
    subscriptions_.push_back(
        eventBus->SubscribeEvent<UIButtonSystem, KeyInputEvent>(this, &UIButtonSystem::OnKeyInput));
    subscriptions_.push_back(
        eventBus->SubscribeEvent<UIButtonSystem, GamepadButtonEvent>(this, &UIButtonSystem::OnGamepadButton));
    subscriptions_.push_back(
        eventBus->SubscribeEvent<UIButtonSystem, MouseWheelEvent>(this, &UIButtonSystem::OnMouseWheel));
  }

  void OnMouseInput(const MouseInputEvent& event) {
    const auto mousePos = GetTransformedMousePos(event);
    if (!mousePos || !EnsureMouseQuery()) {
      return;
    }
    lastMousePos_ = *mousePos;

    const auto* cameraComp = registry_->TryGet<CameraComponent>();
    const octarine::Rect camera = cameraComp ? cameraComp->viewport : octarine::Rect{};
    const float zoom = (cameraComp && cameraComp->zoom > 0.001f) ? cameraComp->zoom : 1.0f;

    if (event.event.type == SDL_EVENT_MOUSE_MOTION) {
      UpdateMouseHover(camera, zoom, *mousePos);
      return;
    }
    if (event.event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.event.button == 1) {
      ClearPressedState();
      return;
    }
    if (event.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.event.button == 1) {
      HandleMousePress(camera, zoom, *mousePos);
    }
  }

  void OnMouseWheel(const MouseWheelEvent& event) {
    if (!registry_) return;
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isHovered) return;
    if (!scrollQuery_) {
      scrollQuery_ = registry_->CreateQuery<UIRectComponent, UIScrollComponent>();
    }
    scrollQuery_->Update();

    Entity bestEntity{static_cast<EcsId>(-1)};
    int bestLayer = std::numeric_limits<int>::min();
    scrollQuery_->ForEach([&](const Entity entity, const UIRectComponent& rect, UIScrollComponent& /*scroll*/) {
      if (!rect.visible || !ContainsPointInRect(rect, lastMousePos_)) return;
      if (rect.layer >= bestLayer) {
        bestLayer = rect.layer;
        bestEntity = entity;
      }
    });
    if (bestEntity.GetId() != static_cast<std::uint32_t>(-1) && registry_->IsAlive(bestEntity)) {
      auto& scroll = registry_->GetComponent<UIScrollComponent>(bestEntity);
      const auto& rect = registry_->GetComponent<UIRectComponent>(bestEntity);
      if (scroll.scrollVertical) scroll.scrollY -= event.dy * scroll.wheelStep;
      if (scroll.scrollHorizontal) scroll.scrollX -= event.dx * scroll.wheelStep;
      scroll.ClampToViewport(rect.Width(), rect.Height());
    }
  }

  void OnKeyInput(const KeyInputEvent& event) {
    if (!event.isPressed || !registry_) {
      return;
    }

    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isFocused) {
      return;
    }

    if (HandleFocusKeyNavigation(event.inputKey, event.inputModifier)) {
      return;
    }

    const std::string key = InputSystem::MakeKey(SDL_GetKeyName(event.inputKey));
    if (key.empty()) {
      return;
    }

    if (!buttonQuery_ && registry_) {
      buttonQuery_ = registry_->CreateQuery<UIButtonComponent>();
    }
    if (!buttonQuery_) return;
    buttonQuery_->Update();

    auto* inputSystem = registry_->TryGet<InputSystem>();

    auto handler = [&](Entity entity, UIButtonComponent& button) {
      if (!button.isActive || !IsButtonVisible(entity)) {
        return;
      }

      const bool matchedKey = MatchesKeyString(button.key, key, inputSystem);
      const bool matchedAction = MatchesActionKey(button.action, key, inputSystem);

      if (matchedKey || matchedAction) {
        TriggerButton(entity, button);
      }
    };
    buttonQuery_->ForEach(handler);
  }

  void OnGamepadButton(const GamepadButtonEvent& event) {
    if (!event.isPressed || !registry_) {
      return;
    }

    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isFocused) {
      return;
    }

    if (HandleFocusGamepadNavigation(event.button)) {
      return;
    }

    const std::string& buttonName = event.buttonName;
    if (buttonName.empty()) {
      return;
    }

    if (!buttonQuery_ && registry_) {
      buttonQuery_ = registry_->CreateQuery<UIButtonComponent>();
    }
    if (!buttonQuery_) return;
    buttonQuery_->Update();

    auto* inputSystem = registry_->TryGet<InputSystem>();

    auto handler = [&](Entity entity, UIButtonComponent& button) {
      if (!button.isActive || !IsButtonVisible(entity)) {
        return;
      }

      const bool matchedController = MatchesGamepadString(button.controllerButton, buttonName, inputSystem);
      const bool matchedAction = MatchesActionGamepad(button.action, buttonName, inputSystem);

      if (matchedController || matchedAction) {
        TriggerButton(entity, button);
      }
    };
    buttonQuery_->ForEach(handler);
  }

  void TriggerButton(Entity entity, UIButtonComponent& button) {
    if (!button.isActive) return;

    EmitButtonSound(button.clickSound);
    if (button.clickFunction != sol::lua_nil) {
      sol::protected_function clickFn = button.clickFunction;
      sol::object btnTable = button.buttonTable ? sol::object(*button.buttonTable) : sol::object(sol::lua_nil);
      auto result = clickFn(btnTable, entity);
      if (!result.valid()) {
        const sol::error err = result;
        Logger::ErrorLua(std::string(err.what()));
      }
    }
  }

  void SetFocus(const Entity target) {
    if (!EnsureFocusQuery()) return;
    const Entity prevFocused = GetFocusedEntity();
    focusQuery_->ForEach([&](const Entity entity, UIFocusComponent& focus) {
      focus.isFocused = (entity == target && focus.isFocusable && !focus.isDisabled);
    });
    if (target != prevFocused && registry_ && registry_->IsAlive(target) &&
        registry_->HasComponent<UIFocusComponent>(target) &&
        registry_->GetComponent<UIFocusComponent>(target).isFocused &&
        registry_->HasComponent<UIButtonComponent>(target)) {
      EmitButtonSound(registry_->GetComponent<UIButtonComponent>(target).focusSound);
    }
  }

  [[nodiscard]] Entity GetFocusedEntity() {
    if (!EnsureFocusQuery()) return Entity(static_cast<EcsId>(-1));
    Entity current{static_cast<EcsId>(-1)};
    focusQuery_->ForEach([&](const Entity entity, const UIFocusComponent& focus) {
      if (focus.isFocused && focus.isFocusable && !focus.isDisabled && IsButtonVisible(entity)) {
        current = entity;
      }
    });
    return current;
  }

  bool NavigateDirection(const glm::vec2 dir) {
    if (!EnsureFocusQuery()) return false;
    const Entity current = GetFocusedEntity();
    if (current.GetId() == static_cast<std::uint32_t>(-1)) {
      return CycleFocusTab(false);
    }

    const auto& curFocus = registry_->GetComponent<UIFocusComponent>(current);
    Entity explicitNext{static_cast<EcsId>(-1)};
    if (dir.y < -0.5f) explicitNext = curFocus.navUp;
    if (dir.y > 0.5f) explicitNext = curFocus.navDown;
    if (dir.x < -0.5f) explicitNext = curFocus.navLeft;
    if (dir.x > 0.5f) explicitNext = curFocus.navRight;
    if (explicitNext.GetId() != static_cast<std::uint32_t>(-1) && registry_->IsAlive(explicitNext)) {
      SetFocus(explicitNext);
      return true;
    }

    if (!registry_->HasComponent<UIRectComponent>(current)) return false;
    const glm::vec2 curCenter = registry_->GetComponent<UIRectComponent>(current).Center();

    Entity best{static_cast<EcsId>(-1)};
    float bestScore = std::numeric_limits<float>::max();
    focusQuery_->ForEach([&](const Entity candidate, const UIFocusComponent& foc) {
      if (candidate == current || !foc.isFocusable || foc.isDisabled || !IsButtonVisible(candidate)) return;
      if (!registry_->HasComponent<UIRectComponent>(candidate)) return;
      const glm::vec2 delta = registry_->GetComponent<UIRectComponent>(candidate).Center() - curCenter;
      const float proj = delta.x * dir.x + delta.y * dir.y;
      if (proj <= 1.0f) return;
      const float perp = std::abs(delta.x * dir.y - delta.y * dir.x);
      const float score = proj + perp * 2.5f;
      if (score < bestScore) {
        bestScore = score;
        best = candidate;
      }
    });

    if (best.GetId() != static_cast<std::uint32_t>(-1)) {
      SetFocus(best);
      return true;
    }
    return false;
  }

  bool CycleFocusTab(const bool reverse) {
    if (!EnsureFocusQuery()) return false;
    std::vector<std::pair<std::int32_t, Entity>> candidates;
    focusQuery_->ForEach([&](const Entity entity, const UIFocusComponent& foc) {
      if (foc.isFocusable && !foc.isDisabled && IsButtonVisible(entity)) {
        candidates.emplace_back(foc.tabIndex, entity);
      }
    });
    if (candidates.empty()) return false;
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    const Entity current = GetFocusedEntity();
    size_t nextIdx = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
      if (candidates[i].second == current) {
        nextIdx = reverse ? ((i + candidates.size() - 1) % candidates.size()) : ((i + 1) % candidates.size());
        break;
      }
    }
    SetFocus(candidates[nextIdx].second);
    return true;
  }

 private:
  [[nodiscard]] bool IsButtonVisible(const Entity entity) const {
    if (!registry_ || !registry_->IsAlive(entity)) return false;
    if (registry_->HasComponent<UIRectComponent>(entity)) {
      return registry_->GetComponent<UIRectComponent>(entity).visible;
    }
    return true;
  }

  [[nodiscard]] int ComputeEntitySortLayer(const Entity entity) const {
    if (registry_ && registry_->HasComponent<UIRectComponent>(entity)) {
      return registry_->GetComponent<UIRectComponent>(entity).layer;
    }
    return 0;
  }

  [[nodiscard]] Entity FindTopmostHit(const octarine::Rect& camera, const float zoom, const glm::vec2 mousePos) {
    Entity topEntity{static_cast<EcsId>(-1)};
    int topLayer = std::numeric_limits<int>::min();
    mouseQuery_->ForEach([&](const Entity entity, UIButtonComponent& button, const GlobalTransformComponent* transform,
                             const BoxColliderComponent* collider) {
      if (!button.isActive || !IsButtonVisible(entity)) return;
      if (!HitTest(entity, button, transform, collider, camera, zoom, mousePos.x, mousePos.y)) return;
      const int layer = ComputeEntitySortLayer(entity);
      if (layer >= topLayer) {
        topLayer = layer;
        topEntity = entity;
      }
    });
    return topEntity;
  }

  void EmitButtonSound(const std::string& clipId) const {
    if (!event_bus_ || clipId.empty()) return;
    event_bus_->EmitEvent<AudioPlayEvent>(clipId, 1.0f);
  }

  void UpdateMouseHover(const octarine::Rect& camera, const float zoom, const glm::vec2 mousePos) {
    const Entity topHit = FindTopmostHit(camera, zoom, mousePos);
    mouseQuery_->ForEach([&](const Entity entity, UIButtonComponent& button,
                             const GlobalTransformComponent* /*transform*/, const BoxColliderComponent* /*collider*/) {
      if (!button.isActive || !IsButtonVisible(entity)) {
        button.isHovered = false;
        button.isPressed = false;
        return;
      }
      const bool wasHovered = button.isHovered;
      const bool hit = (entity == topHit);
      button.isHovered = hit;
      if (!wasHovered && hit) {
        EmitButtonSound(button.hoverSound);
      }
      if (!hit) button.isPressed = false;
    });
  }

  void ClearPressedState() {
    mouseQuery_->ForEach([&](Entity /*entity*/, UIButtonComponent& button,
                             const GlobalTransformComponent* /*transform*/,
                             const BoxColliderComponent* /*collider*/) { button.isPressed = false; });
  }

  void HandleMousePress(const octarine::Rect& camera, const float zoom, const glm::vec2 mousePos) {
    const Entity topHit = FindTopmostHit(camera, zoom, mousePos);
    if (topHit.GetId() == static_cast<std::uint32_t>(-1) || !registry_->IsAlive(topHit)) return;
    if (registry_->HasComponent<UIFocusComponent>(topHit)) {
      SetFocus(topHit);
    }
    auto& button = registry_->GetComponent<UIButtonComponent>(topHit);
    button.isHovered = true;
    button.isPressed = true;
    TriggerButton(topHit, button);
  }

  bool TriggerFocusedButton(const bool requireNoKeyBinding, const bool requireNoControllerBinding) {
    const Entity focused = GetFocusedEntity();
    if (focused.GetId() == static_cast<std::uint32_t>(-1) || !registry_->HasComponent<UIButtonComponent>(focused)) {
      return false;
    }
    auto& btn = registry_->GetComponent<UIButtonComponent>(focused);
    if (!btn.action.empty()) return false;
    if (requireNoKeyBinding && !btn.key.empty()) return false;
    if (requireNoControllerBinding && !btn.controllerButton.empty()) return false;
    TriggerButton(focused, btn);
    return true;
  }

  bool HandleFocusKeyNavigation(const SDL_Keycode keycode, const SDL_Keymod mod) {
    if (!EnsureFocusQuery() || !HasAnyFocusable()) return false;
    if (keycode == SDLK_TAB) return CycleFocusTab((mod & SDL_KMOD_SHIFT) != 0);
    if (keycode == SDLK_UP) return NavigateDirection({0.0f, -1.0f});
    if (keycode == SDLK_DOWN) return NavigateDirection({0.0f, 1.0f});
    if (keycode == SDLK_LEFT) return NavigateDirection({-1.0f, 0.0f});
    if (keycode == SDLK_RIGHT) return NavigateDirection({1.0f, 0.0f});
    if (keycode == SDLK_RETURN || keycode == SDLK_SPACE) {
      return TriggerFocusedButton(true, false);
    }
    return false;
  }

  bool HandleFocusGamepadNavigation(const SDL_GamepadButton btn) {
    if (!EnsureFocusQuery() || !HasAnyFocusable()) return false;
    if (btn == SDL_GAMEPAD_BUTTON_DPAD_UP) return NavigateDirection({0.0f, -1.0f});
    if (btn == SDL_GAMEPAD_BUTTON_DPAD_DOWN) return NavigateDirection({0.0f, 1.0f});
    if (btn == SDL_GAMEPAD_BUTTON_DPAD_LEFT) return NavigateDirection({-1.0f, 0.0f});
    if (btn == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) return NavigateDirection({1.0f, 0.0f});
    if (btn == SDL_GAMEPAD_BUTTON_SOUTH) {
      return TriggerFocusedButton(false, true);
    }
    return false;
  }

  bool EnsureFocusQuery() {
    if (!focusQuery_ && registry_) {
      focusQuery_ = registry_->CreateQuery<UIFocusComponent>();
    }
    if (!focusQuery_) return false;
    focusQuery_->Update();
    return true;
  }

  [[nodiscard]] bool HasAnyFocusable() const {
    bool any = false;
    if (!focusQuery_) return false;
    focusQuery_->ForEach([&](const Entity /*e*/, const UIFocusComponent& f) {
      if (f.isFocusable && !f.isDisabled) any = true;
    });
    return any;
  }

  [[nodiscard]] std::optional<glm::vec2> GetTransformedMousePos(const MouseInputEvent& event) const {
    if (!registry_) {
      return glm::vec2{event.event.x, event.event.y};
    }
    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isHovered) {
      return std::nullopt;
    }
    const auto* config = registry_->TryGet<GameConfig>();
    if (viewport && config) {
      return viewport->TransformCoordinates(event.event.x, event.event.y, config->windowWidth, config->windowHeight);
    }
    return glm::vec2{event.event.x, event.event.y};
  }

  bool EnsureMouseQuery() {
    if (!mouseQuery_ && registry_) {
      mouseQuery_ =
          registry_->CreateQuery<UIButtonComponent, Opt<GlobalTransformComponent>, Opt<BoxColliderComponent>>();
    }
    if (!mouseQuery_) {
      return false;
    }
    mouseQuery_->Update();
    return true;
  }

  template <typename Fn>
  static bool ForEachCommaToken(const std::string& spec, Fn&& fn) {
    if (spec.empty()) return false;
    size_t start = 0;
    while (start < spec.size()) {
      size_t end = spec.find(',', start);
      if (end == std::string::npos) end = spec.size();
      std::string token = spec.substr(start, end - start);
      const size_t first = token.find_first_not_of(" \t\r\n");
      if (first != std::string::npos) {
        const size_t last = token.find_last_not_of(" \t\r\n");
        token = token.substr(first, last - first + 1);
        if (fn(token)) return true;
      }
      start = end + 1;
    }
    return false;
  }

  static bool MatchesKeyString(const std::string& keySpec, const std::string& keyName, const InputSystem* inputSystem) {
    return ForEachCommaToken(keySpec, [&](const std::string& token) {
      if (inputSystem) {
        return inputSystem->MatchesKeyAlias(token, keyName);
      }
      return InputSystem::MakeKey(token) == InputSystem::MakeKey(keyName);
    });
  }

  static bool MatchesGamepadString(const std::string& buttonSpec, const std::string& buttonName,
                                   const InputSystem* inputSystem) {
    return ForEachCommaToken(buttonSpec, [&](const std::string& token) {
      if (inputSystem) {
        return inputSystem->MatchesGamepadAlias(token, buttonName);
      }
      return InputSystem::MakeKey(token) == InputSystem::MakeKey(buttonName);
    });
  }

  static bool MatchesActionKey(const std::string& actionSpec, const std::string& keyName,
                               const InputSystem* inputSystem) {
    if (!inputSystem) return false;
    return ForEachCommaToken(actionSpec,
                             [&](const std::string& token) { return inputSystem->IsActionBoundToKey(token, keyName); });
  }

  static bool MatchesActionGamepad(const std::string& actionSpec, const std::string& buttonName,
                                   const InputSystem* inputSystem) {
    if (!inputSystem) return false;
    return ForEachCommaToken(actionSpec, [&](const std::string& token) {
      return inputSystem->IsActionBoundToGamepadButton(token, buttonName);
    });
  }

  [[nodiscard]] static bool ContainsPointInCircle(const octarine::ui::ResolvedCircle& c, const glm::vec2 pt) {
    if (c.radiusX <= 0.0f || c.radiusY <= 0.0f) return false;
    const float dx = (pt.x - c.center.x) / c.radiusX;
    const float dy = (pt.y - c.center.y) / c.radiusY;
    return (dx * dx + dy * dy) <= 1.0f;
  }

  [[nodiscard]] static bool ContainsPointInCapsule(const octarine::ui::ResolvedCapsule& c, const glm::vec2 pt) {
    if (c.radius < 0.0f) return false;
    return octarine::collision::PointSegmentDistanceSq(pt, c.start, c.end) <= (c.radius * c.radius);
  }

  [[nodiscard]] static bool ContainsPointInTriangle(const octarine::ui::ResolvedTriangle& t, const glm::vec2 pt) {
    return octarine::collision::PointInTriangle(pt, t.p1, t.p2, t.p3);
  }

  [[nodiscard]] static bool ContainsPointInRect(const UIRectComponent& rect, const glm::vec2 pt) {
    return pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom;
  }

  [[nodiscard]] bool IsInsideActiveClip(const UIRectComponent& rect, const glm::vec2 mouse) const {
    if (rect.clipId == 0 || !registry_) return true;
    const auto* rq = registry_->TryGet<RenderQueue>();
    if (!rq) return true;
    const SDL_Rect* clip = rq->GetClipRect(rect.clipId);
    if (!clip) return true;
    return mouse.x >= static_cast<float>(clip->x) && mouse.x <= static_cast<float>(clip->x + clip->w) &&
           mouse.y >= static_cast<float>(clip->y) && mouse.y <= static_cast<float>(clip->y + clip->h);
  }

  [[nodiscard]] static bool HitTestUIRectBoxCollider(const UIRectComponent& rect, const BoxColliderComponent& box,
                                                     const glm::vec2 mouse) {
    if (box.width <= 0 && box.height <= 0) {
      return ContainsPointInRect(rect, mouse);
    }
    const float x = rect.left + box.offset.x;
    const float y = rect.top + box.offset.y;
    return mouse.x >= x && mouse.x <= x + static_cast<float>(box.width) && mouse.y >= y &&
           mouse.y <= y + static_cast<float>(box.height);
  }

  [[nodiscard]] bool HasAnyCollider(const Entity entity, const BoxColliderComponent* boxCollider) const {
    return boxCollider != nullptr || registry_->HasComponent<CircleColliderComponent>(entity) ||
           registry_->HasComponent<CapsuleColliderComponent>(entity) ||
           registry_->HasComponent<TriangleColliderComponent>(entity);
  }

  [[nodiscard]] bool HitTestUIRectCollider(const Entity entity, const UIRectComponent& rect,
                                           const BoxColliderComponent* boxCollider, const glm::vec2 mouse) const {
    if (boxCollider && HitTestUIRectBoxCollider(rect, *boxCollider, mouse)) {
      return true;
    }
    if (registry_->HasComponent<CircleColliderComponent>(entity)) {
      const auto& c = registry_->GetComponent<CircleColliderComponent>(entity);
      if (ContainsPointInCircle(octarine::ui::ResolveCircle(rect, c.offset, c.radius), mouse)) return true;
    }
    if (registry_->HasComponent<CapsuleColliderComponent>(entity)) {
      const auto& c = registry_->GetComponent<CapsuleColliderComponent>(entity);
      if (ContainsPointInCapsule(octarine::ui::ResolveCapsule(rect, c.start, c.end, c.radius), mouse)) return true;
    }
    if (registry_->HasComponent<TriangleColliderComponent>(entity)) {
      const auto& c = registry_->GetComponent<TriangleColliderComponent>(entity);
      if (ContainsPointInTriangle(octarine::ui::ResolveTriangle(rect, c.p1, c.p2, c.p3), mouse)) return true;
    }
    return false;
  }

  [[nodiscard]] bool HitTestUIRect(const Entity entity, const UIRectComponent& rect,
                                   const BoxColliderComponent* boxCollider, const glm::vec2 mouse) const {
    if (!rect.visible || !IsInsideActiveClip(rect, mouse)) {
      return false;
    }
    if (HasAnyCollider(entity, boxCollider)) {
      return HitTestUIRectCollider(entity, rect, boxCollider, mouse);
    }
    if (registry_->HasComponent<CirclePrimitiveComponent>(entity)) {
      const auto& c = registry_->GetComponent<CirclePrimitiveComponent>(entity);
      return ContainsPointInCircle(octarine::ui::ResolveCircle(rect, c.position, c.radius), mouse);
    }
    if (registry_->HasComponent<CapsulePrimitiveComponent>(entity)) {
      const auto& c = registry_->GetComponent<CapsulePrimitiveComponent>(entity);
      return ContainsPointInCapsule(octarine::ui::ResolveCapsule(rect, c.start, c.end, c.radius), mouse);
    }
    if (registry_->HasComponent<TrianglePrimitiveComponent>(entity)) {
      const auto& c = registry_->GetComponent<TrianglePrimitiveComponent>(entity);
      return ContainsPointInTriangle(octarine::ui::ResolveTriangle(rect, c.p1, c.p2, c.p3), mouse);
    }
    return ContainsPointInRect(rect, mouse);
  }

  template <typename ColliderT>
  [[nodiscard]] bool HitTestCollider(const Entity entity, const GlobalTransformComponent& transform,
                                     const glm::vec2 worldMouse) const {
    if (!registry_->HasComponent<ColliderT>(entity)) return false;
    const auto& c = registry_->GetComponent<ColliderT>(entity);
    return KDTreeCollisionSystem::BuildBox(entity, transform, c, EntityMaskComponent{}).containsPoint(worldMouse);
  }

  [[nodiscard]] bool HitTestTransformPrimitive(const Entity entity, const GlobalTransformComponent& transform,
                                               const glm::vec2 worldMouse) const {
    if (registry_->HasComponent<SquarePrimitiveComponent>(entity)) {
      const auto& s = registry_->GetComponent<SquarePrimitiveComponent>(entity);
      const BoxColliderComponent col(static_cast<int>(s.width), static_cast<int>(s.height), s.position, s.isFixed);
      if (KDTreeCollisionSystem::BuildBox(entity, transform, col, EntityMaskComponent{}).containsPoint(worldMouse)) {
        return true;
      }
    }
    if (registry_->HasComponent<CirclePrimitiveComponent>(entity)) {
      const auto& c = registry_->GetComponent<CirclePrimitiveComponent>(entity);
      const CircleColliderComponent col(c.radius, c.position, c.isFixed);
      if (KDTreeCollisionSystem::BuildBox(entity, transform, col, EntityMaskComponent{}).containsPoint(worldMouse)) {
        return true;
      }
    }
    if (registry_->HasComponent<CapsulePrimitiveComponent>(entity)) {
      const auto& c = registry_->GetComponent<CapsulePrimitiveComponent>(entity);
      const CapsuleColliderComponent col(c.start, c.end, c.radius, c.isFixed);
      if (KDTreeCollisionSystem::BuildBox(entity, transform, col, EntityMaskComponent{}).containsPoint(worldMouse)) {
        return true;
      }
    }
    if (registry_->HasComponent<TrianglePrimitiveComponent>(entity)) {
      const auto& t = registry_->GetComponent<TrianglePrimitiveComponent>(entity);
      const TriangleColliderComponent col(t.p1, t.p2, t.p3, t.isFixed);
      if (KDTreeCollisionSystem::BuildBox(entity, transform, col, EntityMaskComponent{}).containsPoint(worldMouse)) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool HitTest(const Entity entity, const UIButtonComponent& button,
                             const GlobalTransformComponent* transform, const BoxColliderComponent* collider,
                             const octarine::Rect& camera, const float zoom, const float mouseX,
                             const float mouseY) const {
    if (registry_->HasComponent<UIRectComponent>(entity)) {
      return HitTestUIRect(entity, registry_->GetComponent<UIRectComponent>(entity), collider, {mouseX, mouseY});
    }
    if (!transform) return false;
    const glm::vec2 worldMouse =
        button.isFixed ? glm::vec2{mouseX, mouseY} : glm::vec2{mouseX / zoom + camera.x, mouseY / zoom + camera.y};
    if (HasAnyCollider(entity, collider)) {
      if (collider && KDTreeCollisionSystem::BuildBox(entity, *transform, *collider, EntityMaskComponent{})
                          .containsPoint(worldMouse)) {
        return true;
      }
      return HitTestCollider<CircleColliderComponent>(entity, *transform, worldMouse) ||
             HitTestCollider<CapsuleColliderComponent>(entity, *transform, worldMouse) ||
             HitTestCollider<TriangleColliderComponent>(entity, *transform, worldMouse);
    }
    return HitTestTransformPrimitive(entity, *transform, worldMouse);
  }

  using MouseQuery = ComponentQuery<UIButtonComponent, Opt<GlobalTransformComponent>, Opt<BoxColliderComponent>>;
  using ButtonQuery = ComponentQuery<UIButtonComponent>;
  using FocusQuery = ComponentQuery<UIFocusComponent>;
  using ScrollQuery = ComponentQuery<UIRectComponent, UIScrollComponent>;

  Registry* registry_ = nullptr;
  EventBus* event_bus_ = nullptr;
  std::unique_ptr<MouseQuery> mouseQuery_ = nullptr;
  std::unique_ptr<ButtonQuery> buttonQuery_ = nullptr;
  std::unique_ptr<FocusQuery> focusQuery_ = nullptr;
  std::unique_ptr<ScrollQuery> scrollQuery_ = nullptr;
  glm::vec2 lastMousePos_{0.0f, 0.0f};
  std::vector<EventBus::SubscriptionHandle> subscriptions_{};
};
