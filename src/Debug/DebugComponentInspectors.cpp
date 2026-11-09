#include "Debug/DebugComponentInspectors.h"

#ifdef OCTARINE_WITH_IMGUI
#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "Components/AnimationComponent.h"
#include "Components/AudioListenerComponent.h"
#include "Components/AudioSourceComponent.h"
#include "Components/BoxColliderComponent.h"
#include "Components/CameraComponents.h"
#include "Components/CameraFollowComponent.h"
#include "Components/CapsuleColliderComponent.h"
#include "Components/CapsulePrimitiveComponent.h"
#include "Components/CircleColliderComponent.h"
#include "Components/CirclePrimitiveComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/LifetimeComponent.h"
#include "Components/LineColliderComponent.h"
#include "Components/LinePrimitiveComponent.h"
#include "Components/NameComponent.h"
#include "Components/PivotComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/RotationComponent.h"
#include "Components/ScaleComponent.h"
#include "Components/ScriptComponent.h"
#include "Components/SpriteComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "Components/TextLabelComponent.h"
#include "Components/TriangleColliderComponent.h"
#include "Components/TrianglePrimitiveComponent.h"
#include "Components/UIButtonComponent.h"
#include "Components/UIFlexComponent.h"
#include "Components/UILayoutBoxComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/UIScrollComponent.h"
#include "Components/UIZIndexComponent.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#endif

namespace octarine::debug {

std::vector<ComponentInspectorEntry>& DebugComponentInspectorRegistry::MutableAll() {
  static std::vector<ComponentInspectorEntry> entries;
  return entries;
}

const std::vector<ComponentInspectorEntry>& DebugComponentInspectorRegistry::All() { return MutableAll(); }

#ifdef OCTARINE_WITH_IMGUI
namespace {

void EditColor(const char* label, octarine::Color& col) {
  std::array<float, 4> c = {static_cast<float>(col.r) / 255.0f, static_cast<float>(col.g) / 255.0f,
                            static_cast<float>(col.b) / 255.0f, static_cast<float>(col.a) / 255.0f};
  if (ImGui::ColorEdit4(label, c.data())) {
    col.r = static_cast<uint8_t>(std::clamp(c[0] * 255.0f, 0.0f, 255.0f));
    col.g = static_cast<uint8_t>(std::clamp(c[1] * 255.0f, 0.0f, 255.0f));
    col.b = static_cast<uint8_t>(std::clamp(c[2] * 255.0f, 0.0f, 255.0f));
    col.a = static_cast<uint8_t>(std::clamp(c[3] * 255.0f, 0.0f, 255.0f));
  }
}

void RegisterTransformInspectors() {
  DebugComponentInspectorRegistry::Register<NameComponent>(
      "Name", [](Registry&, Entity, NameComponent& comp) { ImGui::InputText("Name", &comp.name); });

  DebugComponentInspectorRegistry::Register<PositionComponent>(
      "Position",
      [](Registry&, Entity, PositionComponent& comp) { ImGui::DragFloat2("Position", &comp.value.x, 1.0f); });

  DebugComponentInspectorRegistry::Register<RotationComponent>("Rotation",
                                                               [](Registry&, Entity, RotationComponent& comp) {
                                                                 float deg = glm::degrees(comp.value);
                                                                 if (ImGui::DragFloat("Angle (deg)", &deg, 1.0f)) {
                                                                   comp.value = glm::radians(deg);
                                                                 }
                                                               });

  DebugComponentInspectorRegistry::Register<ScaleComponent>(
      "Scale", [](Registry&, Entity, ScaleComponent& comp) { ImGui::DragFloat2("Scale", &comp.value.x, 0.05f); });

  DebugComponentInspectorRegistry::Register<PivotComponent>("Pivot", [](Registry&, Entity, PivotComponent& comp) {
    ImGui::DragFloat2("Pivot (norm 0..1)", &comp.value.x, 0.05f);
  });

  DebugComponentInspectorRegistry::Register<GlobalTransformComponent>(
      "Global Transform",
      [](Registry&, Entity, GlobalTransformComponent& comp) {
        ImGui::Text("TRS Position: (%.1f, %.1f)", static_cast<double>(comp.position.x),
                    static_cast<double>(comp.position.y));
        ImGui::Text("TRS Rotation: %.2f deg", static_cast<double>(glm::degrees(comp.rotation)));
        ImGui::Text("TRS Scale: (%.2f, %.2f)", static_cast<double>(comp.scale.x), static_cast<double>(comp.scale.y));
        ImGui::Text("TRS Pivot: (%.1f, %.1f)", static_cast<double>(comp.pivot.x), static_cast<double>(comp.pivot.y));
      },
      /*allowAddRemove=*/false);
}

void RegisterPhysicsInspectors() {
  DebugComponentInspectorRegistry::Register<RigidBodyComponent>(
      "RigidBody",
      [](Registry&, Entity, RigidBodyComponent& comp) { ImGui::DragFloat2("Velocity", &comp.velocity.x, 1.0f); });

  DebugComponentInspectorRegistry::Register<BoxColliderComponent>(
      "Box Collider", [](Registry&, Entity, BoxColliderComponent& comp) {
        ImGui::DragInt("Width", &comp.width, 1.0f, 0, 10000);
        ImGui::DragInt("Height", &comp.height, 1.0f, 0, 10000);
        ImGui::DragFloat2("Offset", &comp.offset.x, 1.0f);
        ImGui::Checkbox("Is Fixed", &comp.isFixed);
      });

  DebugComponentInspectorRegistry::Register<CircleColliderComponent>(
      "Circle Collider", [](Registry&, Entity, CircleColliderComponent& comp) {
        ImGui::DragFloat("Radius", &comp.radius, 0.5f, 0.0f, 5000.0f);
        ImGui::DragFloat2("Offset", &comp.offset.x, 1.0f);
        ImGui::Checkbox("Is Fixed", &comp.isFixed);
      });

  DebugComponentInspectorRegistry::Register<LineColliderComponent>("Line Collider",
                                                                   [](Registry&, Entity, LineColliderComponent& comp) {
                                                                     ImGui::DragFloat2("Start", &comp.start.x, 1.0f);
                                                                     ImGui::DragFloat2("End", &comp.end.x, 1.0f);
                                                                     ImGui::Checkbox("Is Fixed", &comp.isFixed);
                                                                   });

  DebugComponentInspectorRegistry::Register<CapsuleColliderComponent>(
      "Capsule Collider", [](Registry&, Entity, CapsuleColliderComponent& comp) {
        ImGui::DragFloat2("Start", &comp.start.x, 1.0f);
        ImGui::DragFloat2("End", &comp.end.x, 1.0f);
        ImGui::DragFloat("Radius", &comp.radius, 0.5f, 0.0f, 5000.0f);
        ImGui::Checkbox("Is Fixed", &comp.isFixed);
      });

  DebugComponentInspectorRegistry::Register<TriangleColliderComponent>(
      "Triangle Collider", [](Registry&, Entity, TriangleColliderComponent& comp) {
        ImGui::DragFloat2("P1", &comp.p1.x, 1.0f);
        ImGui::DragFloat2("P2", &comp.p2.x, 1.0f);
        ImGui::DragFloat2("P3", &comp.p3.x, 1.0f);
        ImGui::Checkbox("Is Fixed", &comp.isFixed);
      });
}

void RegisterRenderInspectors() {
  DebugComponentInspectorRegistry::Register<SpriteComponent>("Sprite", [](Registry&, Entity, SpriteComponent& comp) {
    ImGui::InputText("Asset ID", &comp.assetId);
    ImGui::DragFloat("Width", &comp.width, 1.0f, 0.0f, 10000.0f);
    ImGui::DragFloat("Height", &comp.height, 1.0f, 0.0f, 10000.0f);
    ImGui::DragInt("Layer / Z", &comp.layer, 1.0f, -100, 100);
    EditColor("Color Tint", comp.colorMod);
    ImGui::Checkbox("Is Fixed", &comp.isFixed);
  });

  DebugComponentInspectorRegistry::Register<AnimationComponent>(
      "Animation", [](Registry&, Entity, AnimationComponent& comp) {
        ImGui::DragInt("Current Frame", &comp.currentFrame, 1.0f, 1, std::max(1, comp.numFrames));
        ImGui::DragInt("Num Frames", &comp.numFrames, 1.0f, 1, 100);
        ImGui::DragInt("Frame Speed", &comp.frameRateSpeed, 1.0f, 1, 60);
        ImGui::Checkbox("Looping", &comp.shouldLoop);
        ImGui::Checkbox("Finished", &comp.isFinished);
      });

  DebugComponentInspectorRegistry::Register<TextLabelComponent>("Text Label",
                                                                [](Registry&, Entity, TextLabelComponent& comp) {
                                                                  ImGui::InputText("Text", &comp.text);
                                                                  ImGui::InputText("Font ID", &comp.fontId);
                                                                  ImGui::DragInt("Layer", &comp.layer);
                                                                  EditColor("Color", comp.color);
                                                                  ImGui::Checkbox("Is Fixed", &comp.isFixed);
                                                                });

  DebugComponentInspectorRegistry::Register<SquarePrimitiveComponent>(
      "Square Primitive", [](Registry&, Entity, SquarePrimitiveComponent& comp) {
        ImGui::DragFloat("Width", &comp.width, 1.0f);
        ImGui::DragFloat("Height", &comp.height, 1.0f);
        ImGui::DragFloat("Border Thickness", &comp.borderThickness, 0.5f);
        ImGui::DragFloat("Corner Radius", &comp.cornerRadius, 0.5f);
        ImGui::SliderFloat("Fill Ratio", &comp.fillRatio, 0.0f, 1.0f);
        EditColor("Color", comp.color);
        ImGui::Checkbox("Is Fixed", &comp.isFixed);
      });

  DebugComponentInspectorRegistry::Register<CirclePrimitiveComponent>(
      "Circle Primitive", [](Registry&, Entity, CirclePrimitiveComponent& comp) {
        ImGui::DragFloat("Radius", &comp.radius, 1.0f);
        ImGui::DragFloat("Thickness", &comp.thickness, 0.5f);
        EditColor("Color", comp.color);
        ImGui::Checkbox("Filled", &comp.filled);
        ImGui::Checkbox("Is Fixed", &comp.isFixed);
      });

  DebugComponentInspectorRegistry::Register<LinePrimitiveComponent>(
      "Line Primitive", [](Registry&, Entity, LinePrimitiveComponent& comp) {
        ImGui::DragFloat2("Start", &comp.start.x, 1.0f);
        ImGui::DragFloat2("End", &comp.end.x, 1.0f);
        ImGui::DragFloat("Thickness", &comp.thickness, 0.5f, 1.0f, 50.0f);
        EditColor("Color", comp.color);
        ImGui::Checkbox("Is Fixed", &comp.isFixed);
      });
}

void RegisterAudioAndGameplayInspectors() {
  DebugComponentInspectorRegistry::Register<AudioSourceComponent>(
      "Audio Source", [](Registry&, Entity, AudioSourceComponent& comp) {
        ImGui::InputText("Clip ID", &comp.clipId);
        ImGui::SliderFloat("Volume", &comp.volume, 0.0f, 1.0f);
        ImGui::SliderFloat("Pitch", &comp.pitch, 0.1f, 3.0f);
        ImGui::Checkbox("Loop", &comp.loop);
        ImGui::Checkbox("Spatial", &comp.spatial);
        ImGui::Checkbox("Doppler", &comp.doppler);
      });

  DebugComponentInspectorRegistry::Register<AudioListenerComponent>(
      "Audio Listener", [](Registry&, Entity, AudioListenerComponent& comp) {
        ImGui::DragFloat("Max Distance", &comp.maxDistance, 10.0f, 10.0f, 10000.0f);
        ImGui::DragFloat("Rolloff", &comp.rolloff, 0.1f, 0.0f, 10.0f);
        ImGui::DragFloat("Doppler Factor", &comp.dopplerFactor, 0.1f, 0.0f, 10.0f);
        ImGui::DragFloat("Speed of Sound", &comp.speedOfSound, 1.0f, 10.0f, 2000.0f);
      });

  DebugComponentInspectorRegistry::Register<LifetimeComponent>(
      "Lifetime", [](Registry&, Entity, LifetimeComponent& comp) {
        ImGui::DragFloat("Duration", &comp.lifetimeDuration, 0.1f, 0.0f, 1000.0f);
        ImGui::DragFloat("Remaining", &comp.remainingDuration, 0.1f, 0.0f, 1000.0f);
      });

  DebugComponentInspectorRegistry::Register<CameraFollowComponent>(
      "Camera Follow Tag", [](Registry&, Entity, CameraFollowComponent&) {
        ImGui::TextDisabled("Camera follows this entity's transform.");
      });
}

void RegisterUIInspectors() {
  DebugComponentInspectorRegistry::Register<UIRectComponent>("UI Rect", [](Registry&, Entity, UIRectComponent& comp) {
    ImGui::DragFloat("Left", &comp.left, 1.0f);
    ImGui::DragFloat("Top", &comp.top, 1.0f);
    ImGui::DragFloat("Right", &comp.right, 1.0f);
    ImGui::DragFloat("Bottom", &comp.bottom, 1.0f);
    ImGui::Text("Size: %.1fx%.1f", static_cast<double>(comp.Width()), static_cast<double>(comp.Height()));
    ImGui::Checkbox("Visible", &comp.visible);
  });

  DebugComponentInspectorRegistry::Register<UIZIndexComponent>(
      "UI Z-Index",
      [](Registry&, Entity, UIZIndexComponent& comp) { ImGui::DragInt("Z-Index", &comp.z, 1.0f, -100, 100); });

  DebugComponentInspectorRegistry::Register<UIScrollComponent>("UI Scroll",
                                                               [](Registry&, Entity, UIScrollComponent& comp) {
                                                                 ImGui::DragFloat("Scroll X", &comp.scrollX, 1.0f);
                                                                 ImGui::DragFloat("Scroll Y", &comp.scrollY, 1.0f);
                                                               });
}

void InspectLuaUserdata(const std::string& key, const sol::object& val, sol::table& parentTable) {
  if (val.is<glm::vec2>()) {
    glm::vec2 v = val.as<glm::vec2>();
    if (ImGui::DragFloat2(key.c_str(), &v.x, 0.1f)) {
      parentTable[key] = v;
    }
  } else {
    ImGui::Text("%s: <userdata>", key.c_str());
  }
}

void InspectLuaPrimitive(const std::string& key, const sol::object& val, sol::table& parentTable, sol::type type) {
  if (type == sol::type::number) {
    double num = val.as<double>();
    if (ImGui::DragScalar(key.c_str(), ImGuiDataType_Double, &num, 0.1f)) {
      parentTable[key] = num;
    }
  } else if (type == sol::type::boolean) {
    bool b = val.as<bool>();
    if (ImGui::Checkbox(key.c_str(), &b)) {
      parentTable[key] = b;
    }
  } else if (type == sol::type::string) {
    std::string s = val.as<std::string>();
    if (ImGui::InputText(key.c_str(), &s)) {
      parentTable[key] = s;
    }
  } else if (type == sol::type::userdata) {
    InspectLuaUserdata(key, val, parentTable);
  }
}

// Forward declare for recursion
void InspectLuaValue(const std::string& key, const sol::object& val, sol::table& parentTable, int depth);

// NOLINTNEXTLINE(misc-no-recursion)
void InspectLuaTable(const std::string& key, const sol::table& tbl, int depth) {
  if (!ImGui::TreeNode(key.c_str())) return;
  std::vector<std::string> keys;
  for (const auto& [k, v] : tbl) {
    if (k.is<std::string>()) {
      const std::string kstr = k.as<std::string>();
      if (!kstr.starts_with("__")) keys.push_back(kstr);
    }
  }
  std::sort(keys.begin(), keys.end());
  sol::table mutableTbl = tbl;
  for (const auto& subKey : keys) {
    InspectLuaValue(subKey, mutableTbl.get<sol::object>(subKey), mutableTbl, depth + 1);
  }
  ImGui::TreePop();
}

// NOLINTNEXTLINE(misc-no-recursion)
void InspectLuaValue(const std::string& key, const sol::object& val, sol::table& parentTable, int depth) {
  if (depth > 4) {
    ImGui::Text("%s: <max depth>", key.c_str());
    return;
  }

  const auto type = val.get_type();
  if (type == sol::type::table) {
    InspectLuaTable(key, val.as<sol::table>(), depth);
  } else {
    InspectLuaPrimitive(key, val, parentTable, type);
  }
}

}  // namespace

void DrawScriptComponentLuaTableInspector(Registry& registry, Entity entity) {
  if (!registry.HasComponent<ScriptComponent>(entity)) return;
  auto& script = registry.GetComponent<ScriptComponent>(entity);
  if (!script.script_table.valid()) {
    ImGui::TextDisabled("No script_table loaded.");
    return;
  }

  std::vector<std::string> keys;
  for (const auto& [k, v] : script.script_table) {
    if (k.is<std::string>()) {
      const std::string kstr = k.as<std::string>();
      if (!kstr.starts_with("__") && v.get_type() != sol::type::function) {
        keys.push_back(kstr);
      }
    }
  }
  std::sort(keys.begin(), keys.end());

  if (keys.empty()) {
    ImGui::TextDisabled("No editable state fields found.");
    return;
  }

  for (const auto& key : keys) {
    InspectLuaValue(key, script.script_table.get<sol::object>(key), script.script_table, 0);
  }
}

void DebugComponentInspectorRegistry::InstallBuiltInComponentInspectors() {
  static bool installed = false;
  if (installed) return;
  installed = true;

  RegisterTransformInspectors();
  RegisterPhysicsInspectors();
  RegisterRenderInspectors();
  RegisterAudioAndGameplayInspectors();
  RegisterUIInspectors();
}

#else

void DebugComponentInspectorRegistry::InstallBuiltInComponentInspectors() {}

#endif  // OCTARINE_WITH_IMGUI

}  // namespace octarine::debug
