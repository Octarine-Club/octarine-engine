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
                               v
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
| `tilemap.TILE_EMPTY` | `0` | Empty passable space (`MASK_EMPTY = 1 << 0`). |
| `tilemap.TILE_SOLID` (`TILE_1`) | `1` | Channel 1: solid barrier (`MASK_SOLID = MASK_1 = 1 << 1`). |
| `tilemap.TILE_CHASM` (`TILE_2`) | `2` | Channel 2: pits / chasms (`MASK_CHASM = MASK_2 = 1 << 2`). |
| `tilemap.TILE_HAZARD` (`TILE_3`) | `3` | Channel 3: hazard terrain (`MASK_HAZARD = MASK_3 = 1 << 3`). |
| `tilemap.TILE_SURFACE` (`TILE_4`) | `4` | Channel 4: surface tag (`MASK_SURFACE = MASK_4 = 1 << 4`). |
| `tilemap.TILE_WALL` (`TILE_5`) | `5` | Channel 5: elevated wall (`MASK_WALL = MASK_5 = 1 << 5`). |
| `tilemap.TILE_6`, `tilemap.TILE_7` | `6`, `7` | Custom user channels (`MASK_6 = 1 << 6`, `MASK_7 = 1 << 7`). |
| `tilemap.MASK_GROUND_BLOCKED` | `MASK_SOLID \| MASK_CHASM \| MASK_WALL` | Default collision mask for `is_solid_at`, `resolve_circle`, and `resolve_aabb`. |
| `tilemap.MASK_PROJECTILE_BLOCKED` | `MASK_SOLID \| MASK_WALL` | Default mask for `raycast`. |

---

### Loading & Lifecycle

#### `tilemap.load(path, opts?) -> bool`
Loads an `.ldtk` project (or supported map format) from the asset root or disk:
```lua
local success = tilemap.load("tilemaps/world.ldtk", {
    seed = 1337,
    stitch_mode = "procedural_hash",
    tile_scale = 1.0,
})
```

#### `tilemap.load_json(jsonText, opts?) -> bool`
Parses and loads a tilemap directly from a JSON string.

#### `tilemap.load_data(dataTable, opts?) -> bool`
Builds and loads a procedural or custom tilemap directly from a Lua table definition (`{ chunk_grid_w, chunk_grid_h, tile_size, tilesets, templates }`).

Common `opts` fields accepted by `load`, `load_json`, and `load_data`:
- `seed` (integer), `stitch_mode` (`"toroidal"`, `"procedural_hash"`, `"arena_fixed"`), `tile_scale` (number), `variant_count` (integer), `format` (string hint).
- `load_radius`, `unload_radius` (integers), `ysort_layer` (integer), `solid_mask`, `raycast_mask` (integers).
- `collision_layers`, `ysort_layers`, `palette_names` (arrays/rules), `socket_anchor_tile` (integer), `slice_multi_chunk` (bool).

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
Returns `chunkPixelWidth, chunkPixelHeight, chunkGridWidth, chunkGridHeight, tileSizePx`.

---

### Procedural Stitching & Chunk Overrides

#### `tilemap.set_stitch_mode(mode, seed?)`
Configures the chunk assembly algorithm (`seed` defaults to the active seed):
- `"toroidal"`: Tiles world templates toroidally across chunk coordinates (default).
- `"procedural_hash"` (alias `"procedural"`): Deterministic hashed template/socket selection per coordinate.
- `"arena_fixed"` (alias `"arena"`): Fixed bounded arena layout.

#### `tilemap.get_stitch_mode() -> string` / `tilemap.get_seed() -> integer`
Returns the active canonical stitch mode string (`"toroidal"`, `"procedural_hash"`, `"arena_fixed"`) or RNG seed.

#### `tilemap.set_chunk_override(chunkX, chunkY, templateId?) -> bool`
Forces a specific chunk template (or clears the override when `templateId` is omitted) at `(chunkX, chunkY)`.

#### `tilemap.clear_chunk_overrides()`
Removes all manual chunk overrides, reverting to normal stitching.

#### `tilemap.get_chunk_template_id(chunkX, chunkY) -> string`
Returns the resolved template identifier assigned to `(chunkX, chunkY)` (empty if none).

#### `tilemap.get_templates() -> table`
Returns a list of chunk template tables (`id`, `biome`, `weight`, `edge_north/east/south/west`, `fields`, `props`).

---

### Dynamic Prop Streaming & Persistence

Entities authored inside LDtk levels (chests, enemy spawners, torches, breakables) can be dynamically spawned into the ECS when their home chunk enters the active window.

#### `tilemap.set_prop_spawner(fn)`
Registers a callback invoked whenever a prop instance needs to be spawned (pass non-function / `nil` to clear). Returning an `Entity` (or table of entities) tracks it as chunk-resident so it despawns automatically when the chunk unloads:
```lua
tilemap.set_prop_spawner(function(id, worldX, worldY, fields, chunkX, chunkY, propIndex)
    if id == "Chest" then
        return load_entity({
            components = {
                transform      = { position = { x = worldX, y = worldY } },
                sprite         = { texture_asset_id = "chest_closed", width = 16, height = 16 },
                box_collider   = { width = 16, height = 16 },
                chunk_resident = { chunk_x = chunkX, chunk_y = chunkY, prop_index = propIndex },
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
Returns `true` if the point intersects any tile matching `mask` (defaults to the system solid mask, `MASK_GROUND_BLOCKED`):
```lua
if tilemap.is_solid_at(player.x, player.y, tilemap.MASK_GROUND_BLOCKED) then
    -- Point is inside solid wall or chasm
end
```

#### `tilemap.get_tile_at(worldX, worldY) -> integer`
Returns the integer IntGrid tile value at the given world point (`0` if empty or out of bounds).

#### `tilemap.raycast(x0, y0, x1, y1, mask?) -> bool, number, number, number, number, number, integer`
Performs a fast 2D DDA grid raycast from `(x0, y0)` to `(x1, y1)` against tiles matching `mask` (defaults to the system raycast mask, `MASK_PROJECTILE_BLOCKED`). Returns `hit, hitX, hitY, normalX, normalY, distance, tileValue`:
```lua
local hit, hx, hy, nx, ny, dist, tile = tilemap.raycast(x, y, x + dx * 300, y + dy * 300, tilemap.MASK_SOLID)
if hit then
    log_i(string.format("Hit tile %d at (%.1f, %.1f) normal=(%.1f, %.1f) dist=%.1f",
        tile, hx, hy, nx, ny, dist))
end
```

#### `tilemap.resolve_circle(centerX, centerY, radius, mask?) -> number, number`
Resolves circle penetration against tiles matching `mask` and returns the pushed center `(newX, newY)`:
```lua
local newX, newY = tilemap.resolve_circle(px, py, 12.0, tilemap.MASK_GROUND_BLOCKED)
```

#### `tilemap.resolve_aabb(x, y, width, height, mask?) -> number, number`
Resolves top-left axis-aligned box `(x, y, width, height)` penetration against tiles matching `mask` and returns `(newX, newY)`:
```lua
local newX, newY = tilemap.resolve_aabb(box.x, box.y, box.w, box.h)
```

#### `tilemap.set_default_masks(solidMask, raycastMask)` / `tilemap.get_default_masks() -> integer, integer`
Sets or reads the default bitmasks used when `mask` is omitted on `is_solid_at` / `resolve_*` (`solidMask`) and `raycast` (`raycastMask`).

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
