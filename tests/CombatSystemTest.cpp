#include <cmath>
#include <memory>

#include "Components/BoxColliderComponent.h"
#include "Components/ContactDamageComponent.h"
#include "Components/EnemySteeringComponent.h"
#include "Components/EntityMaskComponent.h"
#include "Components/HealthComponent.h"
#include "Components/HomingComponent.h"
#include "Components/LifetimeComponent.h"
#include "Components/PositionComponent.h"
#include "Components/ProjectileComponent.h"
#include "Components/ProjectileEmitterComponent.h"
#include "Components/RetaliationComponent.h"
#include "Components/RigidBodyComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "Components/WeaponZoneComponent.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Events/CollisionBatchEvent.h"
#include "Events/CollisionExitBatchEvent.h"
#include "Events/CombatDamagePlayerEvent.h"
#include "Events/CombatDeathEvent.h"
#include "Events/CombatHitEvent.h"
#include "Systems/CombatSystem.h"
#include "Systems/DamageSystem.h"
#include "Systems/EnemySteeringSystem.h"
#include "Systems/EntityPoolSystem.h"
#include "Systems/ProjectileEmitSystem.h"
#include "Systems/RetaliationSystem.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

struct DamageTracker {
  int totalDamage = 0;
  int hitCount = 0;
  Entity lastTarget{};

  void OnDamage(CombatDamagePlayerEvent& ev) {
    totalDamage += ev.damage;
    hitCount++;
    lastTarget = ev.player;
  }
};

void TestPhantomContactDamageAndZeroing() {
  std::cout << "[CombatSystemTest] TestPhantomContactDamageAndZeroing\n";
  auto registry = std::make_unique<Registry>();
  auto eventBus = std::make_unique<EventBus>();
  DamageTracker tracker;
  auto sub = eventBus->SubscribeEvent(&tracker, &DamageTracker::OnDamage);
  (void)sub;

  CombatSystem combatSystem;
  combatSystem.Init(registry.get(), eventBus);

  Entity player = registry->CreateEntity();
  Entity enemy = registry->CreateEntity();

  ContactDamageComponent cd;
  cd.target_entity = player;
  cd.damage = 15.0f;
  cd.cooldown = 1.0f;
  cd.current_cooldown = 0.0f;
  cd.active_contacts_count = 0;
  registry->AddComponent(enemy, cd);

  // 1. Initial tick with no collision — should not deal damage.
  ContextFacade ctx(registry.get(), 0.1f, nullptr, &Internal::BulkGetComponent);
  combatSystem(ctx);
  CheckEq(tracker.hitCount, 0, "No damage dealt when no active contacts");

  // 2. Collision enter.
  std::vector<std::pair<Entity, Entity>> enterPairs = {{enemy, player}};
  eventBus->EmitEvent<CollisionBatchEvent>(enterPairs);

  auto& enemyCd = registry->GetComponent<ContactDamageComponent>(enemy);
  CheckEq(enemyCd.active_contacts_count, static_cast<uint8_t>(1), "active_contacts_count is 1 on enter");
  Check(enemyCd.active_contacts[0] == player, "Player stored in active_contacts[0]");

  // Immediate damage on initial contact if cooldown <= 0
  CheckEq(tracker.hitCount, 1, "Immediate damage applied on first contact when cooldown is ready");
  CheckEq(tracker.totalDamage, 15, "Initial damage amount is 15");
  CheckEq(enemyCd.current_cooldown, 1.0f, "Cooldown reset to 1.0s");

  // 3. Collision exit — player leaves contact area.
  std::vector<std::pair<Entity, Entity>> exitPairs = {{enemy, player}};
  eventBus->EmitEvent<CollisionExitBatchEvent>(exitPairs);

  CheckEq(enemyCd.active_contacts_count, static_cast<uint8_t>(0), "active_contacts_count is 0 on exit");
  Check(enemyCd.active_contacts[0] == Entity{}, "Slot 0 is zeroed out on removal so it is not stale");

  // 4. Advance time past cooldown with active_contacts_count == 0.
  // Cooldown expires, but no active contacts exist. MUST NOT deal phantom damage!
  ContextFacade ctx2(registry.get(), 1.5f, nullptr, &Internal::BulkGetComponent);
  combatSystem(ctx2);
  CheckEq(tracker.hitCount, 1, "No phantom damage dealt after cooldown expires when count is 0");
}

void TestSingleCooldownDecrement() {
  std::cout << "[CombatSystemTest] TestSingleCooldownDecrement\n";
  auto registry = std::make_unique<Registry>();
  auto eventBus = std::make_unique<EventBus>();

  CombatSystem combatSystem;
  combatSystem.Init(registry.get(), eventBus);

  Entity player = registry->CreateEntity();
  Entity enemy = registry->CreateEntity();

  // Enemy has both HealthComponent and ContactDamageComponent.
  registry->AddComponent(enemy, HealthComponent(100));

  ContactDamageComponent cd;
  cd.target_entity = player;
  cd.damage = 10.0f;
  cd.cooldown = 1.0f;
  cd.current_cooldown = 0.5f;
  cd.active_contacts_count = 0;
  registry->AddComponent(enemy, cd);

  const float dt = 0.1f;
  ContextFacade ctx(registry.get(), dt, nullptr, &Internal::BulkGetComponent);
  combatSystem(ctx);

  const auto& enemyCd = registry->GetComponent<ContactDamageComponent>(enemy);
  const float expectedCooldown = 0.5f - dt;
  Check(std::fabs(enemyCd.current_cooldown - expectedCooldown) < 1e-4f,
        "Cooldown decremented exactly once by dt across contact_query_ and health_query_");
}

void TestHomingRetargetIgnoresDeadEnemies() {
  std::cout << "[CombatSystemTest] TestHomingRetargetIgnoresDeadEnemies\n";
  auto registry = std::make_unique<Registry>();
  auto eventBus = std::make_unique<EventBus>();

  CombatSystem combatSystem;
  combatSystem.Init(registry.get(), eventBus);

  Entity enemyTag = registry->TagId("enemy");

  // Create weapon entity with homing (piercing, retarget_on_hit)
  Entity weapon = registry->CreateEntity();
  registry->AddComponent(weapon, PositionComponent(glm::vec2(0.0f, 0.0f)));
  WeaponZoneComponent wz;
  wz.damage = 50.0f;
  wz.pierce = 2;
  wz.rehit_cooldown = 0.5f;
  registry->AddComponent(weapon, wz);

  HomingComponent homing;
  homing.retarget_on_hit = true;
  registry->AddComponent(weapon, homing);

  // Target 1: will be hit and take fatal damage
  Entity target1 = registry->CreateEntity();
  registry->AddTag(target1, enemyTag);
  registry->AddComponent(target1, PositionComponent(glm::vec2(10.0f, 0.0f)));
  registry->AddComponent(target1, HealthComponent(50));

  // Dead enemy nearby
  Entity deadEnemy = registry->CreateEntity();
  registry->AddTag(deadEnemy, enemyTag);
  registry->AddComponent(deadEnemy, PositionComponent(glm::vec2(15.0f, 0.0f)));
  HealthComponent deadHealth(50);
  deadHealth.Damage(50);  // current_health = 0 -> IsDead() == true
  registry->AddComponent(deadEnemy, deadHealth);

  // Alive enemy farther away
  Entity aliveEnemy = registry->CreateEntity();
  registry->AddTag(aliveEnemy, enemyTag);
  registry->AddComponent(aliveEnemy, PositionComponent(glm::vec2(30.0f, 0.0f)));
  registry->AddComponent(aliveEnemy, HealthComponent(50));

  // Register weapon contact with target1
  std::vector<std::pair<Entity, Entity>> enterPairs1 = {{weapon, target1}};
  eventBus->EmitEvent<CollisionBatchEvent>(enterPairs1);

  ContextFacade ctx(registry.get(), 0.016f, nullptr, &Internal::BulkGetComponent);
  combatSystem(ctx);

  const auto& updatedHoming = registry->GetComponent<HomingComponent>(weapon);
  Check(updatedHoming.target != deadEnemy, "Homing retarget does NOT select dead enemy");
  Check(updatedHoming.target == aliveEnemy, "Homing retarget selects alive enemy");
}

void TestWeaponZonePierceAndConsumption() {
  std::cout << "[CombatSystemTest] TestWeaponZonePierceAndConsumption\n";
  auto registry = std::make_unique<Registry>();
  auto eventBus = std::make_unique<EventBus>();

  CombatSystem combatSystem;
  combatSystem.Init(registry.get(), eventBus);

  Entity enemyTag = registry->TagId("enemy");

  Entity weapon = registry->CreateEntity();
  registry->AddComponent(weapon, PositionComponent(glm::vec2(0.0f, 0.0f)));
  WeaponZoneComponent wz;
  wz.damage = 10.0f;
  wz.pierce = 1;
  wz.consumed = false;
  registry->AddComponent(weapon, wz);

  Entity target = registry->CreateEntity();
  registry->AddTag(target, enemyTag);
  registry->AddComponent(target, PositionComponent(glm::vec2(5.0f, 0.0f)));
  registry->AddComponent(target, HealthComponent(100));

  std::vector<std::pair<Entity, Entity>> enterPairs2 = {{weapon, target}};
  eventBus->EmitEvent<CollisionBatchEvent>(enterPairs2);

  ContextFacade ctx(registry.get(), 0.016f, nullptr, &Internal::BulkGetComponent);
  combatSystem(ctx);

  const auto& weaponZone = registry->GetComponent<WeaponZoneComponent>(weapon);
  Check(weaponZone.consumed, "WeaponZone consumed flag is set to true after pierce reaches 0");
  CheckEq(weaponZone.pierce, 0, "Pierce decremented to 0");
  CheckEq(registry->GetComponent<HealthComponent>(target).current_health, 90, "Target health reduced by 10");
}

void TestDeadTargetRemovedFromContactDamage() {
  std::cout << "[CombatSystemTest] TestDeadTargetRemovedFromContactDamage\n";
  auto registry = std::make_unique<Registry>();
  auto eventBus = std::make_unique<EventBus>();
  CombatSystem combatSystem;
  combatSystem.Init(registry.get(), eventBus);

  Entity player = registry->CreateEntity();
  Entity enemy = registry->CreateEntity();

  ContactDamageComponent cd;
  cd.target_entity = player;
  cd.damage = 10.0f;
  cd.cooldown = 1.0f;
  cd.current_cooldown = 0.5f;
  cd.active_contacts_count = 0;
  registry->AddComponent(enemy, cd);

  std::vector<std::pair<Entity, Entity>> enterPairs = {{enemy, player}};
  eventBus->EmitEvent<CollisionBatchEvent>(enterPairs);

  auto& enemyCd = registry->GetComponent<ContactDamageComponent>(enemy);
  CheckEq(enemyCd.active_contacts_count, static_cast<uint8_t>(1), "Contact added");

  registry->BlamEntity(player);
  Check(!registry->IsAlive(player), "Player is destroyed");

  ContextFacade ctx(registry.get(), 1.0f, nullptr, &Internal::BulkGetComponent);
  combatSystem(ctx);

  CheckEq(enemyCd.active_contacts_count, static_cast<uint8_t>(0), "Dead player removed from active contacts");
  Check(enemyCd.active_contacts[0] == Entity{}, "Vacated contact slot zeroed");
}

void TestHazardEntityAndCollisionMasks() {
  std::cout << "[CombatSystemTest] TestHazardEntityAndCollisionMasks\n";
  Registry reg;
  reg.Set<EntityPoolManager>(EntityPoolManager());
  auto bus = std::make_unique<EventBus>();

  RetaliationSystem retalSystem;
  retalSystem.Init(reg, bus.get());

  ProjectileEmitSystem emitSystem;
  emitSystem.Init(reg);

  Entity attacker = reg.CreateEntity();
  reg.AddComponent(attacker, PositionComponent{glm::vec2(100.0f, 100.0f)});
  RetaliationComponent retal;
  retal.size = 32.0f;
  retal.damage = 15.0f;
  retal.lifetime = 4.0f;
  retal.tick_interval = 0.5f;
  retal.color = octarine::Color{80, 200, 40, 160};
  reg.AddComponent(attacker, retal);

  Entity dashWeapon = reg.CreateEntity();
  WeaponZoneComponent wz;
  wz.is_dash_weapon = true;
  reg.AddComponent(dashWeapon, wz);

  CombatHitEvent hitEv{dashWeapon, attacker};
  retalSystem.OnCombatHit(hitEv);

  auto query = reg.CreateQuery<ContactDamageComponent, BoxColliderComponent, EntityMaskComponent>();
  query->Update();
  bool foundRetalHazard = false;
  query->ForEach([&](Entity e, ContactDamageComponent&, BoxColliderComponent& box, EntityMaskComponent& mask) {
    if (e != attacker) {
      foundRetalHazard = true;
      CheckEq(box.collisionMask.to_ullong(), 1ULL, "Retaliation hazard collider mask is 1 (PLAYER)");
      CheckEq(mask.mask.to_ullong(), 2ULL, "Retaliation hazard entity mask is 2 (ENEMY)");
    }
  });
  Check(foundRetalHazard, "Retaliation hazard was spawned");

  Entity emitterEnt = reg.CreateEntity();
  reg.AddComponent(emitterEnt, PositionComponent{glm::vec2(200.0f, 200.0f)});
  ProjectileEmitterComponent emitterComp;
  emitterComp.is_hazard = true;
  emitterComp.frequency = 0.5f;
  emitterComp.count_down_timer = 0.001f;
  emitterComp.size = 24.0f;
  emitterComp.damage = 10;
  emitterComp.duration = 5.0f;
  reg.AddComponent(emitterEnt, emitterComp);

  ContextFacade ctx(&reg, 0.05f, nullptr, &Internal::BulkGetComponent);
  emitSystem(ctx);

  query->Update();
  int hazardCount = 0;
  query->ForEach([&](Entity e, ContactDamageComponent&, BoxColliderComponent& box, EntityMaskComponent& mask) {
    if (e != attacker) {
      hazardCount++;
      CheckEq(box.collisionMask.to_ullong(), 1ULL, "Hazard collider mask is 1 (PLAYER)");
      CheckEq(mask.mask.to_ullong(), 2ULL, "Hazard entity mask is 2 (ENEMY)");
    }
  });
  CheckEq(hazardCount, 2, "Both retaliation and projectile hazards have mask 2 and collisionMask 1");
}

void TestDamageSystemPlayerWithoutHealthAndTags() {
  std::cout << "[CombatSystemTest] TestDamageSystemPlayerWithoutHealthAndTags\n";
  Registry reg;
  auto bus = std::make_unique<EventBus>();
  DamageTracker tracker;
  auto sub = bus->SubscribeEvent(&tracker, &DamageTracker::OnDamage);
  (void)sub;

  DamageSystem damageSystem;
  damageSystem.Init(&reg, bus);

  const Entity projTag = reg.Tag<ProjectileTag>();
  const Entity enemyTag = reg.TagId("enemy");
  const Entity enemiesTag = reg.TagId("enemies");

  Entity player = reg.CreateEntity();
  Entity enemyProj = reg.CreateEntity();
  reg.AddTag(enemyProj, projTag);
  reg.AddComponent(enemyProj, ProjectileComponent{25, 5.0f, player});

  std::vector<std::pair<Entity, Entity>> pairs1 = {{enemyProj, player}};
  bus->EmitEvent<CollisionBatchEvent>(pairs1);

  CheckEq(tracker.hitCount, 1, "DamageSystem hits target lacking HealthComponent");
  CheckEq(tracker.totalDamage, 25, "Emits CombatDamagePlayerEvent with 25 damage");
  Check(tracker.lastTarget == player, "Target entity matches player");

  Entity enemy1 = reg.CreateEntity();
  reg.AddTag(enemy1, enemyTag);
  reg.AddComponent(enemy1, HealthComponent(100, 100));

  Entity playerProj1 = reg.CreateEntity();
  reg.AddTag(playerProj1, projTag);
  reg.AddComponent(playerProj1, ProjectileComponent{30, 5.0f, Entity{}});

  std::vector<std::pair<Entity, Entity>> pairs2 = {{playerProj1, enemy1}};
  bus->EmitEvent<CollisionBatchEvent>(pairs2);
  CheckEq(reg.GetComponent<HealthComponent>(enemy1).current_health, 70, "Enemy with 'enemy' tag damaged");

  Entity enemy2 = reg.CreateEntity();
  reg.AddTag(enemy2, enemiesTag);
  reg.AddComponent(enemy2, HealthComponent(100, 100));

  Entity playerProj2 = reg.CreateEntity();
  reg.AddTag(playerProj2, projTag);
  reg.AddComponent(playerProj2, ProjectileComponent{40, 5.0f, Entity{}});

  std::vector<std::pair<Entity, Entity>> pairs3 = {{playerProj2, enemy2}};
  bus->EmitEvent<CollisionBatchEvent>(pairs3);
  CheckEq(reg.GetComponent<HealthComponent>(enemy2).current_health, 60, "Enemy with 'enemies' tag damaged");
}

void TestEnemySteeringSineFlyerFlyThroughAndCharge() {
  std::cout << "[CombatSystemTest] TestEnemySteeringSineFlyerFlyThroughAndCharge\n";
  Registry reg;
  EnemySteeringSystem steeringSystem;

  Entity player = reg.CreateEntity();
  reg.AddComponent(player, PositionComponent{glm::vec2(500.0f, 0.0f)});

  Entity sineEnemy = reg.CreateEntity();
  reg.AddComponent(sineEnemy, PositionComponent{glm::vec2(0.0f, 0.0f)});
  reg.AddComponent(sineEnemy, RigidBodyComponent{});
  EnemySteeringComponent sineSteer;
  sineSteer.type = EnemyBehaviorType::SineFlyer;
  sineSteer.speed = 100.0f;
  sineSteer.target_entity = player;
  reg.AddComponent(sineEnemy, sineSteer);

  Entity flyEnemy = reg.CreateEntity();
  reg.AddComponent(flyEnemy, PositionComponent{glm::vec2(0.0f, 0.0f)});
  reg.AddComponent(flyEnemy, RigidBodyComponent{});
  EnemySteeringComponent flySteer;
  flySteer.type = EnemyBehaviorType::FlyThrough;
  flySteer.speed = 150.0f;
  flySteer.target_entity = player;
  reg.AddComponent(flyEnemy, flySteer);

  Entity charger = reg.CreateEntity();
  reg.AddComponent(charger, PositionComponent{glm::vec2(200.0f, 0.0f)});
  reg.AddComponent(charger, RigidBodyComponent{});
  HealthComponent chargerHealth(100);
  chargerHealth.base_color_r = 100;
  chargerHealth.base_color_g = 100;
  chargerHealth.base_color_b = 200;
  chargerHealth.base_color_a = 255;
  reg.AddComponent(charger, chargerHealth);
  reg.AddComponent(charger,
                   SquarePrimitiveComponent(glm::vec2(0.0f), 24, 24, 0.0f, octarine::Color{100, 100, 200, 255}, false));
  EnemySteeringComponent chargeSteer;
  chargeSteer.type = EnemyBehaviorType::Charge;
  chargeSteer.speed = 80.0f;
  chargeSteer.charge_range = 350.0f;
  chargeSteer.windup_time = 0.4f;
  chargeSteer.charge_speed = 300.0f;
  chargeSteer.target_entity = player;
  reg.AddComponent(charger, chargeSteer);

  ContextFacade ctx(&reg, 0.016f, nullptr, &Internal::BulkGetComponent);
  steeringSystem(ctx);

  const auto& sineRb = reg.GetComponent<RigidBodyComponent>(sineEnemy);
  Check(sineRb.velocity.x > 0.0f, "SineFlyer has positive forward velocity toward target");

  const auto& flyRb = reg.GetComponent<RigidBodyComponent>(flyEnemy);
  Check(std::fabs(flyRb.velocity.x - 150.0f) < 1e-3f, "FlyThrough moves directly toward target at configured speed");

  const auto& updatedChargeSteer = reg.GetComponent<EnemySteeringComponent>(charger);
  CheckEq(updatedChargeSteer.state, 1, "Charger entered windup state (1)");
  const auto& chargerSq = reg.GetComponent<SquarePrimitiveComponent>(charger);
  CheckEq(chargerSq.color.r, static_cast<unsigned char>(255), "Telegraph yellow flash r == 255");
  CheckEq(chargerSq.color.g, static_cast<unsigned char>(220), "Telegraph yellow flash g == 220");
  CheckEq(chargerSq.color.b, static_cast<unsigned char>(40), "Telegraph yellow flash b == 40");

  ContextFacade ctx2(&reg, 0.5f, nullptr, &Internal::BulkGetComponent);
  steeringSystem(ctx2);

  const auto& launchedSteer = reg.GetComponent<EnemySteeringComponent>(charger);
  CheckEq(launchedSteer.state, 2, "Charger entered charge state (2)");
  const auto& launchedRb = reg.GetComponent<RigidBodyComponent>(charger);
  Check(std::fabs(launchedRb.velocity.x - 300.0f) < 1e-3f, "Charger launched at charge_speed (300)");
  const auto& restoredSq = reg.GetComponent<SquarePrimitiveComponent>(charger);
  CheckEq(restoredSq.color.r, static_cast<unsigned char>(100), "Base color r restored on charge launch");
  CheckEq(restoredSq.color.b, static_cast<unsigned char>(200), "Base color b restored on charge launch");
}

}  // namespace

int main() {
  TestPhantomContactDamageAndZeroing();
  TestSingleCooldownDecrement();
  TestHomingRetargetIgnoresDeadEnemies();
  TestWeaponZonePierceAndConsumption();
  TestDeadTargetRemovedFromContactDamage();
  TestHazardEntityAndCollisionMasks();
  TestDamageSystemPlayerWithoutHealthAndTags();
  TestEnemySteeringSineFlyerFlyThroughAndCharge();

  return octarine::test::ReportSummary("CombatSystemTest");
}
