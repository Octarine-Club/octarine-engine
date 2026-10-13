#pragma once

struct WeaponZoneComponent {
  float damage = 1.0f;
  int pierce = 1;  // -1 for infinite pierce
  float rehit_cooldown = 0.25f;
  bool is_dash_weapon = false;
  bool consumed = false;
};
