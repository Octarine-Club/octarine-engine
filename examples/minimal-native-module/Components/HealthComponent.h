#pragma once

#include <algorithm>

struct HealthComponent {
  int max_health;
  int current_health;
  int regen_per_second = 0;

  HealthComponent() = delete;

  explicit constexpr HealthComponent(const int maxHealth, const int regenPerSecond = 0) noexcept
      : max_health(std::max(1, maxHealth)), current_health(std::max(1, maxHealth)), regen_per_second(regenPerSecond) {}

  constexpr void Damage(const int amount) noexcept {
    if (amount <= 0) {
      return;
    }
    current_health = std::max(0, current_health - amount);
  }

  constexpr void Heal(const int amount) noexcept {
    if (amount <= 0) {
      return;
    }
    current_health = std::min(max_health, current_health + amount);
  }

  [[nodiscard]] constexpr bool IsDead() const noexcept { return current_health <= 0; }
};
