#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sol/sol.hpp>
#include <string>

#include "Components/CameraComponents.h"
#include "Components/ChunkResidentComponent.h"
#include "ECS/EntityPool.h"
#include "ECS/Query.h"
#include "ECS/Registry.h"
#include "Game/EngineContext.h"
#include "Game/GameConfig.h"
#include "General/Logger.h"
#include "Lua/LuaBindingContext.h"
#include "Lua/Modules/TilemapModuleLuaBinding.h"
#include "Renderer/RenderQueue.h"
#include "Systems/TilemapSystem.h"
#include "TestHarness.h"
#include "Tilemap/LdtkParser.h"
#include "Tilemap/TilemapBuilder.h"
#include "Tilemap/TilemapLoader.h"
#include "Tilemap/TilemapMath.h"

using octarine::test::Check;
using octarine::test::CheckEq;
using octarine::tilemap::ChunkTemplate;
using octarine::tilemap::FloorDiv;
using octarine::tilemap::GlobalTileToChunk;
using octarine::tilemap::GlobalTileToLocalTile;
using octarine::tilemap::HashChunkCoord;
using octarine::tilemap::kTileChasm;
using octarine::tilemap::kTileEmpty;
using octarine::tilemap::kTileHazard;
using octarine::tilemap::kTileMaskChasm;
using octarine::tilemap::kTileMaskGroundBlocked;
using octarine::tilemap::kTileMaskHazard;
using octarine::tilemap::kTileMaskSolid;
using octarine::tilemap::kTileSolid;
using octarine::tilemap::LdtkParser;
using octarine::tilemap::PositiveMod;
using octarine::tilemap::StitchMode;
using octarine::tilemap::TileLayerMesh;
using octarine::tilemap::TilemapBuilder;
using octarine::tilemap::TilemapData;
using octarine::tilemap::TilemapFormatLoader;
using octarine::tilemap::TilemapLoaderRegistry;
using octarine::tilemap::TilemapLoadOptions;
using octarine::tilemap::TilemapParseOptions;
using octarine::tilemap::TileQuadDesc;
using octarine::tilemap::TilesetDesc;
using octarine::tilemap::WorldToChunk;
using octarine::tilemap::WorldToGlobalTile;

namespace {

// 2-level synthetic LDtk JSON (each level is 4x4 tiles at 16px = 64x64px chunk for compact test assertions).
const char* kTestLdtkJson = R"JSON({
  "defaultGridSize": 16,
  "defs": {
    "tilesets": [
      {
        "uid": 1,
        "identifier": "CryptTileset",
        "relPath": "textures/crypt.png",
        "pxWid": 256,
        "pxHei": 256,
        "tileGridSize": 16
      }
    ]
  },
  "levels": [
    {
      "identifier": "Chunk_0_0",
      "worldX": 0,
      "worldY": 0,
      "pxWid": 64,
      "pxHei": 64,
      "fieldInstances": [
        { "__identifier": "weight", "__value": 2.0 }
      ],
      "layerInstances": [
        {
          "__identifier": "Entities",
          "__type": "Entities",
          "__gridSize": 16,
          "entityInstances": [
            {
              "__identifier": "Brazier",
              "px": [24, 24],
              "width": 16,
              "height": 16,
              "fieldInstances": [
                { "__identifier": "hp", "__value": 50 },
                { "__identifier": "loot", "__value": "obol_cache" }
              ]
            },
            {
              "__identifier": "Spawner",
              "px": [48, 48],
              "width": 16,
              "height": 16,
              "fieldInstances": []
            }
          ]
        },
        {
          "__identifier": "Props_YSort",
          "__type": "Tiles",
          "__gridSize": 16,
          "__tilesetDefUid": 1,
          "gridTiles": [
            { "px": [16, 16], "src": [0, 32], "f": 0 },
            { "px": [16, 32], "src": [0, 48], "f": 0 },
            { "px": [16, 48], "src": [0, 64], "f": 0 }
          ]
        },
        {
          "__identifier": "Ground_Overlay",
          "__type": "AutoLayer",
          "__gridSize": 16,
          "__tilesetDefUid": 1,
          "autoLayerTiles": [
            { "px": [0, 0], "src": [16, 0], "f": 1 },
            { "px": [16, 0], "src": [32, 0], "f": 2 },
            { "px": [32, 0], "src": [48, 0], "f": 3 }
          ]
        },
        {
          "__identifier": "Ground_Base",
          "__type": "Tiles",
          "__gridSize": 16,
          "__tilesetDefUid": 1,
          "gridTiles": [
            { "px": [0, 0], "src": [0, 0], "f": 0 },
            { "px": [16, 0], "src": [0, 0], "f": 0 }
          ]
        },
        {
          "__identifier": "Collisions",
          "__type": "IntGrid",
          "__cWid": 4,
          "__cHei": 4,
          "__gridSize": 16,
          "intGridCsv": [
            0, 1, 2, 3,
            0, 1, 2, 0,
            0, 0, 0, 0,
            0, 0, 0, 1
          ]
        }
      ]
    },
    {
      "identifier": "Chunk_1_0",
      "worldX": 64,
      "worldY": 0,
      "pxWid": 64,
      "pxHei": 64,
      "fieldInstances": [
        { "__identifier": "weight", "__value": 1.0 }
      ],
      "layerInstances": [
        {
          "__identifier": "Collisions",
          "__type": "IntGrid",
          "__cWid": 4,
          "__cHei": 4,
          "__gridSize": 16,
          "intGridCsv": [
            1, 1, 1, 1,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0
          ]
        }
      ]
    }
  ]
})JSON";

void TestNegativeCoordinateMath() {
  std::cout << "[math] FloorDiv and PositiveMod across negative boundaries\n";
  CheckEq(FloorDiv(0, 32), 0, "FloorDiv(0, 32) == 0");
  CheckEq(PositiveMod(0, 32), 0, "PositiveMod(0, 32) == 0");

  CheckEq(FloorDiv(-1, 32), -1, "FloorDiv(-1, 32) == -1");
  CheckEq(PositiveMod(-1, 32), 31, "PositiveMod(-1, 32) == 31");

  CheckEq(FloorDiv(-32, 32), -1, "FloorDiv(-32, 32) == -1");
  CheckEq(PositiveMod(-32, 32), 0, "PositiveMod(-32, 32) == 0");

  CheckEq(FloorDiv(-33, 32), -2, "FloorDiv(-33, 32) == -2");
  CheckEq(PositiveMod(-33, 32), 31, "PositiveMod(-33, 32) == 31");

  // World-to-tile & chunk boundaries at -0.1, -16.0, -16.1, -512.0, -512.1
  CheckEq(WorldToGlobalTile(0.0f, 16), 0, "WorldToGlobalTile(0.0) == 0");
  CheckEq(WorldToGlobalTile(-0.1f, 16), -1, "WorldToGlobalTile(-0.1) == -1");
  CheckEq(WorldToGlobalTile(-16.0f, 16), -1, "WorldToGlobalTile(-16.0) == -1");
  CheckEq(WorldToGlobalTile(-16.1f, 16), -2, "WorldToGlobalTile(-16.1) == -2");

  const int32_t gt = WorldToGlobalTile(-1.0f, 16);
  CheckEq(GlobalTileToChunk(gt, 32), -1, "global tile -1 is in chunk -1");
  CheckEq(GlobalTileToLocalTile(gt, 32), 31, "global tile -1 is local tile 31");

  CheckEq(WorldToChunk(-0.1f, 512), -1, "WorldToChunk(-0.1, 512) == -1");
  CheckEq(WorldToChunk(-512.0f, 512), -1, "WorldToChunk(-512.0, 512) == -1");
  CheckEq(WorldToChunk(-512.1f, 512), -2, "WorldToChunk(-512.1, 512) == -2");
}

void TestLdtkParserAndFlipBits() {
  std::cout << "[parser] LDtk layers, flip bits, UV inset, and vertical YSort grouping\n";
  const auto dataOpt = LdtkParser::ParseFromJsonString(kTestLdtkJson);
  Check(dataOpt.has_value(), "LdtkParser parsed synthetic JSON");
  if (!dataOpt.has_value()) return;

  const auto& data = *dataOpt;
  CheckEq(data.tileSize, 16, "tileSize == 16");
  CheckEq(data.chunkGridWidth, 4, "chunkGridWidth == 4");
  CheckEq(data.chunkGridHeight, 4, "chunkGridHeight == 4");
  CheckEq(data.templates.size(), static_cast<size_t>(2), "2 non-border templates parsed");

  const auto& tmpl0 = data.templates[0];
  CheckEq(tmpl0.staticLayers.size(), static_cast<size_t>(2), "Ground_Base + Ground_Overlay static layers");
  CheckEq(tmpl0.staticLayers[0].renderLayer, 0u, "Ground_Base on renderLayer 0");
  CheckEq(tmpl0.staticLayers[1].renderLayer, 0u, "Ground_Overlay on renderLayer 0");
  Check(tmpl0.staticLayers[0].renderDepth < tmpl0.staticLayers[1].renderDepth,
        "Ground_Overlay renderDepth orders above Ground_Base");

  // Verify flip bits on Ground_Overlay: tile 0 has f=1 (FlipX), tile 1 has f=2 (FlipY), tile 2 has f=3 (FlipX+FlipY).
  const auto& overlayVerts = tmpl0.staticLayers[1].localVertices;
  CheckEq(overlayVerts.size(), static_cast<size_t>(12), "3 overlay quads = 12 vertices");
  // Tile 0 (f=1 FlipX): top-left u > top-right u, top-left v < bottom-left v
  Check(overlayVerts[0].tex_coord.x > overlayVerts[1].tex_coord.x, "f=1 swaps U (FlipX)");
  Check(overlayVerts[0].tex_coord.y < overlayVerts[3].tex_coord.y, "f=1 preserves V");
  // Tile 1 (f=2 FlipY): top-left u < top-right u, top-left v > bottom-left v
  Check(overlayVerts[4].tex_coord.x < overlayVerts[5].tex_coord.x, "f=2 preserves U");
  Check(overlayVerts[4].tex_coord.y > overlayVerts[7].tex_coord.y, "f=2 swaps V (FlipY)");
  // Tile 2 (f=3 FlipX+FlipY): both U and V swapped
  Check(overlayVerts[8].tex_coord.x > overlayVerts[9].tex_coord.x, "f=3 swaps U");
  Check(overlayVerts[8].tex_coord.y > overlayVerts[11].tex_coord.y, "f=3 swaps V");

  // Verify vertical Props_YSort grouping: 3 contiguous vertical tiles at y=16, 32, 48 (height=16)
  // must all share sortLocalY = 48 + 16 = 64.0f!
  CheckEq(tmpl0.ysortTiles.size(), static_cast<size_t>(3), "3 Y-sorted prop tiles extracted");
  for (const auto& yt : tmpl0.ysortTiles) {
    Check(std::abs(yt.sortLocalY - 64.0f) < 0.01f, "contiguous vertical pillar shares bottom sortLocalY=64");
  }

  // Verify IntGrid values (0=Empty, 1=Solid, 2=Chasm, 3=Hazard)
  CheckEq(tmpl0.GetCollisionLocal(0, 0), kTileEmpty, "tile (0,0) is Empty");
  CheckEq(tmpl0.GetCollisionLocal(1, 0), kTileSolid, "tile (1,0) is Solid");
  CheckEq(tmpl0.GetCollisionLocal(2, 0), kTileChasm, "tile (2,0) is Chasm");
  CheckEq(tmpl0.GetCollisionLocal(3, 0), kTileHazard, "tile (3,0) is Hazard");
}

void TestSlidingWindowHysteresisAndStitching() {
  std::cout << "[window] 3x3 load / 5x5 unload hysteresis and deterministic stitching\n";
  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{0, 0, 64, 64}, 1.0f});
  registry.Set<RenderQueue>(RenderQueue(256));

  TilemapSystem sys;
  TilemapLoadOptions opts;
  opts.stitchMode = StitchMode::Toroidal;
  opts.loadRadius = 1;    // 3x3 = 9 chunks
  opts.unloadRadius = 2;  // 5x5 hysteresis ring
  Check(sys.LoadFromJsonString(registry, kTestLdtkJson, opts), "TilemapSystem loaded from JSON");
  CheckEq(sys.GetActiveChunkCount(), static_cast<size_t>(9), "initial 3x3 window loads 9 chunks");

  // Shift camera center to chunk (1, 0): old x=-1 column is at distance 2 <= unloadRadius(2), so it stays resident!
  // New x=+2 column loads (+3 chunks) -> total 12 active chunks (zero boundary thrashing).
  sys.UpdateActiveWindow(registry, 1, 0);
  CheckEq(sys.GetActiveChunkCount(), static_cast<size_t>(12),
          "hysteresis retains x=-1 column at distance 2 (12 chunks)");

  // Shift camera back to (0, 0): both x=-1 and x=+2 are within distance 2 -> still 12 chunks, zero reloads!
  sys.UpdateActiveWindow(registry, 0, 0);
  CheckEq(sys.GetActiveChunkCount(), static_cast<size_t>(12), "moving back across boundary causes zero thrashing");

  // Shift camera to (5, 0): all previous chunks (x <= 2) are at distance >= 3 > unloadRadius(2), leaving fresh 3x3 = 9
  // chunks.
  sys.UpdateActiveWindow(registry, 5, 0);
  CheckEq(sys.GetActiveChunkCount(), static_cast<size_t>(9), "chunks beyond unloadRadius=2 unload cleanly");

  // Test ArenaFixed out-of-bounds solid border
  sys.SetStitchMode(registry, StitchMode::ArenaFixed, 42u);
  Check(!sys.IsSolidAt(4.0f, 4.0f, kTileMaskSolid), "ArenaFixed (0,0) tile (0,0) is Empty");
  Check(sys.IsSolidAt(-8.0f, -8.0f, kTileMaskSolid), "ArenaFixed negative out-of-bounds chunk (-1,-1) is Solid border");
  Check(sys.IsSolidAt(200.0f, 200.0f, kTileMaskSolid), "ArenaFixed positive out-of-bounds chunk is Solid border");

  // Test ProceduralHash determinism
  sys.SetStitchMode(registry, StitchMode::ProceduralHash, 999u);
  const uint64_t h1 = HashChunkCoord(-17, 23, 999u);
  const uint64_t h2 = HashChunkCoord(-17, 23, 999u);
  const uint64_t h3 = HashChunkCoord(-17, 23, 1000u);
  CheckEq(h1, h2, "HashChunkCoord is deterministic for identical (cx, cy, seed)");
  Check(h1 != h3, "HashChunkCoord varies with seed");

  // Verify camera zoom != 1.0f preserves world-space viewport dimensions and initializes all RenderKey::Payload fields.
  auto& cam = registry.Get<CameraComponent>();
  cam.viewport = octarine::Rect{0.0f, 0.0f, 64.0f, 64.0f};
  cam.zoom = 2.0f;
  sys.SetStitchMode(registry, StitchMode::Toroidal, 42u);
  sys.SetShowCollisionOverlay(true);
  auto& rq = registry.Get<RenderQueue>();
  rq.Clear();
  sys.Tick(&registry);
  CheckEq(sys.GetCameraChunkX(), 0, "camera chunk X with zoom=2.0 stays at chunk 0");
  CheckEq(sys.GetCameraChunkY(), 0, "camera chunk Y with zoom=2.0 stays at chunk 0");
  for (const auto& key : rq) {
    if (key.type == SPRITE) {
      Check(std::abs(key.payload.sprite.rotation) < 1e-6f, "YSort sprite rotation initialized to 0");
      CheckEq(static_cast<int>(key.payload.sprite.colorMod.a), 255, "YSort sprite alpha initialized to 255");
    } else if (key.type == SQUARE_PRIMITIVE) {
      Check(std::abs(key.payload.square.rotation) < 1e-6f, "Debug overlay square rotation initialized to 0");
    }
  }
  registry.ValidateInvariants();
}

void TestCollisionsRaycastAndResolution() {
  std::cout << "[collision] Negative coord queries, Chasm vs Solid masks, DDA raycast, Circle/AABB resolution\n";
  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{0, 0, 64, 64}, 1.0f});

  TilemapSystem sys;
  TilemapLoadOptions opts;
  opts.stitchMode = StitchMode::Toroidal;
  sys.LoadFromJsonString(registry, kTestLdtkJson, opts);

  // In Chunk_0_0 (64x64px, gridCols=2, gridRows=1):
  // Tile (0,0) [0..16, 0..16] = 0 (Empty)
  // Tile (1,0) [16..32, 0..16] = 1 (Solid)
  // Tile (2,0) [32..48, 0..16] = 2 (Chasm)
  // Tile (3,0) [48..64, 0..16] = 3 (Hazard)
  // Tile (3,3) [48..64, 48..64] = 1 (Solid)
  // Because gridCols=2,gridRows=1 in Toroidal mode, chunk (-2, -1) wraps to (0, 0)!
  // In chunk (-2, -1), tile (3, 3) is at global tile (-2*4 + 3, -1*4 + 3) = (-5, -1), world [-72, -8).
  CheckEq(sys.GetTileAt(-72.0f, -8.0f), kTileSolid, "negative coord (-72, -8) maps to wrapped tile (3,3) == Solid");
  Check(sys.IsSolidAt(20.0f, 8.0f, kTileMaskGroundBlocked), "Solid tile blocks ground units");
  Check(sys.IsSolidAt(36.0f, 8.0f, kTileMaskGroundBlocked), "Chasm tile blocks ground units");
  Check(!sys.IsSolidAt(36.0f, 8.0f, kTileMaskSolid), "Chasm tile does NOT block projectiles (kTileMaskSolid)");
  Check(sys.IsSolidAt(52.0f, 8.0f, kTileMaskHazard), "Hazard tile detected by kTileMaskHazard");

  // DDA Raycast: cast horizontally from (4, 8) to (60, 8):
  // With kTileMaskSolid, it should hit Tile (1,0) at x = 16.0!
  const auto hitSolid = sys.Raycast(4.0f, 8.0f, 60.0f, 8.0f, kTileMaskSolid);
  Check(hitSolid.hit, "Raycast hits Solid tile (1,0)");
  Check(std::abs(hitSolid.hitX - 16.0f) < 0.05f, "Raycast hits exact left face of tile (1,0) at x=16");
  Check(std::abs(hitSolid.normalX - (-1.0f)) < 0.01f, "Raycast normal is (-1, 0)");

  // Raycast across Chasm only (from (36, 40) to (36, 4) up through Chasm at (2, 1) [y=16..32]):
  // With kTileMaskSolid, Chasm is ignored so the ray reaches (36, 4) unblocked!
  const auto flyOverChasm = sys.Raycast(36.0f, 40.0f, 36.0f, 4.0f, kTileMaskSolid);
  Check(!flyOverChasm.hit, "Projectile raycast with kTileMaskSolid flies over Chasm");
  const auto blockedByChasm = sys.Raycast(36.0f, 40.0f, 36.0f, 4.0f, kTileMaskGroundBlocked);
  Check(blockedByChasm.hit, "Ground raycast with kTileMaskGroundBlocked stops at Chasm edge (y=32)");
  Check(std::abs(blockedByChasm.hitY - 32.0f) < 0.05f, "Chasm hitY == 32.0");

  // Circle resolution against Solid tile (1,0) [16..32, 0..16]:
  // Place circle of radius 6 at (13, 8) (overlapping x=16 by 3px) -> pushed left to x=10!
  const glm::vec2 resolvedCircle = sys.ResolveCircle(13.0f, 8.0f, 6.0f, kTileMaskGroundBlocked);
  Check(std::abs(resolvedCircle.x - 10.0f) < 0.05f, "ResolveCircle pushes overlapping circle out to x=10");

  // AABB resolution against Solid tile (1,0) [16..32, 0..16]:
  // Place 10x10 AABB at (8, 3) (right edge at 18 overlaps 16 by 2px) -> pushed left to x=6!
  const glm::vec2 resolvedAABB = sys.ResolveAABB(8.0f, 3.0f, 10.0f, 10.0f, kTileMaskGroundBlocked);
  Check(std::abs(resolvedAABB.x - 6.0f) < 0.05f, "ResolveAABB pushes overlapping box out to x=6");
}

void TestPropStreamingPoolingAndConsumedBitmask() {
  std::cout << "[props] ECS prop streaming, PoolableTag parking, and consumed-prop bitmask persistence\n";
  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{0, 0, 64, 64}, 1.0f});
  auto& poolMgr = registry.Set<EntityPoolManager>(EntityPoolManager());

  const ArchetypeID propPoolId = poolMgr.RegisterPool(registry, [](Registry& reg) {
    const Entity e = reg.CreateEntity();
    reg.AddTag<PoolableTag>(e);
    return e;
  });

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table);
  lua["spawn_pooled_prop"] = [&registry, &poolMgr, &lua, propPoolId](const std::string& id, const float /*wx*/,
                                                                     const float /*wy*/, const sol::table& /*fields*/,
                                                                     const int32_t /*cx*/, const int32_t /*cy*/,
                                                                     const uint32_t /*pidx*/) -> sol::object {
    if (id == "Spawner") {
      sol::table pack = lua.create_table();
      pack[1] = poolMgr.Spawn(registry, propPoolId);
      pack[2] = poolMgr.Spawn(registry, propPoolId);
      return pack;
    }
    return sol::make_object(lua, poolMgr.Spawn(registry, propPoolId));
  };

  TilemapSystem sys;
  sys.SetPropSpawner(lua["spawn_pooled_prop"]);

  TilemapLoadOptions opts;
  opts.stitchMode = StitchMode::ArenaFixed;
  opts.loadRadius = 0;    // Only load center chunk (0,0)
  opts.unloadRadius = 0;  // Unload immediately when leaving (0,0)
  sys.LoadFromJsonString(registry, kTestLdtkJson, opts);
  registry.ValidateInvariants();

  // Chunk (0,0) has 2 entities in `Entities`: Brazier (propIndex=0 -> 1 entity) and Spawner (propIndex=1 -> table of 2
  // entities).
  int activeProps = 0;
  auto q = registry.CreateQuery<ChunkResidentComponent>();
  q->Update();
  q->ForEach([&](Entity /*e*/, const ChunkResidentComponent& res) {
    CheckEq(res.chunkX, 0, "spawned prop has chunkX == 0");
    CheckEq(res.chunkY, 0, "spawned prop has chunkY == 0");
    ++activeProps;
  });
  CheckEq(activeProps, 3, "3 props spawned (1 single Entity + 2 from table) when chunk (0,0) enters active window");

  // Mark propIndex=0 (Brazier) as consumed (destroyed/looted).
  sys.MarkPropConsumed(0, 0, 0u);
  Check(sys.IsPropConsumed(0, 0, 0u), "propIndex 0 marked consumed");
  Check(!sys.IsPropConsumed(0, 0, 1u), "propIndex 1 still unconsumed");

  // Also test high prop indices (> 64 and up to 255) supported by ConsumedPropMask
  sys.MarkPropConsumed(0, 0, 65u);
  sys.MarkPropConsumed(0, 0, 199u);
  Check(sys.IsPropConsumed(0, 0, 65u), "propIndex 65 marked consumed (words[1])");
  Check(!sys.IsPropConsumed(0, 0, 64u), "propIndex 64 still unconsumed");
  Check(sys.IsPropConsumed(0, 0, 199u), "propIndex 199 marked consumed (words[3])");
  Check(!sys.IsPropConsumed(0, 0, 200u), "propIndex 200 still unconsumed");

  // Move camera to chunk (5, 5) (out-of-bounds border chunk with 0 props) -> chunk (0,0) unloads and parks its 3 props!
  sys.UpdateActiveWindow(registry, 5, 5);
  registry.ValidateInvariants();
  int propsAtBorder = 0;
  q->Update();
  q->ForEach([&](Entity /*e*/, const ChunkResidentComponent& /*res*/) { ++propsAtBorder; });
  CheckEq(propsAtBorder, 0, "props despawned/parked when chunk (0,0) leaves outer window");

  // Move camera back to chunk (0, 0): only unconsumed propIndex=1 (2 entities in table) should respawn!
  sys.UpdateActiveWindow(registry, 0, 0);
  registry.ValidateInvariants();
  int respawnedProps = 0;
  uint32_t survivingPropIdx = 999u;
  q->Update();
  q->ForEach([&](Entity /*e*/, const ChunkResidentComponent& res) {
    ++respawnedProps;
    survivingPropIdx = res.propIndex;
  });
  CheckEq(respawnedProps, 2, "only unconsumed propIndex=1 (2 entities) respawns when revisiting chunk (0,0)");
  CheckEq(survivingPropIdx, 1u, "surviving prop is propIndex=1 (Spawner); consumed Brazier stayed gone");
}

void TestHotReloadFromDisk() {
  std::cout << "[hot-reload] FileWatcher detects .ldtk modification and rebuilds templates in-place\n";
  const auto tempDir = std::filesystem::temp_directory_path() / "octarine-ldtk-hotreload-test";
  std::error_code ec;
  std::filesystem::create_directories(tempDir, ec);
  const std::string ldtkPath = (tempDir / "test_map.ldtk").string();

  {
    std::ofstream out(ldtkPath, std::ios::trunc);
    out << kTestLdtkJson;
  }

  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{0, 0, 64, 64}, 1.0f});

  TilemapSystem sys;
  Check(sys.Load(registry, ldtkPath), "loaded .ldtk file from disk");
  CheckEq(sys.GetTileAt(4.0f, 4.0f), kTileEmpty, "initially tile (0,0) is Empty (0)");

  // Modify tile (0,0) from 0 (Empty) to 1 (Solid) and deterministically advance filesystem timestamp.
  std::string modifiedJson = kTestLdtkJson;
  const auto pos = modifiedJson.find("0, 1, 2, 3,");
  Check(pos != std::string::npos, "located intGridCsv row in JSON");
  if (pos != std::string::npos) {
    modifiedJson.replace(pos, 11, "1, 1, 2, 3,");
  }
  {
    std::ofstream out(ldtkPath, std::ios::trunc);
    out << modifiedJson;
  }
  const auto futureTime = std::filesystem::last_write_time(ldtkPath, ec) + std::chrono::seconds(2);
  std::filesystem::last_write_time(ldtkPath, futureTime, ec);

  Check(sys.PollHotReload(registry, 0.5f, 0.1f), "PollHotReload detected modified .ldtk file");
  CheckEq(sys.GetHotReloadCount(), static_cast<uint64_t>(1), "hotReloadCount incremented to 1");
  CheckEq(sys.GetTileAt(4.0f, 4.0f), kTileSolid, "hot-reloaded template updated tile (0,0) to Solid (1)");

  std::filesystem::remove_all(tempDir, ec);
}

void TestDirectLoadFromDataAndBuilder() {
  std::cout << "[decoupled-ir] TilemapBuilder + TilemapSystem::LoadFromData with zero LDtk involvement\n";
  TilemapData data;
  data.sourceFormat = "procedural_cpp";
  data.tileSize = 16;
  data.chunkGridWidth = 4;
  data.chunkGridHeight = 4;

  TilesetDesc ts;
  ts.assetId = "dungeon_atlas";
  ts.relPath = "textures/dungeon.png";
  ts.textureWidth = 256.0f;
  ts.textureHeight = 256.0f;
  ts.tileGridSize = 16;
  data.tilesetAssets.emplace_back(ts.assetId, ts.relPath);

  ChunkTemplate tmpl;
  tmpl.identifier = "ProcChunk_0_0";
  tmpl.gridX = 0;
  tmpl.gridY = 0;
  tmpl.tileSize = 16;
  tmpl.gridWidth = 4;
  tmpl.gridHeight = 4;
  tmpl.collisions[0] = kTileEmpty;
  tmpl.collisions[1] = kTileSolid;

  TileLayerMesh groundMesh;
  groundMesh.layerName = "Floor";
  groundMesh.tilesetAssetId = ts.assetId;
  groundMesh.tilesetRelPath = ts.relPath;
  groundMesh.renderLayer = 0u;
  TilemapBuilder::ReserveLayerMesh(groundMesh, 1u);
  TilemapBuilder::AppendStaticTileQuad(groundMesh, ts,
                                       TileQuadDesc{0.0f, 0.0f, 32.0f, 16.0f, 16.0f, 1.0f, true, false});
  tmpl.staticLayers.push_back(std::move(groundMesh));

  // Two vertically stacked YSort tiles at x=16, y=16 (top) and y=32 (bottom).
  TilemapBuilder::AppendYSortTile(tmpl.ysortTiles, ts, TileQuadDesc{16.0f, 16.0f, 0.0f, 0.0f, 16.0f});
  TilemapBuilder::AppendYSortTile(tmpl.ysortTiles, ts, TileQuadDesc{16.0f, 32.0f, 0.0f, 16.0f, 16.0f});
  TilemapBuilder::GroupVerticalYSortTiles(tmpl.ysortTiles);
  Check(std::abs(tmpl.ysortTiles[0].sortLocalY - 48.0f) < 1e-4f, "upper YSort tile inherited bottom tile sortLocalY");

  data.templates.push_back(std::move(tmpl));
  TilemapBuilder::FinalizeTilemapData(data);

  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{0, 0, 64, 64}, 1.0f});

  TilemapSystem sys;
  TilemapLoadOptions opts;
  opts.stitchMode = StitchMode::ArenaFixed;
  Check(sys.LoadFromData(registry, std::move(data), opts), "LoadFromData succeeded");
  CheckEq(sys.GetTilemapData().sourceFormat, std::string{"procedural_cpp"}, "sourceFormat preserved");
  Check(!sys.Reload(registry), "Reload on <data> tilemap safely returns false");
  Check(!sys.IsSolidAt(8.0f, 8.0f), "cell (0,0) is empty");
  Check(sys.IsSolidAt(24.0f, 8.0f), "cell (1,0) is solid");
  registry.ValidateInvariants();
}

void TestCustomFormatLoaderRegistration() {
  std::cout << "[format-registry] Custom format registration (.custommap) + hot-reload dispatch\n";
  int customLoadCalls = 0;
  uint8_t dynamicFirstCell = kTileEmpty;

  TilemapFormatLoader customLoader;
  customLoader.formatId = "custom_binary";
  customLoader.extensions = {".custommap"};
  customLoader.loadFromFile = [&](const std::string& path,
                                  const AssetManager* /*assets*/) -> std::optional<TilemapData> {
    std::ifstream in(path);
    if (!in.good()) {
      return std::nullopt;
    }
    ++customLoadCalls;
    TilemapData d;
    d.sourceFormat = "custom_binary";
    d.tileSize = 16;
    d.chunkGridWidth = 4;
    d.chunkGridHeight = 4;
    ChunkTemplate t;
    t.identifier = "Custom_0_0";
    t.gridWidth = 4;
    t.gridHeight = 4;
    t.collisions[0] = dynamicFirstCell;
    d.templates.push_back(std::move(t));
    TilemapBuilder::FinalizeTilemapData(d);
    return d;
  };
  TilemapLoaderRegistry::RegisterFormat(std::move(customLoader));
  Check(TilemapLoaderRegistry::HasFormat("custom_binary"), "HasFormat('custom_binary') is true");
  Check(TilemapLoaderRegistry::HasFormat(".custommap"), "HasFormat('.custommap') is true");

  const auto tempDir = std::filesystem::temp_directory_path() / "octarine-custommap-test";
  std::error_code ec;
  std::filesystem::create_directories(tempDir, ec);
  const std::string mapPath = (tempDir / "level.custommap").string();
  {
    std::ofstream out(mapPath, std::ios::trunc);
    out << "CUSTOM_V1";
  }

  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{0, 0, 64, 64}, 1.0f});

  TilemapSystem sys;
  Check(sys.Load(registry, mapPath), "TilemapSystem::Load auto-detected .custommap format");
  CheckEq(customLoadCalls, 1, "custom loader invoked once on initial Load");
  CheckEq(sys.GetTilemapData().sourceFormat, std::string{"custom_binary"}, "sourceFormat is custom_binary");
  CheckEq(sys.GetTileAt(4.0f, 4.0f), kTileEmpty, "initial custommap cell (0,0) is Empty");

  dynamicFirstCell = kTileHazard;
  Check(sys.Reload(registry), "TilemapSystem::Reload re-invoked custom loader");
  CheckEq(customLoadCalls, 2, "custom loader invoked on Reload");
  CheckEq(sys.GetTileAt(4.0f, 4.0f), kTileHazard, "reloaded custommap cell (0,0) updated to Hazard");

  std::filesystem::remove_all(tempDir, ec);
  registry.ValidateInvariants();
}

class TestLuaBindingContext final : public LuaBindingContext {
 public:
  explicit TestLuaBindingContext(Registry* reg) : registry_(reg) {}
  [[nodiscard]] Registry* GetRegistry() const override { return registry_; }
  [[nodiscard]] SDL_Renderer* GetRenderer() const override { return nullptr; }
  [[nodiscard]] EngineContext& GetContext() override { return ctx_; }
  void LoadScene(const std::string& /*scenePath*/) override {}
  void ReloadScene() override {}
  void StopScene() override {}
  void TrackSceneAssets(const std::vector<std::string>& /*assetIds*/) override {}
  [[nodiscard]] bool IsBakeMode() const override { return false; }
  void RecordBakeValidationFailures(int /*failures*/) override {}
  [[nodiscard]] std::string GetProjectPath() const override { return {}; }
  std::string GetSaveDataPath() override { return {}; }
  void RequestQuit() override {}

 private:
  Registry* registry_ = nullptr;
  EngineContext ctx_{};
};

void TestLuaLoadDataProcedural() {
  std::cout << "[lua-data] tilemap.load_data builds procedural chunks, collisions, and props from Lua table\n";
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::math);

  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{0, 0, 64, 64}, 1.0f});

  TestLuaBindingContext bindCtx(&registry);
  LuaModuleBinding<TilemapModule>::install(lua, bindCtx);

  lua["spawned_prop_hp"] = 0;
  lua.set_function("make_test_prop", [&](const int hp) -> Entity {
    const Entity e = registry.CreateEntity();
    lua["spawned_prop_hp"] = hp;
    return e;
  });

  const auto res = lua.safe_script(R"LUA(
    tilemap.set_prop_spawner(function(id, wx, wy, fields, cx, cy, pidx)
      if id == "Chest" then
        return make_test_prop(fields.hp)
      end
      return nil
    end)

    local ok = tilemap.load_data({
      tile_size = 16,
      chunk_grid_width = 4,
      chunk_grid_height = 4,
      tilesets = {
        { id = "crypt_ts", file = "textures/crypt.png", width = 256, height = 256, tile_size = 16 }
      },
      templates = {
        {
          id = "LuaRoom_0_0",
          grid_x = 0,
          grid_y = 0,
          biome = "crypt",
          edge_north = "Open",
          edge_east = "Open",
          edge_south = "Open",
          edge_west = "Open",
          fields = { difficulty = 3, tag = "entry" },
          collisions = {
            0, 1, 2, 3,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0
          },
          static_layers = {
            {
              name = "Ground",
              tileset = "crypt_ts",
              render_layer = 0,
              render_depth = 0.0,
              tiles = {
                { gx = 0, gy = 0, src_x = 0, src_y = 0, flip_x = true }
              }
            },
            {
              name = "Decals",
              tileset = "crypt_ts",
              render_layer = 0,
              render_depth = 1.0,
              tiles = {
                { gx = 1, gy = 0, src_x = 16, src_y = 0 }
              }
            }
          },
          ysort_tiles = {
            { tileset = "crypt_ts", x = 16, y = 16, src_x = 16, src_y = 0 }
          },
          props = {
            { id = "Chest", x = 32, y = 32, fields = { hp = 77 } }
          }
        },
        {
          id = "LuaCorridor_NS",
          grid_x = 1,
          grid_y = 0,
          biome = "crypt",
          edge_north = "Path",
          edge_east = "Open",
          edge_south = "Path",
          edge_west = "Open",
          collisions = {
            1, 0, 0, 1,
            1, 0, 0, 1,
            1, 0, 0, 1,
            1, 0, 0, 1
          }
        }
      }
    }, { stitch_mode = "procedural_hash", seed = 42, load_radius = 0, unload_radius = 1, tile_scale = 2.0 })
    assert(ok, "tilemap.load_data returned false")
    assert(tilemap.is_loaded(), "tilemap.is_loaded() should be true")
    local cpw, cph, cgw, cgh, tsize = tilemap.get_chunk_size()
    assert(tsize == 32 and cpw == 128 and cph == 128 and cgw == 4 and cgh == 4, "tile_scale=2.0 should double tile/chunk pixel size")
    assert(not tilemap.is_solid_at(16, 16), "scaled cell (0,0) should be empty")
    assert(tilemap.is_solid_at(48, 16), "scaled cell (1,0) should be solid")
    local hit, hx, hy, nx, ny, dist, val = tilemap.raycast(8, 16, 120, 16)
    assert(hit and val == tilemap.TILE_SOLID, "raycast should hit solid cell (1,0)")

    local tmpls = tilemap.get_templates()
    assert(#tmpls == 2, "get_templates should return 2 templates")
    assert(tmpls[1].id == "LuaRoom_0_0" and tmpls[1].biome == "crypt", "template metadata preserved")
    assert(tmpls[1].fields.difficulty == 3 and tmpls[1].fields.tag == "entry", "template fields preserved")

    -- Procedural hash should filter out constrained socket templates (LuaCorridor_NS) in favor of unconstrained (LuaRoom_0_0)
    assert(tilemap.get_chunk_template_id(0, 0) == "LuaRoom_0_0", "procedural hash selects unconstrained template")
    assert(tilemap.set_chunk_override(0, 0, "LuaCorridor_NS"), "set_chunk_override should succeed for known template")
    assert(tilemap.get_chunk_template_id(0, 0) == "LuaCorridor_NS", "override should take effect on resident chunk")
    assert(tilemap.is_solid_at(16, 16), "LuaCorridor_NS cell (0,0) is solid")
    assert(not tilemap.is_solid_at(48, 16), "LuaCorridor_NS cell (1,0) is empty")
    tilemap.clear_chunk_overrides()
    assert(tilemap.get_chunk_template_id(0, 0) == "LuaRoom_0_0", "clear_chunk_overrides restores procedural template")
  )LUA",
                                   sol::script_pass_on_error);
  Check(res.valid(), "Lua tilemap.load_data script executed cleanly");
  CheckEq(lua["spawned_prop_hp"].get<int>(), 77, "prop spawner received Chest prop with fields.hp == 77");
  registry.ValidateInvariants();
}

void TestPaletteAndProceduralVariants() {
  std::cout << "[palette-variants] Palette extraction, SetVariantGenerator hook, SetStitchMode seed regeneration, "
               "and spawn_chance\n";
  TilemapData data;
  data.sourceFormat = "procedural_cpp";
  data.tileSize = 16;
  data.chunkGridWidth = 8;
  data.chunkGridHeight = 8;

  TilesetDesc ts;
  ts.assetId = "dungeon_atlas";
  ts.relPath = "textures/dungeon.png";
  ts.textureWidth = 256.0f;
  ts.textureHeight = 256.0f;
  ts.tileGridSize = 16;
  data.tilesetAssets.emplace_back(ts.assetId, ts.relPath);

  // Palette template at negative coordinates (-2, -2): must not skew playable gridCols/gridRows.
  ChunkTemplate palette;
  palette.identifier = "Chunk_Palette";
  palette.gridX = -2;
  palette.gridY = -2;
  palette.tileSize = 16;
  palette.gridWidth = 8;
  palette.gridHeight = 8;
  palette.collisions[0] = kTileSolid;
  TilemapBuilder::AppendYSortTile(palette.ysortTiles, ts, TileQuadDesc{0.0f, 0.0f, 16.0f, 16.0f, 16.0f});
  data.templates.push_back(std::move(palette));

  // Playable chunk at (0, 0) with custom open socket prefix ("OpenDesert") and a 0% spawn_chance prop.
  ChunkTemplate playable;
  playable.identifier = "Room_0_0";
  playable.gridX = 0;
  playable.gridY = 0;
  playable.tileSize = 16;
  playable.gridWidth = 8;
  playable.gridHeight = 8;
  playable.edgeNorth = "OpenDesert";
  playable.edgeEast = "open_sand";
  playable.edgeSouth = "none";
  playable.edgeWest = "*";
  Check(playable.IsUnconstrainedSocketTemplate(), "generic open/none/* socket prefixes are unconstrained");

  octarine::tilemap::PropSpawnDesc zeroChanceProp;
  zeroChanceProp.identifier = "NeverSpawns";
  zeroChanceProp.localX = 0.0f;
  zeroChanceProp.localY = 0.0f;
  zeroChanceProp.fields["spawn_chance"] = 0.0;
  playable.propSpawns.push_back(zeroChanceProp);
  data.templates.push_back(std::move(playable));

  TilemapBuilder::FinalizeTilemapData(data);
  CheckEq(data.templates.size(), static_cast<size_t>(1), "Chunk_Palette stripped from playable templates");
  CheckEq(data.paletteTemplates.size(), static_cast<size_t>(1), "Chunk_Palette stored in paletteTemplates");
  CheckEq(data.gridCols, 1, "Chunk_Palette negative coords did not inflate gridCols");
  CheckEq(data.gridRows, 1, "Chunk_Palette negative coords did not inflate gridRows");

  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{0, 0, 64, 64}, 1.0f});

  int spawnedNeverProps = 0;
  sol::state lua;
  lua.open_libraries(sol::lib::base);
  lua.set_function("on_prop", [&](const std::string& id) -> sol::object {
    if (id == "NeverSpawns") ++spawnedNeverProps;
    return sol::lua_nil;
  });

  uint32_t lastGeneratorSeed = 0u;
  TilemapSystem sys;
  sys.SetPropSpawner(lua["on_prop"]);
  sys.SetVariantGenerator([&](TilemapData& d, const int32_t variantCount, const uint32_t seed) {
    lastGeneratorSeed = seed;
    d.templateVariants.clear();
    if (variantCount <= 1 || d.templates.empty() || d.paletteTemplates.empty()) return;
    auto& vars = d.templateVariants[d.templates[0].identifier];
    for (int32_t v = 0; v < variantCount; ++v) {
      ChunkTemplate var = d.templates[0];
      var.ysortTiles = d.paletteTemplates[0].ysortTiles;
      vars.push_back(std::move(var));
    }
  });

  TilemapLoadOptions opts;
  opts.stitchMode = StitchMode::ProceduralHash;
  opts.seed = 100u;
  opts.variantCount = 3;
  opts.loadRadius = 0;
  opts.unloadRadius = 1;
  Check(sys.LoadFromData(registry, std::move(data), opts), "LoadFromData with palette + variants succeeded");
  CheckEq(lastGeneratorSeed, 100u, "VariantGenerator invoked with initial seed 100");

  const auto& loadedData = sys.GetTilemapData();
  const auto varIt = loadedData.templateVariants.find("Room_0_0");
  Check(varIt != loadedData.templateVariants.end(), "templateVariants populated for Room_0_0");
  if (varIt != loadedData.templateVariants.end()) {
    CheckEq(varIt->second.size(), static_cast<size_t>(3), "3 procedural variants generated for Room_0_0");
    Check(!varIt->second[0].ysortTiles.empty(), "VariantGenerator copied palette YSort tile into variant");
  }
  CheckEq(spawnedNeverProps, 0, "prop with spawn_chance=0.0 was never spawned");

  // Changing seed via SetStitchMode regenerates variants cleanly.
  sys.SetStitchMode(registry, StitchMode::ProceduralHash, 999u);
  CheckEq(sys.GetSeed(), 999u, "SetStitchMode updated seed to 999");
  CheckEq(lastGeneratorSeed, 999u, "VariantGenerator re-invoked when SetStitchMode changed seed");
  Check(sys.GetTilemapData().templateVariants.contains("Room_0_0"), "templateVariants regenerated after seed change");
  registry.ValidateInvariants();
}

void TestMultiChunkLevelSlicing() {
  std::cout << "[parser] Multi-chunk LDtk level auto-slicing (3x2 sub-chunks, collisions, props, YSort)\n";
  // Base chunk is 4x4 tiles (64x64 px), multi-chunk structure is 12x8 tiles (192x128 px => 3x2 chunks).
  const std::string multiChunkJson = R"JSON({
    "defaultGridSize": 16,
    "defs": {
      "tilesets": [
        { "uid": 1, "identifier": "test_tiles", "relPath": "tilesets/test.png", "pxWid": 64, "pxHei": 64, "tileGridSize": 16 }
      ]
    },
    "levels": [
      {
        "identifier": "Base_Wilderness",
        "worldX": 0, "worldY": 0, "pxWid": 64, "pxHei": 64,
        "fieldInstances": [],
        "layerInstances": [
          { "__identifier": "Collisions", "__type": "IntGrid", "__cWid": 4, "__cHei": 4, "__gridSize": 16, "intGridCsv": [0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0] }
        ]
      },
      {
        "identifier": "Chunk_Vault",
        "worldX": 128, "worldY": 0, "pxWid": 192, "pxHei": 128,
        "fieldInstances": [
          { "__identifier": "EdgeSouth", "__value": "Road" }
        ],
        "layerInstances": [
          {
            "__identifier": "Entities", "__type": "Entities", "__cWid": 12, "__cHei": 8, "__gridSize": 16,
            "entityInstances": [
              { "__identifier": "ScatterZone", "px": [16, 16], "width": 160, "height": 96, "__pivot": [0.0, 0.0], "fieldInstances": [] },
              { "__identifier": "VaultDoor", "px": [96, 112], "width": 16, "height": 16, "__pivot": [0.5, 0.5], "fieldInstances": [] }
            ]
          },
          {
            "__identifier": "Collisions", "__type": "IntGrid", "__cWid": 12, "__cHei": 8, "__gridSize": 16,
            "intGrid": [
              { "coordId": 0, "v": 1 },
              { "coordId": 90, "v": 4 }
            ]
          },
          {
            "__identifier": "Ground_Base", "__type": "Tiles", "__cWid": 12, "__cHei": 8, "__gridSize": 16, "__tilesetDefUid": 1,
            "gridTiles": [
              { "px": [0, 0], "src": [0, 0], "f": 0, "t": 0, "a": 1.0 },
              { "px": [80, 80], "src": [16, 0], "f": 0, "t": 1, "a": 1.0 }
            ]
          }
        ]
      }
    ]
  })JSON";

  auto parsedOpt = LdtkParser::ParseFromJsonString(multiChunkJson);
  Check(parsedOpt.has_value(), "Multi-chunk LDtk JSON parsed");
  if (!parsedOpt.has_value()) return;

  const auto& data = *parsedOpt;
  CheckEq(data.chunkGridWidth, 4, "Base chunkGridWidth preserved as 4");
  CheckEq(data.chunkGridHeight, 4, "Base chunkGridHeight preserved as 4");
  // 1 base chunk + (3 * 2 = 6) sliced sub-chunks = 7 templates
  CheckEq(data.templates.size(), static_cast<size_t>(7), "3x2 multi-chunk level sliced into 6 sub-chunk templates");

  const ChunkTemplate* sub00 = nullptr;
  const ChunkTemplate* sub11 = nullptr;
  for (const auto& t : data.templates) {
    if (t.identifier == "Chunk_Vault__0_0") sub00 = &t;
    if (t.identifier == "Chunk_Vault__1_1") sub11 = &t;
  }
  Check(sub00 != nullptr && sub11 != nullptr, "Sub-chunks Chunk_Vault__0_0 and Chunk_Vault__1_1 exist");
  if (sub00 != nullptr && sub11 != nullptr) {
    Check(std::abs(sub00->weight) < 1e-6f, "Sliced sub-chunk weight is 0.0 so random stitcher skips it");
    CheckEq(std::get<std::string>(sub00->fields.at("structure_id")), std::string{"Chunk_Vault"}, "structure_id set");
    CheckEq(std::get<int64_t>(sub00->fields.at("span_w")), int64_t{3}, "span_w is 3");
    CheckEq(std::get<int64_t>(sub00->fields.at("span_h")), int64_t{2}, "span_h is 2");
    CheckEq(sub00->GetCollisionLocal(0, 0), kTileSolid, "sub00 (0,0) inherited solid cell");
    // coordId 90 in 12x8 grid => gy = 90 / 12 = 7, gx = 90 % 12 = 6 => sub (1, 1) local (2, 3)
    CheckEq(sub11->GetCollisionLocal(2, 3), octarine::tilemap::kTileSurface,
            "sub11 (2,3) inherited road cell from coordId 90");
    CheckEq(sub11->edgeSouth, std::string{"Road"}, "Bottom-center sub-chunk (1,1) inherited EdgeSouth=Road");
    CheckEq(sub00->edgeNorth, std::string{}, "Unset outer edge stays empty rather than game-specific string");
    CheckEq(sub00->edgeEast, std::string{"__Internal"}, "Shared internal seam tagged __Internal");
    CheckEq(sub11->propSpawns.size(), static_cast<size_t>(2), "sub11 received clipped ScatterZone + VaultDoor point");
  }
}

void TestDataDrivenCollisionLayersAndOptions() {
  std::cout << "[data-driven] Data-driven collision layers, custom channels, default masks, and palette flags\n";

  const std::string multiLayerJson = R"JSON({
    "defaultGridSize": 16,
    "defs": { "tilesets": [] },
    "levels": [
      {
        "identifier": "Level_Arena", "worldX": 0, "worldY": 0, "pxWid": 64, "pxHei": 64,
        "fieldInstances": [],
        "layerInstances": [
          {
            "__identifier": "GroundCollisions", "__type": "IntGrid", "__cWid": 4, "__cHei": 4, "__gridSize": 16,
            "intGridCsv": [
              1, 0, 0, 0,
              0, 0, 0, 0,
              0, 0, 0, 0,
              0, 0, 0, 0
            ]
          },
          {
            "__identifier": "WallCollisions", "__type": "IntGrid", "__cWid": 4, "__cHei": 4, "__gridSize": 16,
            "intGridCsv": [
              0, 1, 0, 0,
              0, 0, 0, 0,
              0, 0, 0, 0,
              0, 0, 0, 0
            ]
          },
          {
            "__identifier": "HazardCollisions", "__type": "IntGrid", "__cWid": 4, "__cHei": 4, "__gridSize": 16,
            "intGridCsv": [
              0, 0, 1, 0,
              0, 0, 0, 0,
              0, 0, 0, 0,
              0, 0, 0, 0
            ]
          }
        ]
      },
      {
        "identifier": "Custom_Palette_Room", "worldX": 100, "worldY": 0, "pxWid": 64, "pxHei": 64,
        "fieldInstances": [
          { "__identifier": "is_palette", "__value": true }
        ],
        "layerInstances": []
      }
    ]
  })JSON";

  TilemapParseOptions parseOpts;
  parseOpts.collisionLayerRules = {
      {"ground", 0},
      {"wall", octarine::tilemap::kTileWall},
      {"hazard", octarine::tilemap::kTileHazard},
  };

  auto dataOpt = LdtkParser::ParseFromJsonString(multiLayerJson, "", parseOpts);
  Check(dataOpt.has_value(), "Parsed multi-layer JSON with custom collisionLayerRules");
  if (!dataOpt.has_value()) return;

  const auto& data = *dataOpt;
  CheckEq(data.paletteTemplates.size(), static_cast<size_t>(1), "Custom_Palette_Room extracted as palette template");
  if (!data.paletteTemplates.empty()) {
    CheckEq(data.paletteTemplates[0].identifier, std::string{"Custom_Palette_Room"},
            "Extracted palette template identifier matches");
    Check(data.paletteTemplates[0].isPalette, "Template isPalette flag is true");
  }

  CheckEq(data.templates.size(), static_cast<size_t>(1), "1 playable level template");
  if (!data.templates.empty()) {
    const auto& tmpl = data.templates[0];
    CheckEq(tmpl.GetCollisionLocal(0, 0), octarine::tilemap::kTileSolid, "Cell (0,0) is kTileSolid");
    CheckEq(tmpl.GetCollisionLocal(1, 0), octarine::tilemap::kTileWall, "Cell (1,0) is kTileWall");
    CheckEq(tmpl.GetCollisionLocal(2, 0), octarine::tilemap::kTileHazard, "Cell (2,0) is kTileHazard");
    CheckEq(tmpl.GetCollisionLocal(3, 0), octarine::tilemap::kTileEmpty, "Cell (3,0) is kTileEmpty");
  }

  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<CameraComponent>(CameraComponent{octarine::Rect{0, 0, 64, 64}, 1.0f});

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::math);
  TestLuaBindingContext bindCtx(&registry);
  LuaModuleBinding<TilemapModule>::install(lua, bindCtx);

  CheckEq(lua["tilemap"]["TILE_WALL"].get<int>(), 5, "Lua TILE_WALL is 5");
  CheckEq(lua["tilemap"]["TILE_SURFACE"].get<int>(), 4, "Lua TILE_SURFACE is 4");
  CheckEq(lua["tilemap"]["TILE_1"].get<int>(), 1, "Lua TILE_1 is 1");
  CheckEq(lua["tilemap"]["TILE_7"].get<int>(), 7, "Lua TILE_7 is 7");
  CheckEq(lua["tilemap"]["MASK_WALL"].get<int>(), 1 << 5, "Lua MASK_WALL is 32");

  lua["multi_json"] = multiLayerJson;
  auto loadRes = lua.safe_script(R"LUA(
    return tilemap.load_json(multi_json, {
      collision_layers = {
        { match = "ground", value = tilemap.TILE_SOLID },
        { match = "wall", value = tilemap.TILE_WALL },
        { match = "hazard", value = tilemap.TILE_HAZARD },
      },
      solid_mask = tilemap.MASK_SOLID,
      raycast_mask = tilemap.MASK_WALL,
    })
  )LUA");
  Check(loadRes.valid() && loadRes.get<bool>(),
        "tilemap.load_json succeeded with Lua collision_layers & default masks");

  auto* sys = registry.TryGet<TilemapSystem>();
  Check(sys != nullptr, "TilemapSystem singleton exists in Registry");
  if (sys != nullptr) {
    CheckEq(sys->GetDefaultSolidMask(), static_cast<uint8_t>(octarine::tilemap::kTileMaskSolid),
            "sys default solid mask is MASK_SOLID");
    CheckEq(sys->GetDefaultRaycastMask(), static_cast<uint8_t>(octarine::tilemap::kTileMaskWall),
            "sys default raycast mask is MASK_WALL");

    Check(sys->IsSolidAt(8.0f, 8.0f), "Tile (0,0) is solid under default mask MASK_SOLID");
    Check(!sys->IsSolidAt(24.0f, 8.0f), "Tile (1,0) Wall is NOT solid under default mask MASK_SOLID");
    Check(sys->IsSolidAt(24.0f, 8.0f, octarine::tilemap::kTileMaskWall),
          "Tile (1,0) is solid when explicitly querying MASK_WALL");

    const auto hit = sys->Raycast(0.0f, 8.0f, 60.0f, 8.0f);
    Check(hit.hit, "Raycast hits wall under default raycast mask MASK_WALL");
    CheckEq(static_cast<int>(hit.tileValue), static_cast<int>(octarine::tilemap::kTileWall),
            "Raycast hit tileValue is kTileWall");

    auto maskScriptRes = lua.safe_script(R"LUA(
      tilemap.set_default_masks(tilemap.MASK_WALL, tilemap.MASK_SOLID)
      local s, r = tilemap.get_default_masks()
      return s, r
    )LUA");
    Check(maskScriptRes.valid(), "tilemap.set_default_masks / get_default_masks ran in Lua");
    CheckEq(maskScriptRes.get<int>(0), static_cast<int>(octarine::tilemap::kTileMaskWall),
            "Lua reported updated solid mask");
    CheckEq(maskScriptRes.get<int>(1), static_cast<int>(octarine::tilemap::kTileMaskSolid),
            "Lua reported updated raycast mask");
    Check(sys->IsSolidAt(24.0f, 8.0f), "Tile (1,0) Wall is now solid under updated default solid mask MASK_WALL");
  }
}

}  // namespace

int main() {
  Logger::Init();

  TestNegativeCoordinateMath();
  TestLdtkParserAndFlipBits();
  TestSlidingWindowHysteresisAndStitching();
  TestCollisionsRaycastAndResolution();
  TestPropStreamingPoolingAndConsumedBitmask();
  TestHotReloadFromDisk();
  TestDirectLoadFromDataAndBuilder();
  TestCustomFormatLoaderRegistration();
  TestLuaLoadDataProcedural();
  TestPaletteAndProceduralVariants();
  TestMultiChunkLevelSlicing();
  TestDataDrivenCollisionLayersAndOptions();

  return octarine::test::ReportSummary("TilemapSystemTest");
}
