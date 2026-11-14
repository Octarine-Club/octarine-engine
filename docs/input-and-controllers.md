# Input & Controllers (`input`)

Octarine handles keyboard, mouse, and gamepads through the global `input` Lua module (`InputSystem`). Gamepad axes, triggers, hotplugging, and UI focus navigation run natively in C++.

---

## 1. Quickstart: Declarative Action Bindings

Define all game actions in one table with `input.set_bindings(tbl, opts?)` at startup (for example in `game.lua` or an `input_map` module). Bindings **survive scene transitions** (`load_scene` / `reload_scene`).

```lua
input.set_bindings({
    confirm    = { keys = { "return", "space" }, gamepad = { "south" } },
    cancel     = { keys = { "escape" },          gamepad = "east" },
    pause      = { keys = { "escape", "p" },     gamepad = "start" },
    move_up    = { keys = { "w", "up" },         gamepad = "dpad_up" },
    move_down  = { keys = { "s", "down" },       gamepad = "dpad_down" },
    move_left  = { keys = { "a", "left" },       gamepad = "dpad_left" },
    move_right = { keys = { "d", "right" },      gamepad = "dpad_right" },
    alt_fire   = { keys = "shift",               gamepad = { "rt", "rb" } },
})
```

### `set_bindings` options and `get_bindings`

- **Replace mode (default):** `input.set_bindings(tbl)` clears all previous keyboard and gamepad action bindings first, then installs `tbl`. Safe to call on scene reload without duplicating bindings.
- **Merge mode:** `input.set_bindings(tbl, { merge = true })` leaves unrelated actions untouched and replaces only the `keys` or `gamepad` fields present on each action entry in `tbl`.
- **String or array:** Each action accepts `{ keys = "space" | {...}, gamepad = "south" | {...} }`. Omitting `keys` or `gamepad` leaves that device unbound (or unchanged under `{ merge = true }`).
- **Inspect active bindings:**

```lua
local confirm = input.get_bindings("confirm") -- { keys = { "return", "space" }, gamepad = { "south" } }
local all     = input.get_bindings()          -- { [action_name] = { keys = {...}, gamepad = {...} }, ... }
```

Action names in `get_bindings()` are sorted, and keys and gamepad button names are canonicalized in binding order.

---

## 2. Canonical Gamepad Buttons & Aliases

Use layout-neutral positional names (`south`, `east`, `west`, `north`) so docs and bindings work consistently across Xbox, PlayStation, Switch, and SDL layouts. Xbox (`a`/`b`/`x`/`y`), PlayStation (`cross`/`circle`/`square`/`triangle`), and SDL (`dpup`, `leftshoulder`, `lefttrigger`) aliases are accepted on every input function, `get_bindings` / `on_gamepad_down` / `on_gamepad_up` always emit the canonical name:

| Canonical Name | Role | Accepted Aliases |
|---|---|---|
| `south` | Bottom face button | `a`, `cross`, `x_ps`, `face_down`, `bottom` |
| `east` | Right face button | `b`, `circle`, `face_right`, `right_face` |
| `west` | Left face button | `x`, `square`, `face_left`, `left_face` |
| `north` | Top face button | `y`, `triangle`, `face_up`, `top` |
| `lb` / `rb` | Bumpers / L1 & R1 | `l1`, `leftshoulder`, `left_shoulder` / `r1`, `rightshoulder`, `right_shoulder` |
| `lt` / `rt` | Analog triggers (digital) | `l2`, `lefttrigger`, `left_trigger` / `r2`, `righttrigger`, `right_trigger` |
| `ls` / `rs` | Stick clicks (L3 & R3) | `l3`, `leftstick`, `left_stick` / `r3`, `rightstick`, `right_stick` |
| `dpad_up` / `dpad_down` | Vertical D-Pad | `dpup`, `up`, `dpad up` / `dpdown`, `down`, `dpad down` |
| `dpad_left` / `dpad_right` | Horizontal D-Pad | `dpleft`, `left`, `dpad left` / `dpright`, `right`, `dpad right` |
| `start` | Menu / Options | `options`, `menu` |
| `back` | View / Share / Select | `select`, `view`, `share` |
| `guide` | System / Home button | `home`, `ps`, `xbox` |

Extra hardware controls (`misc1`, `touchpad`, `paddle1`–`paddle4`) are also normalized if present.

### Key Aliases & Diagnostics

Common key aliases are normalized (`"esc"` → `"escape"`, `"enter"` / `"keypad enter"` → `"return"`, `"spacebar"` → `"space"`). In non-shipped builds (`not OCTARINE_SHIPPED`), unrecognized key, button, stick, or axis names log a one-time warning (`WarnUnknownToken`) to catch typos early.

---

## 3. Polling Actions, Keys, Gamepads & Mouse

All edge checks (`is_*_pressed`, `is_*_released`) are single-frame edges evaluated in the simulation frame. Viewport-gated queries return `false` / `0` when the window lacks focus (`not input.is_focused()`).

```lua
-- Bound logical actions (keyboard + gamepad combined)
if input.is_action_pressed("pause")    then toggle_pause() end
if input.is_action_down("alt_fire")    then charge_beam() end
if input.is_action_released("alt_fire") then release_beam() end

-- Raw keyboard, mouse & window focus
if input.is_focused() and input.is_hovered() then ... end
if input.is_key_down("w") or input.is_key_pressed("escape") or input.is_key_released("space") then ... end
local pos   = input.mouse_position() -- vec2 { x, y }, viewport-local
local wheel = input.mouse_wheel()    -- vec2 { x, y }
if input.is_mouse_down(1) or input.is_mouse_pressed(1) or input.is_mouse_released(1) then ... end -- 1 = left, 2 = right, 3 = middle

-- Raw gamepad
if input.is_gamepad_connected() then ... end
if input.is_gamepad_pressed("south") then ... end
if input.is_gamepad_down("rt")       then ... end
if input.is_gamepad_released("rt")   then ... end
```

### Trigger Thresholds (`lt` / `rt`)

Because SDL exposes triggers (`lefttrigger` / `righttrigger`) as analog axes rather than buttons, `InputSystem` automatically synthesizes digital press/down/release events for `"lt"` and `"rt"` using **hysteresis**:
- **Pressed threshold:** `> 0.45` (when viewport is focused).
- **Released threshold:** `< 0.35`.

Triggers work seamlessly in `set_bindings`, `is_gamepad_*`, `.oxml` `gamepad="rt"`, and `on_gamepad_down("rt")`.

---

## 4. Analog Sticks & Axes

Read normalized stick vectors and single axes with scaled radial/linear deadzones:

```lua
-- Sticks: returns vec2 with magnitude rescaled smoothly from [deadzone, 1.0] -> [0.0, 1.0]
local move = input.gamepad_stick("left", 0.18)   -- default deadzone: 0.18
local aim  = input.gamepad_stick("right", 0.20)  -- aliases: "left"/"ls"/"left_stick", "right"/"rs"/"right_stick"

-- Single axes: returns float (sticks in [-1, 1], triggers in [0, 1])
local lx = input.gamepad_axis("leftx", 0.15)     -- default deadzone: 0.15
local ly = input.gamepad_axis("lefty")
local rx = input.gamepad_axis("rightx")
local ry = input.gamepad_axis("righty")
local lt = input.gamepad_axis("lt", 0.05)        -- "lt"/"l2"/"lefttrigger", "rt"/"r2"/"righttrigger"
```

### Example: Combined Analog Stick + Digital Movement Vector

```lua
local function get_move_vector()
    local stick = input.gamepad_stick("left", 0.18)
    local x, y = stick.x, stick.y

    if input.is_action_down("move_left")  then x = x - 1 end
    if input.is_action_down("move_right") then x = x + 1 end
    if input.is_action_down("move_up")    then y = y - 1 end
    if input.is_action_down("move_down")  then y = y + 1 end

    local len = math.sqrt(x * x + y * y)
    if len > 1.0 then
        x, y = x / len, y / len
    end
    return x, y
end
```

---

## 5. Incremental Rebinding & Runtime Options

For key-remapping menus, mutate individual devices cleanly without touching the other device:

```lua
input.bind_key("pause", "p")         -- add key to action
input.unbind_key("pause", "p")       -- remove single key
input.unbind_key("pause")            -- clear all keyboard bindings for "pause" (keeps gamepad)

input.bind_gamepad("pause", "start") -- add gamepad button (canonicalized)
input.unbind_gamepad("pause", "back")
input.unbind_gamepad("pause")        -- clear all gamepad bindings for "pause" (keeps keys)
```

Legacy single-device calls (`input.bind(action, token)`, `input.unbind(action, token?)`) still accept optional `"key:"`, `"gamepad:"`, or `"controller:"` prefixes for backward compatibility; prefer `set_bindings` or `bind_key` / `bind_gamepad` for new code.

---

## 6. Event Callbacks, Multi-Gamepad & Scene Lifecycle

```lua
-- Register inside scene initialization (cleared on scene swap)
input.on_key_down(function(key_name) ... end)
input.on_key_up(function(key_name) ... end)
input.on_gamepad_down(function(button_name) ... end) -- canonical name: "south", "east", "lt", etc.
input.on_gamepad_up(function(button_name) ... end)
input.on_mouse_down(function(btn, x, y) ... end)
input.on_mouse_up(function(btn, x, y) ... end)
input.on_mouse_wheel(function(dx, dy) ... end)
```

- **Multi-Gamepad Active Pad Switching:** Hotplugging (`SDL_EVENT_GAMEPAD_ADDED` / `REMOVED`) is handled automatically. Pressing a button or moving an axis past `0.25` switches the active polled gamepad to that controller ID (`NoteGamepadActivity`).
- **What Survives Scene Transitions:** `InputSystem::ResetLuaState()` runs on `load_scene` / `reload_scene`. It clears all `input.on_*` Lua callback closures and transient held/pressed/released states so inputs cannot stick across scenes. **Action bindings (`set_bindings`, `bind_key`, `bind_gamepad`) survive across scenes** — install them once at startup, or re-apply idempotently with `input.set_bindings(...)`.

---

## 7. Declarative UI (`.oxml` & `.oss`) Integration

UI elements in `.oxml` and Lua `ui_button` components hook directly into keyboard, gamepad, and action bindings (`UIButtonSystem`):

```xml
<Button text="Confirm"
        action="ui_confirm"
        on-click="on_confirm"
        on-focus="on_item_focus" />

<Button text="Back"
        key="escape"
        gamepad="east"
        on-click="on_back" />
```

- **Shortcuts:** Set `key="escape, p"`, `gamepad="east"` (aliases `gamepad-button` / `controller-button` also work), and/or `action="ui_cancel"` on interactive widgets (`<Button>`, `<Toggle>`, `<RadioButton>`, `<Slider>`) or imperative `ui_button` components.
- **Navigation:** `Tab` / `Shift+Tab`, arrow keys, D-Pad, and the left analog stick (with repeat cadence) move focus between visible focusable elements. `<Slider>` steps left/right with arrows, D-Pad, or left stick. Right analog stick vertical movement scrolls the `<ScrollView>` containing the focused element (or the active `<ScrollView>`).
- **Focus Callbacks (`on-focus`):** Fires on keyboard/gamepad focus moves (`controller:on_focus(item)` inside `each` repeaters, or `controller:on_focus(nil)` outside). Mouse press suppresses `on-focus` so a first click on a locked tile can inspect before selecting on a second click.
- **Auto-Focus:** Connecting a controller (`SDL_EVENT_GAMEPAD_ADDED`) or pressing `south` when nothing is focused automatically focuses the first visible focusable UI element if no `south` shortcut matched.
- **Focus Styling (`.oss`):** `:focus` pseudo-states support `border-color`, `background-color`, `color`, `opacity`, and `border-width`. If `:focus` sets `border-color` without an explicit `border-width`, a `2px` minimum border floor is applied automatically for controller readability.

See [`ui-framework.md`](ui-framework.md) and [`lua-scripting.md`](lua-scripting.md) (or [`dev-tools.md`](dev-tools.md) for `<assets>/types/octarine_api.lua`) for widget attributes and generated EmmyLua stubs.
