# Lua Scripting Guide

Octarine Engine is driven by Lua 5.4 scripts (`sol2`). This guide covers project layout, `config.ini`, the `script` component (inline and file-backed hot reload), component proxies, global helpers, `storage.*`, `settings.*`, `input.*`, ImGui/developer tooling, and C++ component-binding rules.

For a complete runnable example, see [`Octarine-Engine-Example`](https://github.com/Octarine-Club/octarine-engine-example).

---

## 1. Project Structure

Scaffold a standalone project with `./scripts/octarine-new.sh --name "My Game" --package-id com.example.mygame --dir ./my-game`. A typical layout:

```text
MyGame/
├── config.ini          # Engine and window configuration
├── project.ini         # Package identity for shipping builds
├── game.lua            # Startup script referenced by config.ini
├── scripts/            # Additional Lua modules and entity scripts
├── scenes/             # Scene files loaded via load_scene()
├── ui/                 # Declarative .oxml templates and .oss stylesheets
├── images/             # Textures (.png, .bmp) and optional .meta sidecars
├── fonts/              # Fonts (.ttf)
├── sounds/             # Audio (.wav, .ogg)
├── tilemaps/           # LDtk maps (.ldtk) and tilesets
└── types/              # Auto-generated octarine_api.lua on --dev launch
```

---

## 2. Configuration (`config.ini`)

`config.ini` lives at the project root:

```ini
Title=My Awesome Game
StartupScript=game.lua
BaseResolutionWidth=1280
BaseResolutionHeight=720
DefaultWindowWidth=1280
DefaultWindowHeight=720
DefaultWindowMode=windowed       # 'windowed', 'borderless', or 'exclusive'
DefaultPresentationMode=expand   # 'expand', 'letterbox', 'integer_scale', or 'stretch'
DefaultScalingMode=nearest       # 'nearest', 'linear', or 'pixelart'
FpsTarget=60                     # frame-rate cap; 0 = uncapped (default 60)
AngleUnit=degrees                # 'degrees' (default) or 'radians'
```

`AngleUnit` controls the unit applied at every Lua boundary (`rotation` in entity tables, `set_rotation()`, and the `rotation` component's `value`). Internal engine storage is always radians. Because `AngleUnit` defaults to `degrees`, scripts written in radians must set `AngleUnit=radians`.

Launch with `--dev` to refresh `<assets>/types/octarine_api.lua` automatically for IDE LuaLS autocomplete (see [`docs/dev-tools.md`](dev-tools.md)).

---

## 3. The `script` Component

Attach a `script` table to any entity for per-frame Lua callbacks:

| Callback | When called |
|---|---|
| `on_update(self, entity, dt)` | Every simulation step, in system order (`dt` in seconds) |
| `on_collision(self, entity, other)` | When this entity's collider enters contact with `other` |
| `on_collision_exit(self, entity, other)` | When this entity's collider separates from `other` |
| `on_debug_gui(self, entity)` | Every frame when debug UI is visible (compiled out in shipped builds) |
| `on_click(self, entity)` | When an entity with `ui_button` is activated via pointer or UI focus |

- **`self`** is the script's own Lua table; store per-entity state directly on `self`.
- **`entity`** is the entity handle (`entity:get_id()` returns the raw ID).
- **`dt`** is the simulation delta time in seconds.

```lua
load_entity({
    components = {
        transform = { position = { x = 100, y = 100 } },
        sprite    = { texture_asset_id = "player", width = 32, height = 32, layer = 2 },
        rigidbody = { velocity = { x = 0, y = 0 } },
        script    = {
            speed    = 120,
            cooldown = 0,
            on_update = function(self, entity, dt)
                self.cooldown = self.cooldown - dt
            end,
        },
    },
})
```

### File-Backed Scripts and Hot Reload

To opt a `script` component into live hot reload on non-shipped builds, reference a Lua file returning a script table via `source`:

```lua
script = { source = "scripts/player.lua" }
```

When `scripts/player.lua` changes on disk, `ScriptHotReload` reloads callbacks while preserving existing table data, and keeps prior callbacks intact if a syntax error occurs.

---

## 4. Reading and Writing Components

### `registry.get_*` / `registry.has_*`

`registry` exposes `registry.has_<key>(entity)` and `registry.get_<key>(entity)` for every registered component (see [`docs/ecs-components.md`](ecs-components.md) for all 39 keys and fields), plus lifetime and pause helpers:

```lua
local pos = registry.get_position(entity)
pos.x = pos.x + 10

if registry.has_lifetime(entity) then
    local lt = registry.get_lifetime(entity)
    lt:extend(1.0)
end

local rb = registry.get_rigidbody(entity)
rb.velocity.x = 150
rb.velocity.y = 0
```

### Entity Hierarchy & Lifecycle

| Function | Description |
|---|---|
| `registry.is_alive(entity)` | Returns `true` if `entity` is currently alive |
| `registry.get_parent(entity)` | Returns parent entity, or `nil` |
| `registry.set_parent(child, parent)` | Reparents `child` under `parent` (self/cycle reparenting is rejected) |
| `registry.is_paused()` / `registry.set_paused(bool)` | Query or toggle ECS simulation pause state |

### Convenience & Gameplay Globals

| Function | Description |
|---|---|
| `load_entity(table)` | Spawns an entity (and optional nested `entities`); returns root handle or `nil` |
| `blam(entity_or_table)` | Queues deferred end-of-frame destruction (single entity or list) |
| `find_entity_by_name(name)` | Returns entity handle or `nil` |
| `get_entity_count()` | Returns live entity count |
| `get_name(entity)` / `set_name(entity, name)` | Read or update `name` component |
| `get_position(entity)` / `get_position_xy(entity)` | Returns `{x, y}` table, or `(x, y)` multiple return values |
| `set_position(entity, x, y)` | Teleports entity |
| `set_rotation(entity, angle)` | Sets rotation in `AngleUnit` |
| `set_sprite_src_rect(entity, x, y)` | Updates sprite source rectangle origin |
| `are_colliding(e1, e2)` | Returns `true` if `e1` and `e2` are currently overlapping |
| `load_scene(path)` / `reload_scene()` / `stop_scene()` / `clear_scene()` | Deferred scene transition and cleanup |
| `load_asset(table)` / `acquire_scene_assets(scene)` / `get_asset_path(rel)` | Load or resolve project assets |
| `play_sound(clip_id, volume?)` | Plays an audio asset (`volume` optional) |
| `get_master_volume()` / `set_master_volume(v)` | Global audio gain |
| `is_audio_muted()` / `set_audio_muted(bool)` | Mute or unmute mixer output |
| `get_camera_position()` / `get_camera_zoom()` / `set_camera_zoom(z)` | Camera viewport origin and zoom |
| `get_game_map_dimensions()` / `set_game_map_dimensions(w, h)` | Clamp camera bounds |
| `log(msg)` / `log_i(msg)` / `log_w(msg)` / `log_e(msg)` / `log_table(tbl, label?)` | Logging helpers |
| `read_file_lines(path)` / `write_project_file(path, text)` | Safe file I/O helpers |
| `get_fps()` / `get_avg_fps()` / `get_frame_time()` / `get_memory_usage()` / `get_perf_stats()` | Runtime performance telemetry |
| `get_env(name)` / `quit_game()` | Process environment lookup and clean shutdown |

Runtime window, display, and determinism globals (see also [`docs/dev-tools.md`](dev-tools.md)):
`is_fullscreen` / `set_fullscreen`, `get_window_mode` / `set_window_mode`, `get_presentation_mode` / `set_presentation_mode`, `get_texture_scale_mode` / `set_texture_scale_mode`, `get_vsync` / `set_vsync`, `get_window_size` / `set_window_size`, `get_base_resolution`, `get_logical_size`, `get_displays`, `get_display_modes`, `set_window_display`, `get_fps_target` / `set_fps_target`, `set_deterministic`, `is_deterministic`, `set_sim_seed`, `set_fixed_dt` / `get_fixed_dt`, `set_sim_substeps` / `get_sim_substeps`, `get_state_hash`, `write_state_dump`, `set_headless` / `is_headless`, `set_debug_gui` / `toggle_debug_gui`.

Bootstrap read-only globals: `oct_startup_mode`, `oct_dev_mode`, `game_window_width`, `game_window_height`.

---

## 5. Sandboxed Save Storage (`storage`) and User Settings (`settings`)

### `storage` (Per-Game Save Root)

`storage.*` performs sandboxed file I/O under the platform save directory (`SDL_GetPrefPath`, overridable via `OCTARINE_SAVE_PATH`). Paths attempting `..` traversal or absolute escapes are rejected.

| Function | Description |
|---|---|
| `storage.write(rel_path, contents) -> bool` | Write string contents |
| `storage.read(rel_path) -> string?` | Read file contents, or `nil` if missing |
| `storage.write_table(rel_path, tbl) -> bool` | Serialize a Lua table safely |
| `storage.read_table(rel_path) -> table?` | Load a serialized Lua table (evaluated in an empty environment) |
| `storage.exists(rel_path) -> bool` | Check if save path exists |
| `storage.remove(rel_path) -> bool` | Delete save file |
| `storage.list(rel_path?) -> string[]` | List entries under save root or subdirectory |

### `settings` (`user_settings.ini`)

`settings.*` defines schema defaults, reads/writes typed user preferences (including built-in keys such as `master_volume`, `audio_muted`, `fps_target`, `window_mode`, `presentation_mode`, `texture_scale_mode`, and `vsync`), and persists changes to `user_settings.ini` in the save root:

```lua
settings.define({
    sfx_volume = 0.8,
    screen_shake = true,
    difficulty = "normal",
})

local vol = settings.get("sfx_volume")
settings.set("screen_shake", false)
settings.save()
```

Full `settings` API: `settings.define(key_or_table, default?)`, `settings.get(key, fallback?)`, `settings.set(key, val)`, `settings.has(key)`, `settings.get_default(key)`, `settings.reset(key)`, `settings.reset_all()`, `settings.get_all()`, `settings.save()` (plus global shorthands `get_user_setting`, `set_user_setting`, `save_user_settings`).

---

## 6. Input

The `input` module handles action binding, keyboard/mouse/gamepad polling, and device callbacks. See [`docs/input-and-controllers.md`](input-and-controllers.md) for the complete reference.

```lua
input.set_bindings({
    move_up    = { keys = { "w", "up" },    gamepad = "dpad_up" },
    move_down  = { keys = { "s", "down" },  gamepad = "dpad_down" },
    move_left  = { keys = { "a", "left" },  gamepad = "dpad_left" },
    move_right = { keys = { "d", "right" }, gamepad = "dpad_right" },
    fire       = { keys = { "space" },      gamepad = { "south", "rt" } },
})

if input.is_action_down("fire") and input.is_focused() then
    -- window has input focus and fire is active
end
```

Action bindings persist across `load_scene()` / `reload_scene()`; `input.on_*` callbacks are cleared on scene swap. Also check `input.is_focused()` and `input.is_hovered()` for window focus and pointer hover state.

---

## 7. ImGui, Dev Tools, and Domain Modules

- **`dev` / `ImGui`:** Non-shipped builds expose `ImGui.*`, `script.on_debug_gui(self, entity)`, and engine-wide `dev.*` helpers (`dev.register_panel`, `dev.register_action`, `dev.register_toggle`, `dev.watch`, `dev.select_entity`). See [`docs/dev-tools.md`](dev-tools.md).
- **`ui`:** Declarative `.oxml`/`.oss` UI framework (`ui.mount`, `ui.observable`, `ui.register_widget`, `ui.debug_draw`) and procedural layout helpers (`ui.canvas`, `ui.anchor`, `ui.flex`, `ui.box`, `ui.clip`, `ui.nine_slice`, `ui.scroll`, `ui.focus`, `ui.get_focused`). See [`docs/ui-framework.md`](ui-framework.md) and [`docs/ui-layout.md`](ui-layout.md).
- **`tilemap`:** Infinite chunked LDtk tilemaps, IntGrid masks, 2D DDA raycasting, circle/AABB resolution, and procedural stitching. See [`docs/tilemaps.md`](tilemaps.md).

---

## 8. Adding Component Methods (Engine Contributors & Native Game Modules)

When adding a `LuaBinding<T>` specialization, `bindUsertype` can expose both fields and member functions to Lua:

```cpp
lua.new_usertype<LifetimeComponent>(
    kUsertypeName, sol::no_constructor,
    "remaining_duration", sol::readonly(&LifetimeComponent::remainingDuration),
    "duration",
    sol::property([](const LifetimeComponent& s) { return s.lifetimeDuration; },
                  [](LifetimeComponent& s, const float d) { s.lifetimeDuration = d; }),
    "is_alive", &LifetimeComponent::IsAlive,
    "extend",   &LifetimeComponent::Extend,
    "reset",    &LifetimeComponent::Reset);
```

### Component Method Invariant

A component method may read and write **the component's own fields only**:

- No `Registry&` access or cross-entity reads/writes.
- No `EventBus*` access or event emission.
- No archetype mutation (`AddComponent` / `RemoveComponent` / `BlamEntity`).
- No heap allocation, file I/O, or external side effects.

Anything requiring cross-entity coordination or events belongs in a system. Bind read-only derived values with `sol::readonly` or `sol::property(&T::Accessor)`, and enforce field invariants inside setter properties.

Reference implementations: [`LuaBinding.h`](../src/Lua/Bindings/LuaBinding.h), [`LifetimeComponentLuaBinding.h`](../src/Lua/Bindings/LifetimeComponentLuaBinding.h), and [`RegisterTransformCoreBindings.cpp`](../src/Lua/Bindings/RegisterTransformCoreBindings.cpp).
