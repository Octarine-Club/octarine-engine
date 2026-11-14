# Developer Tools & Automation

Octarine Engine provides built-in developer overlays, live hot reload, IDE LuaLS stub generation, deterministic headless execution, frame capture, and a loopback TCP dev protocol. Debug overlays, Lua developer bindings, and the TCP listener are compiled out when building with `-DOCTARINE_SHIPPED=ON`.

---

## 1. Launching in Dev Mode (`--dev`)

Start any non-shipped engine build (`player-debug`, `player-release`, `player-profile`) with `--dev` (or `-dev`):

```bash
./build/player-debug/bin/debug/OctarineEngine ./projects/my-game --dev
```

When `--dev` is active (`oct_dev_mode == true` in Lua):

- **ImGui overlays** and `dev.*` panels are enabled (`player-debug` and presets built with `OCTARINE_WITH_IMGUI=ON`).
- **LuaLS stubs refresh automatically:** On launch, `LuaApiManifest` writes `<assets>/types/octarine_api.lua` from the live `sol::state` so editors using Lua Language Server get autocomplete for engine globals, components, `ui`, `tilemap`, `input`, `dev`, and `ImGui`.
- **Override Lua API dump path:** Set `OCTARINE_DUMP_LUA_API=<path>` (or `OCTARINE_DUMP_LUA_API=1` to write `lua_api.lua` in the working directory) on any non-shipped run to export the stub manually.
- **Hot reload:** Non-shipped runs poll `ScriptHotReload`, `AssetHotReload`, `UIRuntime`, and `TilemapSystem` each frame so edits to Lua scripts, textures/assets, `.oxml`/`.oss`, and LDtk maps apply live.

> [!NOTE]
> Use `--scripts-dir <dir>` to override the directory watched/loaded for Lua scripts, or `--use-manifest` to force baked manifest asset loading in a dev player.

---

## 2. Custom ImGui Tooling in Lua (`dev.*` and `on_debug_gui`)

The `dev` module registers persistent panels, menu items, toggles, telemetry watches, and entity selection with the engine's `DebugMenuRegistry`. Unlike entity-scoped `script.on_debug_gui(self, entity)`, `dev.*` registrations apply engine-wide and are cleaned up on `dev.clear()` or scene stop (`StopScene` calls `DebugMenuRegistry::ClearLuaOwned()`).

| Function | Purpose |
|---|---|
| `dev.register_panel(id, fn_or_opts)` | Registers an ImGui panel; `opts` accepts `{ title, category, shortcut, auto_window, draw }`. |
| `dev.unregister_panel(id) -> bool` | Removes a Lua-registered panel. |
| `dev.is_panel_open(id) -> bool` | Queries whether a panel is open. |
| `dev.set_panel_open(id, open)` | Opens or closes a registered panel. |
| `dev.register_action(menu_path, fn, shortcut?)` | Adds an action callback to the debug menu bar. |
| `dev.register_toggle(menu_path, getter, setter)` | Adds a boolean menu toggle bound to Lua getter/setter functions. |
| `dev.watch(label, fn)` | Adds a live telemetry watch value rendered in the watch overlay. |
| `dev.unwatch(label) -> bool` | Removes a watch entry. |
| `dev.select_entity(entity?)` | Selects an entity in the inspector (or clears selection when `nil`). |
| `dev.get_selected_entity() -> Entity?` | Returns the currently selected entity handle, or `nil`. |
| `dev.clear()` | Unregisters all Lua-owned panels, actions, toggles, and watches. |

For quick scene-free UI toggling from Lua, call `toggle_debug_gui()` or `set_debug_gui(enabled)`.

```lua
if oct_dev_mode then
    dev.register_panel("spawner", {
        title = "Wave Spawner",
        category = "Gameplay",
        auto_window = true,
        draw = function()
            if ImGui.Button("Spawn Test Unit") then
                load_entity({
                    components = {
                        position = { x = 320, y = 240 },
                        sprite = { texture_asset_id = "enemy", width = 32, height = 32 },
                    },
                })
            end
        end,
    })

    dev.watch("Entities", function()
        return get_entity_count()
    end)
end
```

---

## 3. Native C++ Debug UI (`GameModuleHooks`)

Native C++ game modules (`OCTARINE_GAME_MODULE_DIR`) can install C++ Dear ImGui debug panels, menu actions, toggles, and watches via [`game_module::RegisterDebugUI`](../src/Engine/GameModuleHooks.h) using [`DebugMenuRegistry`](../src/Debug/DebugMenuRegistry.h) descriptors:

- `DebugPanelDescriptor` (`id`, `title`, `category`, `shortcut`, draw callback)
- `DebugMenuActionDescriptor` (`menuPath`, action callback, `shortcut`)
- `DebugMenuToggleDescriptor` (`menuPath`, getter, setter)
- `DebugWatchDescriptor` (`label`, sample callback)

See [`docs/extension-recipes.md`](extension-recipes.md) for wiring a native game module.

---

## 4. Headless Mode, Determinism, and Frame Capture

Run simulations in CI or offline scenarios without opening a desktop window using `--headless` (or `set_headless(bool)` at runtime):

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/player-debug/bin/debug/OctarineEngine ./projects/my-game --headless --startup-mode stress
```

- **Deterministic stepping helpers in Lua:** `set_deterministic(true)`, `set_sim_seed(seed)`, `set_fixed_dt(dt)`, `set_sim_substeps(n)`, `get_state_hash()`, `write_state_dump(rel_path)`.
- **Headless frame capture:** Set `OCTARINE_CAPTURE_PATH=/path/to/out.bmp` (and optionally `OCTARINE_CAPTURE_FRAME=<n>`, default `180`). The engine renders up to frame `n`, writes the scene texture to `OCTARINE_CAPTURE_PATH` as a BMP image, and exits cleanly.

---

## 5. Loopback TCP Dev Server (`--dev-listen`)

Non-shipped builds support `--dev-listen <port>` (`1..65535`), implemented in [`DevListenServer`](../src/Dev/DevListenServer.h) and [`DevListenDispatch`](../src/Dev/DevListenDispatch.cpp). The listener binds strictly to `127.0.0.1` and is stripped completely when `OCTARINE_SHIPPED=ON`.

| Opcode | Name | Purpose |
|---|---|---|
| `1` | `Hello` | Handshake with server/session info |
| `2` | `Ping` | Keep-alive / latency check |
| `10` | `EvalLua` | Evaluate a Lua chunk inside the live `sol::state` |
| `11` | `ReloadScene` | Trigger a deferred scene reload |
| `12` | `PushScript` | Push script source updates over the dev socket |
| `13` | `PushAsset` | Push updated asset data over the dev socket |
| `20` | `Step` | Advance the simulation by N fixed ticks while paused |
| `21` | `Pause` | Pause or resume simulation ticks |
| `22` | `Inspect` | Query ECS state (`summary`, `entities`, `hash[:scale]`, `entity:<id>`, `query:<list>`) |
