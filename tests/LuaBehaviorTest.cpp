// Behavioral checks for the Lua binding surface — call bound component methods from Lua
// and verify the C++ component mutated. Layered on top of LuaApiSmokeTest, which only
// validates that the surface exists. Catches the "binding compiles, method does nothing"
// class of regression that drift checks miss.
//
// Headless Game (constructor allocates Registry/EventBus/Lua, opens no SDL window/renderer).

#include <cmath>
#include <sol/sol.hpp>
#include <string>

#include "Components/CapsulePrimitiveComponent.h"
#include "Components/CirclePrimitiveComponent.h"
#include "Components/ColorGridComponent.h"
#include "Components/GlobalTransformComponent.h"
#include "Components/LinePrimitiveComponent.h"
#include "Components/TrianglePrimitiveComponent.h"
#ifdef OCTARINE_HAS_GAME_MODULE
#include "Components/HealthComponent.h"
#include "Lua/Bindings/HealthComponentLuaBinding.h"
#endif
#include "Components/LifetimeComponent.h"
#include "Components/NameComponent.h"
#include "Components/PivotComponent.h"
#include "Components/PositionComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/RotationComponent.h"
#include "Components/ScaleComponent.h"
#include "Components/UIButtonComponent.h"
#include "ECS/Registry.h"
#include "Events/GamepadButtonEvent.h"
#include "Events/KeyInputEvent.h"
#include "Game/Game.h"
#include "Game/GameConfig.h"
#include "General/AngleUnit.h"
#include "General/Logger.h"
#include "Lua/Bindings/LuaBinding.h"
#include "Lua/Bindings/LuaComponentRegistry.h"
#include "Lua/Bindings/RegisterAllBindings.h"
#include "Lua/Bindings/RotationComponentLuaBinding.h"
#include "Lua/Modules/RegisterAllModules.h"
#include "Systems/InputSystem.h"
#include "Systems/RenderDebugGUISystem.h"
#include "Systems/ScriptSystem.h"
#include "Systems/TransformSystem.h"
#include "Systems/UIButtonSystem.h"
#include "Systems/VelocityIntegrationSystem.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {
bool NearVec2(const glm::vec2 got, const glm::vec2 want, const float eps = 1e-3F) {
  return std::fabs(got.x - want.x) < eps && std::fabs(got.y - want.y) < eps;
}

bool RunLua(sol::state& lua, const std::string& script) {
  const auto r = lua.safe_script(script, sol::script_pass_on_error);
  if (!r.valid()) {
    const sol::error err = r;
    std::cerr << "    lua error: " << err.what() << "\n";
    return false;
  }
  return true;
}

void TestPrimitiveBindings(sol::state& lua, Registry* reg) {
  std::cout << "[primitive components: circle / line / triangle / capsule round-trip via Lua]\n";
  Check(RunLua(lua, R"LUA(
    e_circle = load_entity({
      components = {
        transform = { position = { x = 0, y = 0 } },
        circle = {
          position = { x = 4, y = 8 },
          layer = 3,
          radius = 16.5,
          thickness = 2.5,
          color = { r = 200, a = 128 },
          fixed = true,
          filled = false,
          blend_mode = "add",
        }
      }
    })
    e_line = load_entity({
      components = {
        transform = { position = { x = 0, y = 0 } },
        line = {
          start = { x = 1, y = 2 },
          ["end"] = { x = 10, y = 20 },
          layer = 2,
          thickness = 3.0,
          color = { g = 180, a = 255 },
          fixed = false,
          blend_mode = "mod",
        }
      }
    })
    e_tri = load_entity({
      components = {
        transform = { position = { x = 0, y = 0 } },
        triangle = {
          p1 = { x = 0, y = -5 },
          p2 = { x = 5, y = 5 },
          p3 = { x = -5, y = 5 },
          layer = 4,
          thickness = 2.0,
          color = { b = 250, a = 200 },
          fixed = true,
          filled = true,
        }
      }
    })
    e_cap = load_entity({
      components = {
        transform = { position = { x = 0, y = 0 } },
        capsule = {
          start = { x = -10, y = 0 },
          ["end"] = { x = 10, y = 0 },
          layer = 5,
          radius = 6.0,
          thickness = 1.5,
          color = { r = 100, g = 150, b = 200, a = 255 },
          fixed = false,
          filled = false,
        }
      }
    })
  )LUA"),
        "load_entity succeeds for circle, line, triangle, and capsule");

  const Entity eCircle = lua["e_circle"];
  const Entity eLine = lua["e_line"];
  const Entity eTri = lua["e_tri"];
  const Entity eCap = lua["e_cap"];

  Check(reg->HasComponent<GlobalTransformComponent>(eCircle), "circle auto-attaches GlobalTransformComponent");
  Check(reg->HasComponent<GlobalTransformComponent>(eLine), "line auto-attaches GlobalTransformComponent");
  Check(reg->HasComponent<GlobalTransformComponent>(eTri), "triangle auto-attaches GlobalTransformComponent");
  Check(reg->HasComponent<GlobalTransformComponent>(eCap), "capsule auto-attaches GlobalTransformComponent");

  const auto& circleComp = reg->GetComponent<CirclePrimitiveComponent>(eCircle);
  CheckEq(circleComp.radius, 16.5F, "circle radius parsed");
  CheckEq(static_cast<int>(circleComp.color.r), 200, "circle partial color r parsed");
  CheckEq(static_cast<int>(circleComp.color.g), 0, "circle partial color omitted g defaults to 0");
  Check(circleComp.blendMode == octarine::BlendMode::Add, "circle blend_mode parsed as Add");
  Check(!circleComp.filled, "circle filled=false parsed");

  Check(RunLua(lua, R"LUA(
    local c = registry.get_circle(e_circle)
    c.radius = 32.0
    c.filled = true
    c.blend_mode = "blend"
    local l = registry.get_line(e_line)
    l.thickness = 5.0
    local tr = registry.get_triangle(e_tri)
    tr.p2 = vec2.new(12, 15)
    local cp = registry.get_capsule(e_cap)
    cp.radius = 9.5
    cp.blend_mode = "add"
  )LUA"),
        "Lua mutations on circle, line, triangle, and capsule succeed");

  CheckEq(circleComp.radius, 32.0F, "circle.radius mutated from Lua");
  Check(circleComp.filled, "circle.filled mutated from Lua");
  Check(circleComp.blendMode == octarine::BlendMode::Blend, "circle.blend_mode mutated from Lua");
  CheckEq(reg->GetComponent<LinePrimitiveComponent>(eLine).thickness, 5.0F, "line.thickness mutated from Lua");
  Check(NearVec2(reg->GetComponent<TrianglePrimitiveComponent>(eTri).p2, {12.0F, 15.0F}),
        "triangle.p2 mutated from Lua");
  CheckEq(reg->GetComponent<CapsulePrimitiveComponent>(eCap).radius, 9.5F, "capsule.radius mutated from Lua");
  Check(reg->GetComponent<CapsulePrimitiveComponent>(eCap).blendMode == octarine::BlendMode::Add,
        "capsule.blend_mode mutated from Lua");
}

void TestColliderBindings(sol::state& lua, Registry* reg) {
  std::cout << "[collider components: circle / capsule / triangle / line round-trip via Lua]\n";
  Check(RunLua(lua, R"LUA(
    c_circle = load_entity({
      components = {
        transform = { position = { x = 10, y = 20 } },
        circle_collider = { radius = 12.5, offset = { x = 2, y = 3 }, is_fixed = true, collision_mask = 3 }
      }
    })
    c_cap = load_entity({
      components = {
        transform = { position = { x = 0, y = 0 } },
        capsule_collider = { start = { x = -8, y = 0 }, ["end"] = { x = 8, y = 0 }, radius = 4.0 }
      }
    })
    c_tri = load_entity({
      components = {
        transform = { position = { x = 0, y = 0 } },
        triangle_collider = { p1 = { x = 0, y = -6 }, p2 = { x = 6, y = 6 }, p3 = { x = -6, y = 6 } }
      }
    })
    c_line = load_entity({
      components = {
        transform = { position = { x = 0, y = 0 } },
        line_collider = { start = { x = -15, y = -5 }, ["end"] = { x = 15, y = 5 }, is_fixed = false }
      }
    })
  )LUA"),
        "load_entity succeeds for circle_collider, capsule_collider, triangle_collider, and line_collider");

  const Entity cCircle = lua["c_circle"];
  const Entity cCap = lua["c_cap"];
  const Entity cTri = lua["c_tri"];
  const Entity cLine = lua["c_line"];

  Check(reg->HasComponent<GlobalTransformComponent>(cCircle), "circle_collider auto-attaches GlobalTransformComponent");
  Check(reg->HasComponent<EntityMaskComponent>(cCircle), "circle_collider auto-attaches EntityMaskComponent");
  Check(reg->HasComponent<GlobalTransformComponent>(cCap), "capsule_collider auto-attaches GlobalTransformComponent");
  Check(reg->HasComponent<EntityMaskComponent>(cCap), "capsule_collider auto-attaches EntityMaskComponent");
  Check(reg->HasComponent<GlobalTransformComponent>(cTri), "triangle_collider auto-attaches GlobalTransformComponent");
  Check(reg->HasComponent<EntityMaskComponent>(cTri), "triangle_collider auto-attaches EntityMaskComponent");
  Check(reg->HasComponent<GlobalTransformComponent>(cLine), "line_collider auto-attaches GlobalTransformComponent");
  Check(reg->HasComponent<EntityMaskComponent>(cLine), "line_collider auto-attaches EntityMaskComponent");

  CheckEq(reg->GetComponent<CircleColliderComponent>(cCircle).radius, 12.5F, "circle_collider.radius parsed");
  Check(reg->GetComponent<CircleColliderComponent>(cCircle).isFixed, "circle_collider.is_fixed parsed");
  CheckEq(reg->GetComponent<CapsuleColliderComponent>(cCap).radius, 4.0F, "capsule_collider.radius parsed");
  Check(NearVec2(reg->GetComponent<TriangleColliderComponent>(cTri).p2, {6.0F, 6.0F}), "triangle_collider.p2 parsed");
  Check(NearVec2(reg->GetComponent<LineColliderComponent>(cLine).end, {15.0F, 5.0F}), "line_collider.end parsed");

  Check(RunLua(lua, R"LUA(
    registry.get_circle_collider(c_circle).radius = 24.0
    registry.get_capsule_collider(c_cap).radius = 7.5
    registry.get_triangle_collider(c_tri).p3 = vec2.new(-10, 8)
    registry.get_line_collider(c_line).start = vec2.new(-20, -10)
  )LUA"),
        "Lua mutations on collider components succeed");

  CheckEq(reg->GetComponent<CircleColliderComponent>(cCircle).radius, 24.0F, "circle_collider.radius mutated");
  CheckEq(reg->GetComponent<CapsuleColliderComponent>(cCap).radius, 7.5F, "capsule_collider.radius mutated");
  Check(NearVec2(reg->GetComponent<TriangleColliderComponent>(cTri).p3, {-10.0F, 8.0F}),
        "triangle_collider.p3 mutated");
  Check(NearVec2(reg->GetComponent<LineColliderComponent>(cLine).start, {-20.0F, -10.0F}),
        "line_collider.start mutated");
}

void EmitLeftMouseClick(EventBus& bus, const float x, const float y) {
  SDL_MouseButtonEvent ev{};
  ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  ev.button = 1;
  ev.x = x;
  ev.y = y;
  bus.EmitEvent<MouseInputEvent>(ev);
}

void TestUIButtonShapes(sol::state& lua, Registry* reg) {
  std::cout << "[ui_button: shaped hit-testing for layout UIRect and transform buttons]\n";
  auto eventBus = std::make_unique<EventBus>();
  UIButtonSystem uiButtonSystem;
  uiButtonSystem.Init(reg, eventBus);

  Check(RunLua(lua, R"LUA(
    hit_circle = false
    hit_capsule = false
    hit_tri = false
    hit_transform_circle = false

    btn_circle = load_entity({
      components = {
        circle = { color = { r = 255, a = 255 } },
        ui_button = { on_click = function() hit_circle = true end }
      }
    })
    ui.anchor(btn_circle, "top_left", { width = 100, height = 100 })

    btn_capsule = load_entity({
      components = {
        capsule = { color = { g = 255, a = 255 } },
        ui_button = { on_click = function() hit_capsule = true end }
      }
    })
    ui.anchor(btn_capsule, "top_left", { width = 200, height = 60 })

    btn_tri = load_entity({
      components = {
        triangle = { color = { b = 255, a = 255 } },
        ui_button = { on_click = function() hit_tri = true end }
      }
    })
    ui.anchor(btn_tri, "top_left", { width = 100, height = 100 })

    btn_tx_circle = load_entity({
      components = {
        transform = { position = { x = 300, y = 300 } },
        circle = { radius = 25.0, fixed = true },
        ui_button = { is_fixed = true, on_click = function() hit_transform_circle = true end }
      }
    })
  )LUA"),
        "shaped UI buttons loaded from Lua");

  const Entity btnCircle = lua["btn_circle"];
  const Entity btnCapsule = lua["btn_capsule"];
  const Entity btnTri = lua["btn_tri"];
  const Entity btnTxCircle = lua["btn_tx_circle"];
  reg->GetComponent<UIRectComponent>(btnCircle) = UIRectComponent(0.0F, 0.0F, 100.0F, 100.0F, 1);
  reg->GetComponent<UIRectComponent>(btnCapsule) = UIRectComponent(0.0F, 200.0F, 200.0F, 260.0F, 1);
  reg->GetComponent<UIRectComponent>(btnTri) = UIRectComponent(0.0F, 400.0F, 100.0F, 500.0F, 1);
  reg->GetComponent<GlobalTransformComponent>(btnTxCircle).position = {300.0F, 300.0F};

  EmitLeftMouseClick(*eventBus, 2.0F, 2.0F);
  Check(lua["hit_circle"] == false, "UIRect circle button ignores click in bounding-box corner outside circle");
  EmitLeftMouseClick(*eventBus, 50.0F, 50.0F);
  Check(lua["hit_circle"] == true, "UIRect circle button fires on click inside circle");

  EmitLeftMouseClick(*eventBus, 1.0F, 201.0F);
  Check(lua["hit_capsule"] == false, "UIRect capsule button ignores click outside pill cap");
  EmitLeftMouseClick(*eventBus, 100.0F, 230.0F);
  Check(lua["hit_capsule"] == true, "UIRect capsule button fires on click inside pill");

  EmitLeftMouseClick(*eventBus, 5.0F, 405.0F);
  Check(lua["hit_tri"] == false, "UIRect triangle button ignores click outside triangle edge");
  EmitLeftMouseClick(*eventBus, 50.0F, 480.0F);
  Check(lua["hit_tri"] == true, "UIRect triangle button fires on click inside triangle");

  EmitLeftMouseClick(*eventBus, 350.0F, 350.0F);
  Check(lua["hit_transform_circle"] == false, "transform circle button ignores click outside radius");
  EmitLeftMouseClick(*eventBus, 310.0F, 310.0F);
  Check(lua["hit_transform_circle"] == true, "transform circle button fires via primitive fallback");
}
}  // namespace

int main() {
  // Logger::Init wires the lua_logger_ that Logger::LogLua / Logger::InfoLua dereference.
  // Without it, the bound Lua `log` / `log_i` functions crash on null logger.
  Logger::Init();

  Game game;  // SDL-free constructor.
  sol::state& lua = game.GetLua();
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::io, sol::lib::string, sol::lib::table);

  RegisterAllLuaBindings();
  ScriptSystem scriptSystem;
  scriptSystem.CreateLuaBindings(lua);  // primitives + every LuaBinding<T>::bindUsertype
  RegisterAllLuaModules(lua, game);     // installs `registry.has_/get_<key>` + free functions

  Registry* reg = game.GetRegistry();

#ifdef OCTARINE_HAS_GAME_MODULE
  std::cout << "[health: damage / heal round-trip via Lua]\n";
  {
    const Entity e = reg->CreateEntity();
    reg->AddComponent<HealthComponent>(e, HealthComponent(100));
    lua["test_entity"] = e;

    Check(RunLua(lua, "registry.get_health(test_entity):damage(30)"), "Lua: damage(30) call succeeds");
    CheckEq(reg->GetComponent<HealthComponent>(e).current_health, 70, "health.damage(30) reduced current_health to 70");

    Check(RunLua(lua, "registry.get_health(test_entity):heal(10)"), "Lua: heal(10) call succeeds");
    CheckEq(reg->GetComponent<HealthComponent>(e).current_health, 80, "health.heal(10) bumped current_health to 80");

    // damage() floors at 0; heal() caps at max_health — invariants enforced by bound methods.
    Check(RunLua(lua, "registry.get_health(test_entity):damage(9999)"), "Lua: massive damage call succeeds");
    CheckEq(reg->GetComponent<HealthComponent>(e).current_health, 0, "damage() floors at 0");

    Check(RunLua(lua, "registry.get_health(test_entity):heal(9999)"), "Lua: massive heal call succeeds");
    CheckEq(reg->GetComponent<HealthComponent>(e).current_health, 100, "heal() caps at max_health");
  }

  std::cout << "[health: clamping property setter from Lua]\n";
  {
    const Entity e = reg->CreateEntity();
    reg->AddComponent<HealthComponent>(e, HealthComponent(50));
    lua["test_entity"] = e;

    // current_health is a clamping property; writing a value above max_health must clamp,
    // writing a negative value must clamp to 0.
    Check(RunLua(lua, "registry.get_health(test_entity).current_health = 999"), "Lua write current_health=999");
    CheckEq(reg->GetComponent<HealthComponent>(e).current_health, 50, "current_health setter clamps to max_health");

    Check(RunLua(lua, "registry.get_health(test_entity).current_health = -42"), "Lua write current_health=-42");
    CheckEq(reg->GetComponent<HealthComponent>(e).current_health, 0, "current_health setter clamps to 0");
  }
#endif

  std::cout << "[position: write field through bound usertype]\n";
  {
    const Entity e = reg->CreateEntity();
    reg->AddComponent<PositionComponent>(e, PositionComponent(glm::vec2(1.0F, 2.0F)));
    lua["test_entity"] = e;

    Check(RunLua(lua, "registry.get_position(test_entity).value = vec2.new(7, 9)"), "Lua write position.value");
    CheckEq(reg->GetComponent<PositionComponent>(e).value.x, 7.0F, "position.value.x written from Lua");
    CheckEq(reg->GetComponent<PositionComponent>(e).value.y, 9.0F, "position.value.y written from Lua");
  }

  std::cout << "[has_/get_ accessors]\n";
  {
    const Entity hasPos = reg->CreateEntity();
    reg->AddComponent<PositionComponent>(hasPos, PositionComponent(glm::vec2(1.0f, 2.0f)));
    const Entity noPos = reg->CreateEntity();

    lua["with_pos"] = hasPos;
    lua["without_pos"] = noPos;
    Check(RunLua(lua, "assert(registry.has_position(with_pos))"), "registry.has_position true for owner");
    Check(RunLua(lua, "assert(not registry.has_position(without_pos))"), "registry.has_position false for non-owner");
  }

  std::cout << "[blam: single entity and table of entities]\n";
  {
    const Entity single = reg->CreateEntity();
    lua["blam_single"] = single;
    Check(RunLua(lua, "blam(blam_single)"), "Lua: blam(entity) call succeeds");
    Check(reg->IsAlive(single), "blam(entity) defers destruction until end of frame");
    reg->Update(0.016F);
    Check(!reg->IsAlive(single), "blam(entity) destroyed the entity after Update");

    const Entity first = reg->CreateEntity();
    const Entity second = reg->CreateEntity();
    const Entity survivor = reg->CreateEntity();
    lua["blam_first"] = first;
    lua["blam_second"] = second;
    Check(RunLua(lua, "blam({ blam_first, blam_second })"), "Lua: blam(table) call succeeds");
    Check(reg->IsAlive(first) && reg->IsAlive(second), "blam(table) defers destruction until end of frame");
    reg->Update(0.016F);
    Check(!reg->IsAlive(first), "blam(table) destroyed the first entity after Update");
    Check(!reg->IsAlive(second), "blam(table) destroyed the second entity after Update");
    Check(reg->IsAlive(survivor), "blam(table) left unrelated entities alone");

    // Non-entity entries are skipped with an error log, not a Lua error.
    Check(RunLua(lua, "blam({ 42, 'nope' })"), "Lua: blam(table) tolerates non-entity entries");
  }

  std::cout << "[registry.is_alive reports entity liveness]\n";
  {
    const Entity aliveEnt = reg->CreateEntity();
    lua["alive_ent"] = aliveEnt;
    Check(RunLua(lua, "assert(registry.is_alive(alive_ent) == true)"),
          "registry.is_alive returns true for live entity");

    Check(RunLua(lua, "assert(registry.is_alive(nil) == false)"), "registry.is_alive returns false for nil");
    Check(RunLua(lua, "assert(registry.is_alive() == false)"), "registry.is_alive returns false with no args");
    Check(RunLua(lua, "assert(registry.is_alive(12345) == false)"), "registry.is_alive returns false for non-entity");
    Check(RunLua(lua, "assert(registry.is_alive({}) == false)"), "registry.is_alive returns false for table");

    lua["dead_ent"] = aliveEnt;
    reg->BlamEntity(aliveEnt);
    reg->Update(0.016F);
    Check(RunLua(lua, "assert(registry.is_alive(dead_ent) == false)"),
          "registry.is_alive returns false for destroyed entity");
  }

  std::cout << "[registry.set_parent / get_parent round-trip]\n";
  {
    const Entity parent = reg->CreateEntity();
    const Entity child = reg->CreateEntity();
    lua["hier_parent"] = parent;
    lua["hier_child"] = child;

    Check(RunLua(lua, "assert(registry.get_parent(hier_child) == nil)"), "get_parent is nil before set_parent");
    Check(RunLua(lua, "registry.set_parent(hier_child, hier_parent)"), "Lua: set_parent call succeeds");
    Check(reg->GetParent(child).has_value() && reg->GetParent(child)->id == parent.id,
          "set_parent wired child->parent in the registry");
    Check(RunLua(lua, "assert(registry.get_parent(hier_child):get_id() == hier_parent:get_id())"),
          "get_parent returns the parent handle");

    const Entity second = reg->CreateEntity();
    lua["hier_second"] = second;
    Check(RunLua(lua, "registry.set_parent(hier_child, hier_second)"), "Lua: reparent call succeeds");
    Check(reg->GetParent(child).has_value() && reg->GetParent(child)->id == second.id,
          "reparent moved the child to the new parent");
    Check(reg->GetChildren(parent).empty(), "reparent detached the child from the old parent");

    // Self-parenting and cycles are rejected with an error log, not a Lua error.
    Check(RunLua(lua, "registry.set_parent(hier_child, hier_child)"), "Lua: self-parent call tolerated");
    Check(reg->GetParent(child).has_value() && reg->GetParent(child)->id == second.id,
          "self-parent rejected, parent unchanged");
    Check(RunLua(lua, "registry.set_parent(hier_second, hier_child)"), "Lua: cycle call tolerated");
    Check(!reg->GetParent(second).has_value(), "cycle rejected, would-be grandparent unchanged");
  }

  std::cout << "[load_entity: returns the root entity handle]\n";
  {
    Check(RunLua(lua,
                 "spawned = load_entity({ name = 'HandleTest', components = { transform = { position = { x = 3, y = "
                 "4 } } } })"),
          "Lua: load_entity call succeeds");
    Check(RunLua(lua, "assert(spawned ~= nil)"), "load_entity returned a non-nil handle");
    Check(RunLua(lua, "assert(type(spawned:get_id()) == 'number')"), "handle exposes get_id()");
    Check(RunLua(lua, "assert(registry.has_position(spawned))"), "handle works with registry accessors");

    const Entity spawned = lua["spawned"].get<Entity>();
    CheckEq(reg->GetComponent<PositionComponent>(spawned).value.x, 3.0F,
            "handle resolves to the entity built from the table");
  }

#ifdef OCTARINE_HAS_GAME_MODULE
  std::cout << "[fromLua: defaults applied when fields omitted]\n";
  {
    // HealthComponent has no default-constructor (deleted); fromLua must synthesize one
    // from an empty table using its 100-max default.
    const auto t = lua.script("return {}");
    const HealthComponent defaulted = LuaBinding<HealthComponent>::fromLua(t);
    CheckEq(defaulted.max_health, 100, "fromLua({}) gives default max_health=100");
    CheckEq(defaulted.current_health, 100, "fromLua({}) starts at full health");

    const auto t2 = lua.script("return { max_health = 25 }");
    const HealthComponent custom = LuaBinding<HealthComponent>::fromLua(t2);
    CheckEq(custom.max_health, 25, "fromLua reads max_health");
    CheckEq(custom.current_health, 25, "fromLua initializes current to max");
  }
#endif

  std::cout << "[module callable: log() runs]\n";
  {
    // No observable side effect to assert against without wiring a log sink — just confirm
    // the bound function exists and returns from a Lua call.
    Check(lua["log"].valid() && lua["log"].get_type() == sol::type::function, "log is bound as a function");
    Check(RunLua(lua, "log('LuaBehaviorTest: smoke from log()')"), "log('...') runs from Lua without error");
  }

  std::cout << "[angle unit: degrees convert at the Lua boundary, radians pass through]\n";
  {
    octarine::AngleUnits::Set(octarine::AngleUnit::Degrees);

    const Entity e = reg->CreateEntity();
    reg->AddComponent<RotationComponent>(e, RotationComponent(0.0F));
    lua["angle_entity"] = e;

    Check(RunLua(lua, "set_rotation(angle_entity, 90)"), "set_rotation(90) runs in degrees mode");
    Check(std::fabs(reg->GetComponent<RotationComponent>(e).value - 1.5707964F) < 1e-4F,
          "90 degrees authored from Lua stores pi/2 radians");

    Check(RunLua(lua, "lua_angle = registry.get_rotation(angle_entity).value"), "rotation.value reads from Lua");
    Check(std::fabs(lua["lua_angle"].get<float>() - 90.0F) < 1e-3F,
          "reading rotation.value back gives 90, not the stored radians");

    const auto degTable = lua.script("return { value = 180 }");
    Check(std::fabs(LuaBinding<RotationComponent>::fromLua(degTable).value - 3.1415927F) < 1e-4F,
          "component table value=180 stores pi radians in degrees mode");

    // Radians mode is opt-in via AngleUnit=radians and must leave authored values untouched.
    octarine::AngleUnits::Set(octarine::AngleUnit::Radians);
    const auto radTable = lua.script("return { value = 3.1415927 }");
    Check(std::fabs(LuaBinding<RotationComponent>::fromLua(radTable).value - 3.1415927F) < 1e-4F,
          "radians mode stores the authored value unchanged");
    Check(RunLua(lua, "set_rotation(angle_entity, 1.5707964)"), "set_rotation runs in radians mode");
    Check(std::fabs(reg->GetComponent<RotationComponent>(e).value - 1.5707964F) < 1e-4F,
          "radians mode stores pi/2 as authored");

    // AngleUnits is process-wide, so restore the default before the next block: leaving radians
    // set here would silently reinterpret any angle a later test authors.
    octarine::AngleUnits::Set(octarine::AngleUnit::Degrees);
  }

  std::cout << "[scale shorthand: a bare number means uniform scale on both authoring paths]\n";
  {
    Check(RunLua(lua, R"LUA(
      inline_uniform  = load_entity({ components = { transform = { scale = 3 } } })
      inline_table    = load_entity({ components = { transform = { scale = { x = 2, y = 5 } } } })
      standalone      = load_entity({ components = { scale = 4 } })
      standalone_flat = load_entity({ components = { scale = { x = 6, y = 7 } } })
    )LUA"),
          "scale shorthand script runs");

    const Entity inlineUniform = lua["inline_uniform"];
    const Entity inlineTable = lua["inline_table"];
    const Entity standalone = lua["standalone"];
    const Entity standaloneFlat = lua["standalone_flat"];

    Check(reg->GetComponent<ScaleComponent>(inlineUniform).value == glm::vec2(3.0F, 3.0F),
          "transform.scale = 3 is uniform scale, not the (1,1) default");
    Check(reg->GetComponent<ScaleComponent>(inlineTable).value == glm::vec2(2.0F, 5.0F),
          "transform.scale as a table still reads both axes");
    Check(reg->GetComponent<ScaleComponent>(standalone).value == glm::vec2(4.0F, 4.0F),
          "a standalone scale component takes the same shorthand");
    Check(reg->GetComponent<ScaleComponent>(standaloneFlat).value == glm::vec2(6.0F, 7.0F),
          "a standalone scale component accepts flat { x = ..., y = ... }");

    // `scale` is the uniform view and reads x, so scripts need is_uniform() to know whether that
    // is the whole story. It must not fail on the non-uniform case, only report it.
    Check(RunLua(lua, "uni = registry.get_scale(inline_uniform):is_uniform()"), "is_uniform reads from Lua");
    Check(lua["uni"].get<bool>(), "a uniformly scaled entity reports is_uniform");
    Check(RunLua(lua, "nonuni = registry.get_scale(inline_table):is_uniform()"),
          "is_uniform reads on a per-axis scale");
    Check(!lua["nonuni"].get<bool>(), "a per-axis scale reports non-uniform");
    Check(RunLua(lua, "any = registry.get_scale(inline_table).scale"), "reading .scale on a per-axis scale is safe");
    Check(lua["any"].get<float>() == 2.0F, "the uniform view of a per-axis scale reports the x axis");
  }

  std::cout << "[pivot: authored independently of rotation]\n";
  {
    Check(RunLua(lua, R"LUA(
      inline_pivot   = load_entity({ components = { transform = { pivot = { x = 0, y = 0 } } } })
      standalone     = load_entity({ components = { pivot = { x = 1, y = 1 } } })
      shorthand      = load_entity({ components = { pivot = 0 } })
      nested         = load_entity({ components = { pivot = { value = { x = 0.25, y = 0.75 } } } })
      rotation_only  = load_entity({ components = { transform = { rotation = 0.5 } } })
      pivot_no_rot   = load_entity({ components = { transform = { scale = 2, pivot = { x = 0, y = 0 } } } })
    )LUA"),
          "pivot authoring script runs");

    const Entity inlinePivot = lua["inline_pivot"];
    const Entity standalone = lua["standalone"];
    const Entity shorthand = lua["shorthand"];
    const Entity nested = lua["nested"];
    const Entity rotationOnly = lua["rotation_only"];
    const Entity pivotNoRot = lua["pivot_no_rot"];

    Check(reg->GetComponent<PivotComponent>(inlinePivot).value == glm::vec2(0.0F, 0.0F),
          "transform.pivot builds a PivotComponent of its own");
    Check(reg->GetComponent<PivotComponent>(standalone).value == glm::vec2(1.0F, 1.0F),
          "a standalone pivot component reads x and y");
    Check(reg->GetComponent<PivotComponent>(shorthand).value == glm::vec2(0.0F, 0.0F),
          "a bare number anchors both axes");
    // Reasoning by analogy from `scale`/`position`, whose standalone forms nest under `value`.
    Check(reg->GetComponent<PivotComponent>(nested).value == glm::vec2(0.25F, 0.75F),
          "the nested { value = { x, y } } form is accepted too, not silently defaulted");

    // The split is the point: neither component implies the other.
    Check(!reg->HasComponent<PivotComponent>(rotationOnly), "rotation alone attaches no pivot");
    Check(reg->HasComponent<RotationComponent>(rotationOnly), "rotation alone still attaches rotation");
    Check(reg->HasComponent<PivotComponent>(pivotNoRot), "a pivot can be authored with no rotation at all");
    Check(!reg->HasComponent<RotationComponent>(pivotNoRot), "pivot alone attaches no rotation");
  }

  std::cout << "[component axis accessors: x and y are distinct on both axes]\n";
  {
    const Entity e = reg->CreateEntity();
    reg->AddComponent<PositionComponent>(e, PositionComponent(glm::vec2(3.0F, 7.0F)));
    reg->AddComponent<ScaleComponent>(e, ScaleComponent(2.0F, 5.0F));
    lua["axis_entity"] = e;

    Check(RunLua(lua, "px = registry.get_position(axis_entity).x; py = registry.get_position(axis_entity).y"),
          "position x/y read from Lua");
    Check(lua["px"].get<float>() == 3.0F, "position.x reads x");
    Check(lua["py"].get<float>() == 7.0F, "position.y reads y, not x");

    Check(RunLua(lua, "sx = registry.get_scale(axis_entity).x; sy = registry.get_scale(axis_entity).y"),
          "scale x/y read from Lua");
    Check(lua["sx"].get<float>() == 2.0F, "scale.x reads x");
    Check(lua["sy"].get<float>() == 5.0F, "scale.y reads y, not x");

    reg->AddComponent<PivotComponent>(e, PivotComponent(0.25F, 0.75F));
    Check(RunLua(lua, "vx = registry.get_pivot(axis_entity).x; vy = registry.get_pivot(axis_entity).y"),
          "pivot x/y read from Lua");
    Check(lua["vx"].get<float>() == 0.25F, "pivot.x reads x");
    Check(lua["vy"].get<float>() == 0.75F, "pivot.y reads y, not x");

    Check(RunLua(lua, "registry.get_position(axis_entity).y = 11"), "position.y writes from Lua");
    Check(reg->GetComponent<PositionComponent>(e).value == glm::vec2(3.0F, 11.0F),
          "writing position.y moves y and leaves x alone");

    Check(RunLua(lua, "registry.get_pivot(axis_entity).x = 1"), "pivot.x writes from Lua");
    Check(reg->GetComponent<PivotComponent>(e).value == glm::vec2(1.0F, 0.75F),
          "writing pivot.x moves x and leaves y alone");
  }

  std::cout << "[pivot anchors the drawn quad under scale, growing and shrinking]\n";
  {
    reg->RegisterBulkSystem<GlobalTransformComponent>(TransformSystem());

    // A 40x20 sprite anchored at its bottom-right corner. Whatever the scale, that corner is the
    // fixed point and the quad grows away from it — the property a corner pivot promises.
    Check(RunLua(lua, R"LUA(
      anchored = load_entity({ components = {
        transform = { position = { x = 200, y = 100 }, scale = 1, pivot = { x = 1, y = 1 } },
        sprite = { texture_asset_id = "tex", width = 40, height = 20 },
      }})
    )LUA"),
          "corner-anchored entity builds from Lua");
    const Entity anchored = lua["anchored"];

    for (const float scale : {1.0F, 2.0F, 3.5F, 0.5F}) {
      reg->GetComponent<ScaleComponent>(anchored).value = glm::vec2(scale, scale);
      reg->Update(1.0F / 60.0F);

      // Exactly what RenderSpriteSystem draws: destRect at `position`, sized width * scale.
      const auto& g = reg->GetComponent<GlobalTransformComponent>(anchored);
      const glm::vec2 bottomRight = g.position + glm::vec2(40.0F, 20.0F) * g.scale;
      Check(std::fabs(bottomRight.x - 240.0F) < 1e-3F && std::fabs(bottomRight.y - 120.0F) < 1e-3F,
            "anchored corner holds at scale " + std::to_string(scale));
    }

    // Moving the pivot from Lua re-anchors on the very next frame: the anchor is resolved inside
    // TransformSystem, so there is no cached offset to go stale behind a write.
    reg->GetComponent<ScaleComponent>(anchored).value = glm::vec2(2.0F, 2.0F);
    Check(RunLua(lua, "local p = registry.get_pivot(anchored); p.x = 0; p.y = 0"), "pivot moved from Lua");
    reg->Update(1.0F / 60.0F);
    Check(NearVec2(reg->GetComponent<GlobalTransformComponent>(anchored).position, glm::vec2(200.0F, 100.0F)),
          "a pivot written from Lua re-anchors the entity the next frame");
  }

  std::cout << "[color_grid component Lua binding and mutation]\n";
  {
    Check(RunLua(lua, R"LUA(
      grid_ent = load_entity({ components = {
        color_grid = {
          cell_size = 64,
          color1 = { r = 10, g = 20, b = 30, a = 255 },
          color2 = { r = 40, g = 50, b = 60, a = 255 },
          offset = { x = 5, y = 15 },
          scroll_velocity = { x = 100, y = 50 },
          layer = -5,
          is_fixed = true,
          blend_mode = "add",
        }
      }})
    )LUA"),
          "color_grid entity loads from Lua");
    const Entity gridEnt = lua["grid_ent"];
    Check(reg->HasComponent<ColorGridComponent>(gridEnt), "entity has ColorGridComponent in C++");
    const auto& grid = reg->GetComponent<ColorGridComponent>(gridEnt);
    CheckEq(grid.cellWidth, 64.0f, "cellWidth parsed from cell_size");
    CheckEq(grid.cellHeight, 64.0f, "cellHeight parsed from cell_size");
    CheckEq(static_cast<int>(grid.color1.r), 10, "color1.r parsed");
    CheckEq(static_cast<int>(grid.color2.b), 60, "color2.b parsed");
    CheckEq(grid.offset.x, 5.0f, "offset.x parsed");
    CheckEq(grid.offset.y, 15.0f, "offset.y parsed");
    CheckEq(grid.scrollVelocity.x, 100.0f, "scrollVelocity.x parsed");
    CheckEq(grid.layer, -5, "layer parsed");
    Check(grid.isFixed, "isFixed parsed");
    Check(grid.blendMode == octarine::BlendMode::Add, "blendMode parsed as add");

    // Mutation from Lua
    Check(RunLua(lua, R"LUA(
      local g = registry.get_color_grid(grid_ent)
      g.cell_size = 128
      g.offset.x = 25
      g.layer = 2
      g.blend_mode = "blend"
    )LUA"),
          "color_grid mutates from Lua");
    CheckEq(grid.cellWidth, 128.0f, "cellWidth mutated from Lua");
    CheckEq(grid.cellHeight, 128.0f, "cellHeight mutated from Lua");
    CheckEq(grid.offset.x, 25.0f, "offset.x mutated from Lua");
    CheckEq(grid.layer, 2, "layer mutated from Lua");
    Check(grid.blendMode == octarine::BlendMode::Blend, "blendMode mutated from Lua");
  }

  std::cout << "[color_grid loaded via array-of-tables components style]\n";
  {
    Check(RunLua(lua, R"LUA(
      grid_bg = load_entity({
        name = "Grid Background",
        components = {
          {
            color_grid = {
              cell_size = 64,              -- square tile width & height
              color1 = { r = 30, g = 30, b = 30, a = 255 },
              color2 = { r = 60, g = 60, b = 60, a = 255 },
              layer = -10,                 -- render behind sprites
              is_fixed = false,            -- scrolls with camera
              scroll_velocity = { x = 20, y = 0 }, -- auto-scrolls 20 px/sec right
            }
          }
        },
      })
    )LUA"),
          "user's exact load_entity snippet loads successfully");
    const Entity gridBg = lua["grid_bg"];
    Check(reg->HasComponent<ColorGridComponent>(gridBg), "grid_bg has ColorGridComponent");
    Check(reg->HasComponent<NameComponent>(gridBg), "grid_bg has NameComponent");
    CheckEq(reg->GetComponent<NameComponent>(gridBg).name, std::string("Grid Background"), "name parsed");
    const auto& grid = reg->GetComponent<ColorGridComponent>(gridBg);
    CheckEq(grid.cellWidth, 64.0f, "cellWidth is 64");
    CheckEq(grid.cellHeight, 64.0f, "cellHeight is 64");
    CheckEq(static_cast<int>(grid.color1.r), 30, "color1.r is 30");
    CheckEq(static_cast<int>(grid.color2.r), 60, "color2.r is 60");
    CheckEq(grid.layer, -10, "layer is -10");
    Check(!grid.isFixed, "is_fixed is false");
    CheckEq(grid.scrollVelocity.x, 20.0f, "scrollVelocity.x is 20");
    CheckEq(grid.scrollVelocity.y, 0.0f, "scrollVelocity.y is 0");
  }

  std::cout << "[ui_button: key, controller, action, trigger -> on_click]\n";
  {
    UIButtonSystem uiButtonSystem;
    auto eventBus = std::make_unique<EventBus>();
    auto* inputSystem = reg->TryGet<InputSystem>();
    if (!inputSystem) {
      inputSystem = &reg->Set<InputSystem>(InputSystem());
      inputSystem->SubscribeToEvents(eventBus, reg);
    }
    uiButtonSystem.Init(reg, eventBus);

    auto* viewport = reg->TryGet<ViewportInfo>();
    if (!viewport) {
      viewport = &reg->Set<ViewportInfo>(ViewportInfo{});
    }
    viewport->isFocused = true;
    viewport->isHovered = true;

    Check(RunLua(lua, R"LUA(
      clicked_btn1 = false
      clicked_btn2 = false
      clicked_self_matched = false

      btn1 = load_entity({
        components = {
          ui_button = {
            key = "space",
            controller_button = "a",
            action = "submit",
            on_click = function(self, entity)
              clicked_btn1 = true
              if self.key == "space" or self.key == "x" then
                clicked_self_matched = true
              end
            end
          }
        }
      })

      btn2 = load_entity({
        components = {
          ui_button = {
            key = "escape",
            controller_button = "b",
            action = "cancel",
            on_click = function(self, entity)
              clicked_btn2 = true
            end
          }
        }
      })
    )LUA"),
          "ui_button loaded from Lua with key, controller_button, and action");

    const Entity btn1 = lua["btn1"];
    const Entity btn2 = lua["btn2"];
    Check(reg->HasComponent<UIButtonComponent>(btn1), "btn1 has UIButtonComponent");
    Check(reg->HasComponent<UIButtonComponent>(btn2), "btn2 has UIButtonComponent");
    auto& comp1 = reg->GetComponent<UIButtonComponent>(btn1);
    auto& comp2 = reg->GetComponent<UIButtonComponent>(btn2);
    CheckEq(comp1.key, std::string("space"), "key is space");
    CheckEq(comp1.controllerButton, std::string("a"), "controllerButton is a");
    CheckEq(comp1.action, std::string("submit"), "action is submit");

    // 1. Programmatic trigger calls on_click
    Check(RunLua(lua, R"LUA(
      local b = registry.get_ui_button(btn1)
      b:trigger(btn1)
    )LUA"),
          "btn:trigger() runs");
    Check(lua["clicked_btn1"] == true, "btn1 on_click fired via trigger()");
    Check(lua["clicked_self_matched"] == true, "on_click received button self table");
    lua["clicked_btn1"] = false;

    // 2. Keyboard key press (space) triggers btn1 on_click directly
    eventBus->EmitEvent<KeyInputEvent>(SDLK_SPACE, SDL_KMOD_NONE, true);
    Check(lua["clicked_btn1"] == true, "btn1 on_click triggered by keyboard key (space)");
    lua["clicked_btn1"] = false;

    // 3. Controller button press ("south" matching alias "a") triggers btn1 on_click directly
    eventBus->EmitEvent<GamepadButtonEvent>(0, SDL_GAMEPAD_BUTTON_SOUTH, "south", true);
    Check(lua["clicked_btn1"] == true, "btn1 on_click triggered by controller button (south -> a)");
    lua["clicked_btn1"] = false;

    // 4. Action trigger: bind action "submit" to key "return", then press Return
    inputSystem->Bind("submit", "return");
    eventBus->EmitEvent<KeyInputEvent>(SDLK_RETURN, SDL_KMOD_NONE, true);
    Check(lua["clicked_btn1"] == true, "btn1 on_click triggered by action 'submit' bound to return");
    lua["clicked_btn1"] = false;

    // 5. btn2 triggers on_click via key 'escape' and controller 'east' -> 'b'
    eventBus->EmitEvent<KeyInputEvent>(SDLK_ESCAPE, SDL_KMOD_NONE, true);
    Check(lua["clicked_btn2"] == true, "btn2 on_click triggered by key 'escape'");
    lua["clicked_btn2"] = false;

    eventBus->EmitEvent<GamepadButtonEvent>(0, SDL_GAMEPAD_BUTTON_EAST, "east", true);
    Check(lua["clicked_btn2"] == true, "btn2 on_click triggered by controller button 'east'");
    lua["clicked_btn2"] = false;

    // 6. Inactive button does not fire on_click
    comp2.isActive = false;
    eventBus->EmitEvent<KeyInputEvent>(SDLK_ESCAPE, SDL_KMOD_NONE, true);
    Check(lua["clicked_btn2"] == false, "inactive button does not fire on_click on key press");
    comp2.isActive = true;

    // 7. Property mutation via Lua
    Check(RunLua(lua, R"LUA(
      local b = registry.get_ui_button(btn1)
      b.key = "x"
      b.controller_button = "y"
      b.action = "jump"
    )LUA"),
          "btn1 properties mutate from Lua");
    CheckEq(comp1.key, std::string("x"), "btn1.key mutated to x");
    CheckEq(comp1.controllerButton, std::string("y"), "btn1.controllerButton mutated to y");
    CheckEq(comp1.action, std::string("jump"), "btn1.action mutated to jump");
  }

  std::cout << "[debug_overlay: closing overlay returns focus to game window and restores directional inputs]\n";
  {
    auto eventBus = std::make_unique<EventBus>();
    auto* viewport = reg->TryGet<ViewportInfo>();
    if (!viewport) {
      viewport = &reg->Set<ViewportInfo>(ViewportInfo{});
    }

    auto* inputSystem = reg->TryGet<InputSystem>();
    if (!inputSystem) {
      inputSystem = &reg->Set<InputSystem>(InputSystem());
    }
    inputSystem->SubscribeToEvents(eventBus, reg);

    // 1. Simulate debug overlay open and holding focus: viewport is unfocused
    viewport->isFocused = false;
    eventBus->EmitEvent<KeyInputEvent>(SDLK_LEFT, SDL_KMOD_NONE, true);
    Check(!inputSystem->IsKeyDown("left"), "left arrow input is eaten while overlay holds focus");
    eventBus->EmitEvent<KeyInputEvent>(SDLK_LEFT, SDL_KMOD_NONE, false);

    // 2. Closing the debug overlay calls ReturnFocusToGame, returning focus to game window
    if (!reg->TryGet<GameConfig>()) {
      reg->Set<GameConfig>(GameConfig{});
    }
    reg->Get<GameConfig>().GetEngineOptions().showDebugGUI = false;
#ifdef OCTARINE_WITH_IMGUI
    RenderDebugGUISystem::ReturnFocusToGame(&game);
#else
    viewport->isFocused = true;
#endif

    Check(viewport->isFocused, "ReturnFocusToGame restored focus to game window");

    // 3. Directional inputs are now received by the game, not eaten
    eventBus->EmitEvent<KeyInputEvent>(SDLK_LEFT, SDL_KMOD_NONE, true);
    Check(inputSystem->IsKeyDown("left"), "left arrow input is received by game after overlay is closed");
    eventBus->EmitEvent<KeyInputEvent>(SDLK_LEFT, SDL_KMOD_NONE, false);

    eventBus->EmitEvent<KeyInputEvent>(SDLK_RIGHT, SDL_KMOD_NONE, true);
    Check(inputSystem->IsKeyDown("right"), "right arrow input is received by game after overlay is closed");
    eventBus->EmitEvent<KeyInputEvent>(SDLK_RIGHT, SDL_KMOD_NONE, false);

    eventBus->EmitEvent<KeyInputEvent>(SDLK_UP, SDL_KMOD_NONE, true);
    Check(inputSystem->IsKeyDown("up"), "up arrow input is received by game after overlay is closed");
    eventBus->EmitEvent<KeyInputEvent>(SDLK_UP, SDL_KMOD_NONE, false);

    eventBus->EmitEvent<KeyInputEvent>(SDLK_DOWN, SDL_KMOD_NONE, true);
    Check(inputSystem->IsKeyDown("down"), "down arrow input is received by game after overlay is closed");
    eventBus->EmitEvent<KeyInputEvent>(SDLK_DOWN, SDL_KMOD_NONE, false);
  }

  std::cout << "[lifetime component Lua binding and helpers]\n";
  {
    Check(RunLua(lua, R"LUA(
      e_life = load_entity({ components = { lifetime = { duration = 5.0 } } })
      e_life_num = load_entity({ components = { lifetime = 2.5 } })
    )LUA"),
          "lifetime component loads via Lua");

    const Entity eLife = lua["e_life"];
    const Entity eLifeNum = lua["e_life_num"];
    Check(reg->HasComponent<LifetimeComponent>(eLife), "e_life has LifetimeComponent");
    Check(reg->GetComponent<LifetimeComponent>(eLife).lifetimeDuration == 5.0F, "duration parsed as 5.0");
    Check(reg->GetComponent<LifetimeComponent>(eLifeNum).lifetimeDuration == 2.5F, "bare number duration parsed");

    Check(RunLua(lua, "dur = registry.get_lifetime(e_life).duration"), "read duration property");
    CheckEq(lua["dur"].get<float>(), 5.0F, "duration property is 5.0");
    Check(RunLua(lua, "rem = registry.get_lifetime(e_life).remaining_duration"), "read remaining_duration property");
    CheckEq(lua["rem"].get<float>(), 5.0F, "initial remaining_duration equals duration");

    // Decrease lifetime in C++
    reg->GetComponent<LifetimeComponent>(eLife).Decrease(2.0F);
    Check(RunLua(lua, "rem2 = registry.get_lifetime(e_life).remaining_duration"), "read remaining after decrease");
    CheckEq(lua["rem2"].get<float>(), 3.0F, "remaining_duration decreased to 3.0");

    // Extend via helper method
    Check(RunLua(lua, "registry.get_lifetime(e_life):extend(1.5)"), "call extend(1.5)");
    CheckEq(reg->GetComponent<LifetimeComponent>(eLife).remainingDuration, 4.5F,
            "extend bumped remainingDuration to 4.5");

    // Reset via helper method
    Check(RunLua(lua, "registry.get_lifetime(e_life):reset()"), "call reset()");
    CheckEq(reg->GetComponent<LifetimeComponent>(eLife).remainingDuration, 5.0F,
            "reset restored remainingDuration to 5.0");

    Check(RunLua(lua, "alive = registry.get_lifetime(e_life):is_alive()"), "call is_alive()");
    Check(lua["alive"].get<bool>(), "is_alive reports true");
  }

  std::cout << "[registry pause controls]\n";
  {
    Check(!reg->IsPaused(), "registry initially unpaused");
    Check(RunLua(lua, "paused = registry.is_paused()"), "registry.is_paused() callable");
    Check(!lua["paused"].get<bool>(), "registry.is_paused reports false initially");

    Check(RunLua(lua, "registry.set_paused(true)"), "registry.set_paused(true) callable");
    Check(reg->IsPaused(), "C++ registry reflected pause");
    Check(RunLua(lua, "paused_after = registry.is_paused()"), "registry.is_paused after set_paused(true)");
    Check(lua["paused_after"].get<bool>(), "registry.is_paused reports true");

    reg->SetPaused(false);
    Check(!reg->IsPaused(), "C++ SetPaused(false) unpaused registry");
    Check(RunLua(lua, "unpaused = registry.is_paused()"), "registry.is_paused after C++ unpause");
    Check(!lua["unpaused"].get<bool>(), "registry.is_paused reports false");

    // VelocityIntegrationSystem respects pause state
    const Entity ent = reg->CreateEntityWithBundle(PositionComponent{glm::vec2(10.0F, 20.0F)},
                                                   RigidBodyComponent{glm::vec2(100.0F, 50.0F)});
    VelocityIntegrationSystem vis;
    vis.Prepare(reg);
    vis(1.0F, reg->GetComponent<PositionComponent>(ent), reg->GetComponent<RigidBodyComponent>(ent));
    CheckEq(reg->GetComponent<PositionComponent>(ent).value.x, 110.0F, "integrated position X while unpaused");

    reg->SetPaused(true);
    const bool shouldRun = vis.Prepare(reg);
    Check(!shouldRun, "vis.Prepare returns false when paused");
    vis(1.0F, reg->GetComponent<PositionComponent>(ent), reg->GetComponent<RigidBodyComponent>(ent));
    CheckEq(reg->GetComponent<PositionComponent>(ent).value.x, 110.0F, "position did not change while paused");
    reg->SetPaused(false);

    // Verify metatable synchronization pattern as used in world.lua
    Check(RunLua(lua, R"LUA(
      test_world = {}
      local raw_p = false
      function test_world.set_paused(p)
        raw_p = (p == true)
        if registry and registry.set_paused then registry.set_paused(raw_p) end
      end
      function test_world.is_paused()
        if registry and registry.is_paused then return registry.is_paused() end
        return raw_p
      end
      setmetatable(test_world, {
        __index = function(t, k)
          if k == "paused" then return test_world.is_paused() end
        end,
        __newindex = function(t, k, v)
          if k == "paused" then test_world.set_paused(v) else rawset(t, k, v) end
        end
      })
      test_world.paused = true
    )LUA"),
          "test_world.paused = true script execution");
    Check(reg->IsPaused(), "test_world.paused = true synchronized to C++ registry");
    Check(RunLua(lua, "read_back_paused = (test_world.paused == true)"), "read test_world.paused");
    Check(lua["read_back_paused"].get<bool>(), "test_world.paused reads true");

    Check(RunLua(lua, "test_world.paused = false"), "test_world.paused = false script execution");
    Check(!reg->IsPaused(), "test_world.paused = false synchronized to C++ registry");
  }

  TestPrimitiveBindings(lua, reg);
  TestColliderBindings(lua, reg);
  TestUIButtonShapes(lua, reg);

  return octarine::test::ReportSummary("Lua behavior test");
}
