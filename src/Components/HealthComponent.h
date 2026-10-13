#pragma once

#include <algorithm>
#include <array>

#include "ECS/Entity.h"

struct WeaponContact {
  Entity weapon_entity{};
  float timer = 0.0f;
};

struct HealthComponent {
  std::array<WeaponContact, 8> active_contacts{};
  int current_health;
  int max_health;

  // For hit-flash logic
  float hit_flash_timer = 0.0f;
  unsigned char base_color_r = 255;
  unsigned char base_color_g = 255;
  unsigned char base_color_b = 255;
  unsigned char base_color_a = 255;

  uint8_t active_contacts_count = 0;
  bool dash_only = false;

  HealthComponent() = delete;

  explicit HealthComponent(const int t_maxHealth) : current_health(t_maxHealth), max_health(t_maxHealth) {}

  HealthComponent(const int t_maxHealth, const int t_currentHealth)
      : current_health(t_currentHealth), max_health(t_maxHealth) {}

  void Damage(const int amount) { current_health = std::max(0, current_health - amount); }
  void Heal(const int amount) { current_health = std::min(max_health, current_health + amount); }
  [[nodiscard]] bool IsDead() const { return current_health <= 0; }
  [[nodiscard]] float Fraction() const {
    return max_health > 0 ? static_cast<float>(current_health) / static_cast<float>(max_health) : 0.0F;
  }
};
