# LDtk Chunked Tilemap System

Octarine features a high-performance, infinite 2D chunked tilemap subsystem powered by `TilemapSystem` and `TilemapRenderSystem`. It loads levels authored in [LDtk](https://ldtk.io/) (or custom JSON/table IRs), pre-batches static layer geometry into cache-friendly `TILE_CHUNK` render queue commands, streams dynamic entity props with hysteresis windowing, supports edge-socket procedural chunk stitching, and exposes sub-millisecond collision queries and 2D DDA raycasting directly to Lua.

---

## 1. Architecture & Key Concepts

```
┌─────────────────────────────────────────────────────────────┐
│                    LDtk Project (.ldtk)                     │
│  - Tile Layers (Ground, Walls, Details)                     │
│  - IntGrid Layers (Collision, Hazards, Chasms)              │
│  - Entity / Prop Instances (Chests, Enemies, Spawners)      │
└──────────────────────────────┬──────────────────────────────┘
                               │ tilemap.load()
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                      TilemapSystem                          │
│  ┌───────────────────────────┬───────────────────────────┐  │
│  │     Static Geometry       │     Dynamic Streaming     │  │
│  │ - Pre-batched TILE_CHUNK  │ - Hysteresis Windowing    │  │
│  │ - Multi-layer depth bands │ - Prop ECS Spawning       │  │
│  │ - TilemapRenderSystem     │ - Consumed Persistence    │  │
│  └───────────────────────────┴───────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                Spatial Queries & Physics              │  │
│  │ - IntGrid O(1) Bitmask    - 2D DDA Raycasting         │  │
│  │ - Circle & AABB Resolvers - Edge-Socket Stitcher      │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Static Layer Geometry Batching
Rather than instantiating thousands of individual sprite entities for ground and wall tiles, static tile layers are compiled into monolithic vertex/quad batches per chunk (`TILE_CHUNK`). Each chunk is submitted to `RenderQueue` as a single draw command sorted by layer depth band, running after `CameraFollowSystem` and before individual world sprites.

### Hysteresis Chunk Windowing
As the camera moves through the world, chunks stream in and out based on configurable load and unload radii (`set_window_radii`):
- **Load radius** (default 2 chunks around camera): loads static geometry and triggers prop spawning.
- **Unload radius** (default 3 chunks around camera): evicts distant chunks, despawning transient prop entities while preserving consumed state.
The gap between load and unload radii prevents thrashing when the camera hovers on chunk boundaries.

---

## 2. Lua API Reference (`tilemap.*`)

All tilemap functions live on the global `tilemap` table installed by `TilemapModuleLuaBinding`.

### Tile & Collision Bitmasks

| Constant | Value | Description |
|---|---|---|
| `tilemap.TILE_EMPTY` | `0` | Empty passable space. |
| `tilemap.TILE_SOLID` | `1` | Solid barrier blocking movement. |
| `tilemap.TILE_HAZARD` | `2` | Dangerous terrain (spikes, lava). |
| `tilemap.TILE_CHASM` | `3` | Pits / chasms (blocks ground movement, allows projectiles). |
| `tilemap.MASK_EMPTY` | `1 << 0` | Bitmask for empty tiles. |
| `tilemap.MASK_SOLID` | `1 << 1` | Bitmask for solid tiles. |
| `tilemap.MASK_HAZARD` | `1 << 2` | Bitmask for hazard tiles. |
| `tilemap.MASK_CHASM` | `1 << 3` | Bitmask for chasm tiles. |
| `tilemap.MASK_GROUND_BLOCKED` | `MASK_SOLID \| MASK_CHASM` | Convenience mask for standard ground unit collisions. |

---

### Loading & Lifecycle

#### `tilemap.load(path, opts?) -> bool`
Loads an `.ldtk` project from the asset catalog or disk:
```lua
local success = tilemap.load("tilemaps/world.ldtk", {
    seed = 1337,
    stitch_mode = "procedural",
    tile_scale = 1.0,
})
```
- `opts.seed` (optional integer): Random seed for procedural stitching.
- `opts.stitch_mode` (optional string): `"direct"`, `"procedural"`, or `"socket"`.
- `opts.tile_scale` (optional float): Global world scale factor applied to tile dimensions.

#### `tilemap.unload()`
Unloads the active tilemap, frees all chunk batches, and despawns all streamed prop entities. (Automatically called during scene swaps).

#### `tilemap.reload() -> bool`
Re-reads the currently loaded map file from disk and hot-reloads chunks while preserving camera state and consumed props.

#### `tilemap.is_loaded() -> bool`
Returns `true` if a tilemap is currently active.

---

### Window Radii & Chunk Streaming

#### `tilemap.set_window_radii(loadRadius, unloadRadius)`
Sets the active chunk streaming window in chunk units relative to the camera:
```lua
tilemap.set_window_radii(2, 3) -- Load 2 chunks out, unload beyond 3
```

#### `tilemap.get_active_chunk_count() -> integer`
Returns the count of chunks currently loaded in memory.

#### `tilemap.get_camera_chunk() -> integer, integer`
Returns the chunk grid coordinates `(chunkX, chunkY)` containing the current camera position.

#### `tilemap.get_chunk_size() -> integer, integer, integer, integer, integer`
Returns `chunkWidthTiles, chunkHeightTiles, tileSizePx, worldChunkWidthPx, worldChunkHeightPx`.

---

### Procedural Stitching & Chunk Overrides

#### `tilemap.set_stitch_mode(mode, seed?)`
Configures the edge-socket chunk assembly algorithm:
- `"direct"`: Uses authored world coordinates from LDtk.
- `"procedural"`: Randomly picks chunk templates matching adjacent socket tags.
- `"socket"`: Deterministic edge-socket matching with fallback stamps.

#### `tilemap.set_chunk_override(chunkX, chunkY, templateId)`
Forces a specific chunk template to be placed at `(chunkX, chunkY)`.

#### `tilemap.clear_chunk_overrides()`
Removes all manual chunk overrides, reverting to normal stitching.

#### `tilemap.get_templates() -> table`
Returns a table listing all chunk templates parsed from the map.

---

### Dynamic Prop Streaming & Persistence

Entities authored inside LDtk levels (chests, enemy spawners, torches, breakables) can be dynamically spawned into the ECS when their home chunk enters the active window.

#### `tilemap.set_prop_spawner(fn)`
Registers a callback invoked whenever a prop instance needs to be spawned:
```lua
tilemap.set_prop_spawner(function(prop)
    -- prop contains: id, name, chunk_x, chunk_y, prop_index, x, y, width, height, fields, template_id
    if prop.name == "Chest" then
        local e = load_entity({
            components = {
                transform = { position = { x = prop.x, y = prop.y } },
                sprite = { texture_asset_id = "chest_closed", width = 16, height = 16 },
                box_collider = { width = 16, height = 16 },
                chunk_resident = { chunk_x = prop.chunk_x, chunk_y = prop.chunk_y, prop_index = prop.prop_index },
            }
        })
    end
end)
```

#### Consumed Prop Tracking
To prevent collected pickups or destroyed chests from respawning when a chunk reloads:
- `tilemap.mark_prop_consumed(chunkX, chunkY, propIndex)`: Flags a prop as permanently consumed.
- `tilemap.is_prop_consumed(chunkX, chunkY, propIndex) -> bool`: Checks consumption state.
- `tilemap.clear_consumed_props()`: Resets all consumption flags (e.g. on new game or run restart).

---

### Spatial Queries & Physics Collision

All collision functions use world-space pixel coordinates and evaluate against the composite IntGrid collision bitmasks:

#### `tilemap.is_solid_at(worldX, worldY, mask?) -> bool`
Returns `true` if the point intersects any tile matching `mask` (default `MASK_SOLID`):
```lua
if tilemap.is_solid_at(player.x, player.y, tilemap.MASK_GROUND_BLOCKED) then
    -- Point is inside solid wall or chasm
end
```

#### `tilemap.get_tile_at(worldX, worldY) -> integer`
Returns the integer IntGrid tile value at the given world point (`0` if empty or out of bounds).

#### `tilemap.raycast(originX, originY, dirX, dirY, maxDist, mask?) -> table?`
Performs a fast 2D DDA (Digital Differential Analyzer) grid raycast. Returns `nil` if no tile was hit, or a hit table:
```lua
local hit = tilemap.raycast(x, y, dx, dy, 300.0, tilemap.MASK_SOLID)
if hit then
    log.info(string.format("Hit wall at (%0.1f, %0.1f) normal=(%0.1f, %0.1f) dist=%0.1f",
        hit.point.x, hit.point.y, hit.normal.x, hit.normal.y, hit.distance))
end
```

#### `tilemap.resolve_circle(centerX, centerY, radius, mask?) -> number, number, bool`
Resolves circle penetration against solid tiles, sliding along walls:
```lua
local newX, newY, collided = tilemap.resolve_circle(px, py, 12.0, tilemap.MASK_SOLID)
if collided then
    player.x = newX
    player.y = newY
end
```

#### `tilemap.resolve_aabb(left, top, right, bottom, mask?) -> number, number, number, number, bool`
Resolves axis-aligned bounding box penetration against solid tiles:
```lua
local l, t, r, b, collided = tilemap.resolve_aabb(box.l, box.t, box.r, box.b)
```

---

### Debug Visualization

#### `tilemap.set_debug_overlay(showBounds, showCollisions)`
Enables runtime wireframe overlay visualization:
- `showBounds`: Draws chunk boundaries and chunk coordinate labels.
- `showCollisions`: Draws colored collision overlays for IntGrid solid, hazard, and chasm tiles.

---

## 3. Scene Integration & Lifecycle

Tilemaps are tied to the active scene:
- When a scene loads, you can load the map in `setup()` or via `tilemap.load`.
- When switching scenes (`load_scene`), `SceneLoader::clearSceneEntities` automatically unloads the active tilemap and frees chunk geometry.
- If assets were preloaded at startup before loading any scene, tilemap texture references survive across scene swaps thanks to refcounting.
