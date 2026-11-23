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

---

## 6. UI Previewer, Mock Sidecars & Layout Automation (`-m ui-preview` & `dev.*`)

Non-shipped builds include [`UIPreviewer`](../src/UI/UIPreviewer.h) and the ImGui **UI Previewer** panel (`UI` -> `UI Previewer`; bind your own hotkeys in Lua via `dev.set_panel_open("engine.ui_previewer", ...)` and `dev.reload_preview_ui()`). Hovering rows in the **Layout Audit** tab highlights the target entity (`UIRectComponent`, or a `12x12px` reticle when `0x0`) in amber; clicking pins/unpins selection in cyan. Enabling **Safe Areas** draws `93%` action-safe (cyan) and `90%` title-safe (gold) viewport guides.

### Standalone CLI Flags (`-m ui-preview`)

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/player-debug/bin/debug/OctarineEngine ./projects/my-game \
  -m ui-preview --headless --ui-file assets/ui/hud.oxml \
  --ui-matrix ./out/hud_matrix --ui-resolutions 1920x1080,1280x800,800x600 --ui-presets all
```

| Flag | Purpose |
|---|---|
| `--ui-file <path_or_id>` | `.oxml` view to mount in preview mode (defaults to first catalog `.oxml`). |
| `--ui-mock <path.lua>` | Explicit Lua mock script overriding `<view>.preview.lua` sidecar auto-loading. |
| `--ui-preset <name>` | Named preset in `.preview.lua`, or stress preset (`@default`, `@empty`, `@overflow`). |
| `--ui-size <WxH>` | Simulated viewport resolution (also supports `800x600`, `1280x800`, etc.). |
| `--ui-capture <path.png>` | One-shot PNG screenshot output (non-`ui-preview` runs also support `OCTARINE_CAPTURE_PATH`). |
| `--ui-audit-json <path\|->` | Write layout and contract audit JSON (`-` writes to stdout). |
| `--ui-matrix <out_dir>` | Sweep `(preset x resolution)` captures + `matrix_report.json` + portable `index.html` contact sheet. |
| `--ui-resolutions <csv>` | Comma-separated `WxH` list for `--ui-matrix` (default: `1920x1080,1280x800,800x600`). |
| `--ui-presets <csv>` | Preset list, `auto`, or `all` for `--ui-matrix`. |

### `dev.*` UI Preview & Audit Lua API

Available in non-shipped builds (live `--dev` sessions or via `--dev-listen` `EvalLua`):

| Function | Purpose |
|---|---|
| `dev.preview_ui(path_or_id, opts?) -> bool` | Mounts an `.oxml` view in `UIPreviewer` (`opts`: `{ context, controller, params, styles }`). |
| `dev.preview_inline_ui(xml, oss?) -> bool` | Mounts inline OXML + optional OSS strings in the previewer. |
| `dev.unmount_preview_ui()` | Unmounts the active preview and restores scene UI isolation state. |
| `dev.reload_preview_ui() -> bool` | Reloads the active `.oxml`, `.oss`, and `.preview.lua` mock script. |
| `dev.list_ui_views() -> table` | Returns array of `{ id, path }` catalog `.oxml` entries. |
| `dev.audit_ui() -> table` / `dev.audit_ui_json() -> string` | Runs geometric, focus, and contract audit rules and returns Lua table or JSON. |
| `dev.inspect_ui_contract(path_or_id?) -> table` | Inspects `<Param>`, `{var}`, `ref`, slot, and callback contracts (live or static `.oxml`). |
| `dev.inspect_ui_contract_json(path_or_id?) -> string` | Returns live UI preview state, diagnostics, contract, and audit issues as JSON. |
| `dev.set_ui_mock_field(path, value) -> bool` | Updates a mock `context` field live by dotted path. |
| `dev.resize_ui_mock_array(path, len) -> bool` | Resizes a repeater table (`each="item in list"`) live using contract item hints. |
| `dev.set_ui_param(name, value) -> bool` | Overrides a `<Param>` value (`string`, `bool`, or `number`). |
| `dev.load_ui_mock(path) -> bool` | Loads a sandboxed `.lua` mock script onto the active preview. |
| `dev.save_ui_mock(path?, force?) -> bool` | Saves live mock snapshot to `.preview.lua` (`force=true` overwrites controller functions). |
| `dev.export_ui_mock_lua() -> string` | Serializes the live mock snapshot as Lua source without writing to disk. |
| `dev.seed_ui_mock(path?) -> bool` | Seeds a single unset contract field or all missing contract fields. |
| `dev.set_ui_viewport(w, h) -> bool` | Sets simulated viewport size (`0, 0` restores native window dimensions). |
| `dev.get_ui_viewport_json() -> string` | Returns active width, height, `ui_scale`, `simulated`, `safe_areas`, `integer_zoom`. |
| `dev.set_ui_safe_area_guides(enabled) -> bool` | Enables or disables 93% action-safe / 90% title-safe viewport guides. |
| `dev.set_ui_integer_zoom(enabled) -> bool` | Enables or disables integer zoom scaling during viewport simulation. |
| `dev.list_ui_presets_json() -> string` | Lists authored `.preview.lua` presets, active/default preset, and stress tags. |
| `dev.select_ui_preset(name) -> bool` | Switches to an authored preset or `@default` / `@empty` / `@overflow`. |
| `dev.save_ui_preset(name, set_default?, force?) -> bool` | Saves current live state as a named preset inside `.preview.lua`. |
| `dev.delete_ui_preset(name, force?) -> bool` | Deletes a named preset from `.preview.lua` (preserving at least one preset). |
| `dev.apply_ui_stress_preset(kind) -> bool` | Applies `"default"`, `"empty"`, or `"overflow"` stress seed data live. |
| `dev.run_ui_matrix_json(out_dir, resolutions_csv?, presets_csv?) -> string` | Executes a `(preset x resolution)` sweep writing PNGs, `matrix_report.json`, and `index.html`. |
