#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <sol/sol.hpp>
#include <string>
#include <vector>

#include "Components/BoxColliderComponent.h"
#include "Components/CameraComponents.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/UIButtonComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/ViewportInfo.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Events/GamepadButtonEvent.h"
#include "Events/KeyInputEvent.h"
#include "Events/MouseInputEvent.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "Systems/InputSystem.h"
#include "Systems/KDTreeCollisionSystem.h"

class UIButtonSystem {
 public:
  void Init(Registry* registry, const std::unique_ptr<EventBus>& eventBus) {
    registry_ = registry;
    if (registry_) {
      mouseQuery_ =
          registry_->CreateQuery<UIButtonComponent, Opt<GlobalTransformComponent>, Opt<BoxColliderComponent>>();
      buttonQuery_ = registry_->CreateQuery<UIButtonComponent>();
    }
    subscriptions_.clear();
    subscriptions_.push_back(
        eventBus->SubscribeEvent<UIButtonSystem, MouseInputEvent>(this, &UIButtonSystem::OnMouseInput));
    subscriptions_.push_back(
        eventBus->SubscribeEvent<UIButtonSystem, KeyInputEvent>(this, &UIButtonSystem::OnKeyInput));
    subscriptions_.push_back(
        eventBus->SubscribeEvent<UIButtonSystem, GamepadButtonEvent>(this, &UIButtonSystem::OnGamepadButton));
  }

  void OnMouseInput(const MouseInputEvent& event) {
    if (event.event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.event.button != 1) {
      return;
    }

    const auto mousePos = GetTransformedMousePos(event);
    if (!mousePos || !EnsureMouseQuery()) {
      return;
    }

    const auto* cameraComp = registry_->TryGet<CameraComponent>();
    const octarine::Rect camera = cameraComp ? cameraComp->viewport : octarine::Rect{};
    const float zoom = (cameraComp && cameraComp->zoom > 0.001f) ? cameraComp->zoom : 1.0f;
    auto handler = [&](Entity entity, UIButtonComponent& button, const GlobalTransformComponent* transform,
                       const BoxColliderComponent* collider) {
      if (!button.isActive) {
        return;
      }
      if (!HitTest(entity, button, transform, collider, camera, zoom, mousePos->x, mousePos->y)) {
        return;
      }
      TriggerButton(entity, button);
    };
    mouseQuery_->ForEach(handler);
  }

  void OnKeyInput(const KeyInputEvent& event) {
    if (!event.isPressed || !registry_) {
      return;
    }

    const auto* viewport = registry_->TryGet<ViewportInfo>();
    if (viewport && !viewport->isFocused) {
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
      if (!button.isActive) {
        return;
      }

      bool matchedKey = MatchesKeyString(button.key, key, inputSystem);
      bool matchedAction = MatchesActionKey(button.action, key, inputSystem);

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
      if (!button.isActive) {
        return;
      }

      bool matchedController = MatchesGamepadString(button.controllerButton, buttonName, inputSystem);
      bool matchedAction = MatchesActionGamepad(button.action, buttonName, inputSystem);

      if (matchedController || matchedAction) {
        TriggerButton(entity, button);
      }
    };
    buttonQuery_->ForEach(handler);
  }

  void TriggerButton(Entity entity, UIButtonComponent& button) {
    if (!button.isActive) return;

    if (button.clickFunction != sol::lua_nil) {
      auto btnTable = button.buttonTable ? *button.buttonTable : sol::lua_nil;
      auto result = button.clickFunction(btnTable, entity);
      if (!result.valid()) {
        const sol::error err = result;
        Logger::ErrorLua(std::string(err.what()));
      }
    }
  }

 private:
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

  template <typename ColliderT>
  [[nodiscard]] bool HitTestCollider(const Entity entity, const GlobalTransformComponent& transform,
                                     const glm::vec2 worldMouse) const {
    if (!registry_->HasComponent<ColliderT>(entity)) return false;
    const auto& c = registry_->GetComponent<ColliderT>(entity);
    return KDTreeCollisionSystem::BuildBox(entity, transform, c, EntityMaskComponent{}).containsPoint(worldMouse);
  }

  [[nodiscard]] bool HitTest(const Entity entity, const UIButtonComponent& button,
                             const GlobalTransformComponent* transform, const BoxColliderComponent* collider,
                             const octarine::Rect& camera, const float zoom, const float mouseX,
                             const float mouseY) const {
    // Layout-driven UI button: the resolved rect already accounts for anchoring, so no collider or
    // transform is needed — this is the preferred path for `ui_button` + `ui_anchor` entities.
    if (registry_->HasComponent<UIRectComponent>(entity)) {
      const auto& rect = registry_->GetComponent<UIRectComponent>(entity);
      return mouseX >= rect.left && mouseX <= rect.right && mouseY >= rect.top && mouseY <= rect.bottom;
    }
    if (!transform) return false;
    const glm::vec2 worldMouse =
        button.isFixed ? glm::vec2{mouseX, mouseY} : glm::vec2{mouseX / zoom + camera.x, mouseY / zoom + camera.y};
    if (collider && KDTreeCollisionSystem::BuildBox(entity, *transform, *collider, EntityMaskComponent{})
                        .containsPoint(worldMouse)) {
      return true;
    }
    return HitTestCollider<CircleColliderComponent>(entity, *transform, worldMouse) ||
           HitTestCollider<CapsuleColliderComponent>(entity, *transform, worldMouse) ||
           HitTestCollider<TriangleColliderComponent>(entity, *transform, worldMouse);
  }

  using MouseQuery = ComponentQuery<UIButtonComponent, Opt<GlobalTransformComponent>, Opt<BoxColliderComponent>>;
  using ButtonQuery = ComponentQuery<UIButtonComponent>;

  Registry* registry_ = nullptr;
  std::unique_ptr<MouseQuery> mouseQuery_ = nullptr;
  std::unique_ptr<ButtonQuery> buttonQuery_ = nullptr;
  std::vector<EventBus::SubscriptionHandle> subscriptions_;
};
