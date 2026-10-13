#pragma once

struct UICanvasComponent {
  float width = 0.f;   // 0 = use viewport width at layout time
  float height = 0.f;  // 0 = use viewport height at layout time
  int baseLayer = 0;   // base render layer for all entities in this canvas
  bool isFixed = true;

  explicit UICanvasComponent(const bool t_isFixed = true, const float t_width = 0.f, const float t_height = 0.f,
                             const int t_baseLayer = 0)
      : width(t_width), height(t_height), baseLayer(t_baseLayer), isFixed(t_isFixed) {}
};
