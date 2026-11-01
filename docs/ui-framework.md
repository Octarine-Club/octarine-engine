# Declarative UI Framework (`.oxml` & `.oss`)

Octarine's declarative UI framework pairs **Octarine XML (`.oxml`)** for structure, bindings, and templates with **Octarine Style Sheets (`.oss`)** for theme variables, flexbox/percentage layout, and interactive pseudo-states (`:hover`, `:active`, `:focus`, `:disabled`, `:checked`).

Every `.oxml` view compiles directly into standard Octarine ECS entities and data-oriented components (`UIRectComponent`, `UILayoutBoxComponent`, `UIFlexComponent`, `UIStyleStateComponent`, `UIClipComponent`, `UIFocusComponent`, `UIScrollComponent`). There is no secondary widget hierarchy or retained DOM outside the ECS: your UI lives in the same `Registry`, sorts in the same `RenderQueue`, and can be inspected via `StateInspector`.

> **Looking for low-level procedural layout?** If you prefer building UI entities manually in Lua with `ui.canvas` and `ui.anchor`, see [`docs/ui-layout.md`](ui-layout.md).

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Architecture: One Unified Spine, Three Workflows](#2-architecture-one-unified-spine-three-workflows)
   - [Paradigm A: Imperative Code-Behind (`ref` + `controller`)](#paradigm-a-imperative-code-behind-ref--controller)
   - [Paradigm B: Data Context / Snapshot (`view:set_context`)](#paradigm-b-data-context--snapshot-viewset_context)
   - [Paradigm C: Reactive MVVM (`ui.observable` + `bind-value`)](#paradigm-c-reactive-mvvm-uiobservable--bind-value)
3. [`.oxml` Markup Reference](#3-oxml-markup-reference)
   - [Built-In Primitives](#built-in-primitives)
   - [Flex-Wrap & Grid Layout (`<Grid>`, `wrap`, `cross-gap`)](#flex-wrap--grid-layout-grid-wrap-cross-gap)
   - [Bindings, Conditionals (`if`), and Repeaters (`each`)](#bindings-conditionals-if-and-repeaters-each)
   - [Events, Input Shortcuts & Audio Feedback](#events-input-shortcuts--audio-feedback)
4. [`.oss` Stylesheet Reference](#4-oss-stylesheet-reference)
   - [Selectors, Specificity, and Pseudo-States](#selectors-specificity-and-pseudo-states)
   - [Variables (`--var`)](#variables---var)
   - [Sizing Units (`px`, `%`, `fr`, `auto`) & Box Properties](#sizing-units-px--fr-auto--box-properties)
   - [Visual, Text, Audio & 9-Slice Properties](#visual-text-audio--9-slice-properties)
5. [Custom Reusable Widgets](#5-custom-reusable-widgets)
   - [Declarative `.oxml` Templates (`<Use>`, `<Param>`, Named `<Slot />`)](#declarative-oxml-templates-use-param-named-slot-)
   - [Programmatic Lua Widgets (`ui.register_widget`)](#programmatic-lua-widgets-uiregister_widget)
6. [Focus Navigation, Z-Order Hit Testing & Scrolling](#6-focus-navigation-z-order-hit-testing--scrolling)
7. [UI Debug Wireframe Overlay (`ui.debug_draw`)](#7-ui-debug-wireframe-overlay-uidebug_draw)
8. [Lifecycle, Hot Reload & Asset Baking](#8-lifecycle-hot-reload--asset-baking)
9. [Lua API Reference (`ui.*` & `view` Handle)](#9-lua-api-reference-ui--view-handle)

---

## 1. Quick Start

Create a stylesheet (`assets/ui/hud.oss`), a layout file (`assets/ui/hud.oxml`), and mount the view from any Lua scene or script.

### `assets/ui/hud.oss`
```css
:root {
  --bg-panel: #161020e6;
  --accent: #ffcc00;
  --accent-hover: #ffe066;
  --danger: #e03c3c;
  --text-main: #f4eefc;
}

.hud-card {
  width: 340px;
  padding: 16px;
  gap: 10px;
  background-color: var(--bg-panel);
  border-width: 2px;
  border-color: var(--accent);
  border-radius: 8px;
}

.title {
  font: ui-font;
  color: var(--accent);
  text-align: left;
}

.btn {
  width: 100%;
  height: 40px;
  background-color: #2e2040;
  color: var(--text-main);
  border-width: 1px;
  border-color: var(--accent);
  border-radius: 6px;
  text-align: center;
  text-valign: center;
}

.btn:hover {
  background-color: #463060;
  border-color: var(--accent-hover);
}

.btn:active {
  background-color: #1e142b;
}

.btn:focus {
  border-width: 2px;
  border-color: var(--accent-hover);
}
```

### `assets/ui/hud.oxml`
```xml
<OXML>
  <Style src="assets/ui/hud.oss" />

  <Canvas fixed="true" base-layer="10">
    <Column class="hud-card" margin="20px">
      <Label class="title" ref="hpLabel" text="HP: {hp}/{maxHp}" />
      <ProgressBar ref="hpBar" value="{hp / maxHp}" height="16px" fill-color="#44d060" />

      <Label if="{hp < 30}" text="WARNING: LOW HEALTH!" color="#e03c3c" />

      <Button class="btn" text="Drink Potion [Space]" key="space" on-click="onDrinkPotion" />
    </Column>
  </Canvas>
</OXML>
```

### Mounting from Lua (`scenes/arena.lua`)
```lua
local vm = ui.observable({
    hp = 80,
    maxHp = 100,
})

local controller = {
    onDrinkPotion = function(self, entity)
        vm.hp = math.min(vm.maxHp, vm.hp + 25)
    end,
}

local hud_view = ui.mount("assets/ui/hud.oxml", {
    context = vm,
    controller = controller,
})
```

---

## 2. Architecture: One Unified Spine, Three Workflows

Different game UI screens have different complexity needs. A static pause menu shouldn't require a full ViewModel hierarchy, and a complex RPG inventory shouldn't be wired with dozens of manual `get_text_label` calls.

Octarine UI uses a single runtime spine (`context` + `ref` + `controller`) that lets you choose—or mix—three workflows within the same project or even the same `.oxml` file:

```
                   ┌──────────────────────────────────────┐
                   │        ui.mount(oxml, opts)          │
                   └──────────────────┬───────────────────┘
                                      │
        ┌─────────────────────────────┼─────────────────────────────┐
        ▼                             ▼                             ▼
┌───────────────────┐       ┌───────────────────┐       ┌───────────────────────┐
│  1. Code-Behind   │       │  2. Data Context  │       │   3. Reactive MVVM    │
│  ref="playBtn"    │       │  context = table  │       │ context = observable  │
│  on-click="fn"    │       │  text="{hp}"      │       │ bind-value="volume"   │
│  view.refs.playBtn│       │  view:refresh()   │       │ vm.hp = 50 (auto-sync)│
└───────────────────┘       └───────────────────┘       └───────────────────────┘
```

### Paradigm A: Imperative Code-Behind (`ref` + `controller`)
**Best for:** Pause menus, confirmation dialogs, title screens, and simple overlays.

Name elements with `ref="id"` in `.oxml` and pass action callbacks via `controller`. Access named elements directly on `view.refs.<id>`:

```lua
local pause_view = ui.mount("assets/ui/pause_menu.oxml", {
    controller = {
        onResume = function(self, entity)
            pause_view.refs.statusLabel:set_text("Resuming...")
            pause_view:unmount()
        end,
        onQuit = function(self, entity)
            load_scene("scenes/main_menu.lua")
        end,
    },
})

-- Imperative updates at any time:
pause_view.refs.statusLabel:set_text("Game Paused")
pause_view.refs.quitBtn:set_disabled(false)
```

### Paradigm B: Data Context / Snapshot (`view:set_context`)
**Best for:** End-of-run summary screens, item inspection cards, and scoreboards where data updates at discrete moments.

Pass a plain Lua table as `context`. Any attribute containing `{...}` is evaluated against the `context` table on mount and whenever you call `view:set_context(new_tbl)` or `view:refresh()`:

```lua
local summary_view = ui.mount("assets/ui/run_summary.oxml", {
    context = {
        wave = 12,
        gold = 450,
        victory = true,
        loots = { "Iron Helm", "Ruby Ring", "Scroll of Haste" },
    },
})

-- Later, replace or refresh the snapshot:
summary_view:set_context({
    wave = 13,
    gold = 620,
    victory = true,
    loots = { "Dragon Scale" },
})
```

### Paradigm C: Reactive MVVM (`ui.observable` + `bind-value`)
**Best for:** Live HUDs, settings menus, character sheets, and reactive lists.

Wrap a Lua table with `ui.observable(tbl)`. The returned proxy tracks property reads and writes via Lua `__index` / `__newindex` metamethods:
- **One-way expressions (`{expr}`)**: Mutating `vm.hp = 40` immediately re-evaluates only the bindings that depend on `hp`.
- **Two-way input bindings (`bind-value="path"`)**: Interacting with a `<Toggle>`, `<RadioGroup>`, or `<Slider>` automatically writes the new value back into `vm[path]` and fires `on-change` if bound.

```lua
local settings_vm = ui.observable({
    masterVolume = 0.8,
    fullscreen = true,
    difficulty = "normal",
})

local settings_view = ui.mount("assets/ui/settings.oxml", {
    context = settings_vm,
    controller = {
        onDifficultyChanged = function(self, newDiff, entity)
            log.info("Difficulty changed to: " .. tostring(newDiff))
        end,
    },
})

-- Mutating the observable updates the UI automatically:
settings_vm.masterVolume = 0.5
```

---

## 3. `.oxml` Markup Reference

An `.oxml` document starts with a root `<OXML>` element containing optional `<Style src="..." />` stylesheets, optional `<Use name="..." src="..." />` widget imports, and a layout tree rooted at `<Canvas>` (or a reusable widget root).

### Built-In Primitives

Unlike manual multi-entity setups that require separate background and text entities, Octarine's built-in primitives use **single-entity composite widgets** wherever possible (`UIRectComponent` + `SquarePrimitiveComponent` + `TextLabelComponent` + `UIButtonComponent` on the same ECS entity).

| Tag | Description | Key Attributes |
|---|---|---|
| `<Canvas>` | Root coordinate space (`UICanvasComponent`). | `fixed="true\|false"`, `width`, `height`, `base-layer="0"` |
| `<Panel>` | Styled container (`UIFlexComponent` + background/border/9-slice). Defaults to column direction. Supports flex-wrap. | `direction="row\|column"`, `wrap="true\|false"`, `gap`, `cross-gap`, `padding`, `slice="l,t,r,b"` |
| `<Row>` | Horizontal flex container (`direction="row"`). Supports flex-wrap. | `gap`, `cross-gap`, `wrap="true\|false"`, `padding`, `justify`, `align` |
| `<Column>` | Vertical flex container (`direction="column"`). Supports flex-wrap. | `gap`, `cross-gap`, `wrap="true\|false"`, `padding`, `justify`, `align` |
| `<Grid>` | Fixed-column grid container (`direction="row"`, `wrap="true"`). Partitions children into $N$ equal-width columns. | `columns="N"`, `gap`, `cross-gap`, `padding`, `justify`, `align` |
| `<Box>` | Non-flex container or spacer with explicit sizing. | `width`, `height`, `anchor` |
| `<Label>` | Text display (`TextLabelComponent`). Supports `{...}` interpolation and multi-line word wrapping (`wrap="true"`). | `text`, `font`, `color`, `text-align`, `wrap="true\|false\|<px>"`, `max-width` |
| `<Image>` | Texture / atlas region (`SpriteComponent`). Supports 9-slice borders. | `src="asset_id"`, `slice="l,t,r,b"` (or `"all"`), `color` |
| `<Button>` | Interactive button (`UIButtonComponent` + background + label + focus/style state). | `text`, `on-click`, `key`, `gamepad`, `action`, `sound-hover`, `sound-click`, `sound-focus`, `disabled`, `focusable` |
| `<ProgressBar>` | Fill bar (`SquarePrimitiveComponent.fillRatio`). | `value="{0..1}"`, `fill-color`, `background-color` |
| `<Toggle>` | Boolean checkbox / switch with `:checked` state and two-way binding. | `checked="{bool}"`, `bind-value="field"`, `on-change="fn"`, `sound-hover`, `sound-click`, `text` |
| `<RadioGroup>` | Mutual-exclusion container for `<RadioButton>` children. | `value="{val}"`, `bind-value="field"`, `on-change="fn"`, `direction="row\|column"` |
| `<RadioButton>` | Selectable option inside a `<RadioGroup>`. | `value="option_id"`, `text`, `sound-hover`, `sound-click`, `disabled` |
| `<Slider>` | Numeric range control (`0..1` normalized fill + keyboard/click step adjustment). | `min="0"`, `max="100"`, `step="5"`, `value="{num}"`, `bind-value="field"`, `on-change="fn"`, `sound-focus` |
| `<ScrollView>` | Clipped scroll container (`UIScrollComponent` + `UIClipComponent`). Responds to mouse wheel. | `scroll-x="true\|false"`, `scroll-y="true\|false"`, `wheel-step="32"` |

### Flex-Wrap & Grid Layout (`<Grid>`, `wrap`, `cross-gap`)

Octarine supports multi-line flex wrapping and fixed-column grids directly in `.oxml` and `.oss`:

#### 1. `<Grid columns="N">` Container
`<Grid>` is a specialized row flex container (`direction="row"`, `wrap="true"`) that partitions available width evenly across `columns` slots:
- **Equal Column Sizing**: Each column cell is automatically assigned width:
  $$\text{cellWidth} = \frac{\text{availWidth} - \text{gap} \times (\text{columns} - 1)}{\text{columns}}$$
- **Automatic Wrapping & Row Advancement**: After every $N$ items, the layout wraps to a new row, advancing by the tallest item in the previous row plus `cross-gap`.
- **Intrinsic Auto Height (`height="auto"`)**: Wrapped containers and grids compute their total height dynamically based on the number of wrapped lines and cross gaps.

```xml
<Grid ref="invGrid" columns="4" gap="10px" cross-gap="8px" width="400px" height="auto">
  <Panel each="item in {inventory}" height="64px" class="slot-card">
    <Image src="{item.icon}" width="32px" height="32px" />
    <Label text="{item.name}" font="ui-font" />
  </Panel>
</Grid>
```

#### 2. Flex Wrapping on `<Row>` & `<Column>` (`wrap="true"`)
Any flex container can wrap its children by setting `wrap="true"` (or `flex-wrap: wrap;` in `.oss`):
- Items fit on the current line as long as `lineMain + gap + itemMain <= availMain`.
- When an item overflows, it wraps to a new line spaced by `cross-gap` (which falls back to `gap` if omitted).
- Cross-axis lines support `cross-gap` (aliases `row-gap` or `column-gap` in `.oss`).

```xml
<Row wrap="true" gap="6px" cross-gap="4px" width="240px" height="auto">
  <Box width="70px" height="24px" background-color="#302040" />
  <Box width="70px" height="24px" background-color="#302040" />
  <Box width="70px" height="24px" background-color="#302040" />
  <Box width="70px" height="24px" background-color="#302040" />
</Row>
```

### Bindings, Conditionals (`if`), and Repeaters (`each`)

#### One-Way Expressions (`{...}`)
Any attribute value containing `{...}` is evaluated against the active `context` (plus any loop variables from enclosing `each` repeaters):
- **Pure expression** (`value="{hp / maxHp}"`, `disabled="{gold < cost}"`): Evaluates to its native Lua type (number, boolean, string).
- **String interpolation** (`text="Wave {wave} — {enemies} left"`): Interpolates multiple `{...}` segments into a string.

#### Two-Way Bindings (`bind-value="field"`)
Available on `<Toggle>`, `<RadioGroup>`, and `<Slider>`:
```xml
<Toggle text="Mute Audio" bind-value="isMuted" on-change="onAudioToggle" />

<RadioGroup bind-value="difficulty" on-change="onDiffChange" direction="row" gap="8px">
  <RadioButton value="easy" text="Easy" width="90px" height="32px" />
  <RadioButton value="normal" text="Normal" width="90px" height="32px" />
  <RadioButton value="hard" text="Hard" width="90px" height="32px" />
</RadioGroup>

<Slider min="0" max="100" step="5" bind-value="sfxVolume" height="20px" />
```

#### Conditional Visibility (`if="{expr}"`)
Attaching `if="{expr}"` to any element toggles its `UIRectComponent.visible` state and skips layout/rendering for its entire subtree when falsy:
```xml
<Panel if="{isBossWave}" class="boss-alert">
  <Label text="BOSS APPROACHING" />
</Panel>
```

#### List Repeaters (`each="item in {list}"`)
Attaching `each="item in {expr}"` (or `each="item, index in {expr}"`) repeats the element for each entry in the Lua array table evaluated from `{expr}` on the container's children:
```xml
<ScrollView height="240px">
  <Column ref="inventoryList" width="100%" gap="6px">
    <Row each="slot, idx in {inventory}" height="36px" padding="6px" background-color="#221830">
      <Label text="#{idx}: {slot.name}" width="70%" />
      <Label text="x{slot.count}" width="30%" text-align="right" />
    </Row>
  </Column>
</ScrollView>
```

### Events, Input Shortcuts & Audio Feedback

Interactive widgets dispatch events to functions on the `controller` table passed to `ui.mount(src, { controller = ... })`:

| Attribute | Fired By | Controller Signature |
|---|---|---|
| `on-click="fn"` | `<Button>`, `<Toggle>`, `<RadioButton>` | `function(controller, entity)` |
| `on-change="fn"` | `<Toggle>`, `<RadioGroup>`, `<Slider>` | `function(controller, newValue, entity)` |

Every interactive element also supports direct keyboard, gamepad, and input-action shortcuts without extra Lua boilerplate:
- `key="return, space"` — Comma-separated keyboard key names.
- `gamepad="a, south"` — Comma-separated controller button names.
- `action="ui_confirm"` — Input action name from your project configuration.

#### Declarative Audio Feedback Hooks (`sound-*`)
Interactive elements emit `AudioPlayEvent` sounds through the engine's `EventBus` without writing manual audio callbacks in Lua:
- `sound-hover="clip_id"` — Played when the pointer enters the element's bounding rect (automatically debounced so moving within the element does not re-fire).
- `sound-click="clip_id"` (alias `sound-press`) — Played when the button or control is activated via mouse click, keyboard shortcut (`key`), gamepad shortcut (`gamepad`), or mapped `action`.
- `sound-focus="clip_id"` — Played when the element gains keyboard/gamepad focus (via `Tab`, directional arrows/D-Pad, or click).

```xml
<Button text="Launch Mission"
        sound-hover="sfx_hover"
        sound-click="sfx_confirm"
        sound-focus="sfx_focus"
        key="return, space"
        on-click="onLaunch" />
```

> [!NOTE]
> Audio IDs referenced in `sound-*` attributes are statically validated during headless bake (`-m bake`). Unknown clip IDs produce CI build failures before broken assets can reach production.

---

## 4. `.oss` Stylesheet Reference

`.oss` files use familiar CSS-like syntax tailored for Octarine's 2D layout and primitive renderer. Every `.oss` property can also be written directly as an inline attribute on any `.oxml` tag (e.g., `<Panel width="50%" padding="12px">`).

### Selectors, Specificity, and Pseudo-States

- **Tag selector**: `Button { ... }`, `Label { ... }`
- **Class selector**: `.primary-btn { ... }` (matched against `class="btn primary-btn"`)
- **ID selector**: `#confirmBtn { ... }` (matched against `id="confirmBtn"` or `ref="confirmBtn"`)
- **Cascade order**: Inline `.oxml` attributes override `#id`, which overrides `.class`, which overrides `Tag` rules.

#### Supported Pseudo-States
Pseudo-states are evaluated every frame by `UIButtonSystem` in priority order (`:disabled` > `:active` > `:hover` > `:focus` > `:checked` > normal). Pseudo-states can also attach sound cues (`sound: "clip_id"`):
```css
.slot-btn {
  background-color: #261c35;
  border-width: 1px;
  border-color: #554070;
  sound-hover: "sfx_hover";
  sound-click: "sfx_click";
}
.slot-btn:hover    { background-color: #38284e; border-color: #b088ff; sound: "sfx_hover"; }
.slot-btn:active   { background-color: #1b1226; sound: "sfx_click"; }
.slot-btn:focus    { border-width: 2px; border-color: #ffcc00; sound: "sfx_focus"; }
.slot-btn:checked  { background-color: #4a3268; border-color: #ffcc00; }
.slot-btn:disabled { background-color: #18141d; color: #666070; }
```

### Variables (`--var`)

Define reusable palette colors and metrics in `:root` and reference them anywhere with `var(--name)`:
```css
:root {
  --brand-purple: #9370db;
  --brand-gold: #ffd700;
  --card-radius: 10px;
}

.card {
  border-color: var(--brand-purple);
  border-radius: var(--card-radius);
}
```

### Sizing Units (`px`, `%`, `fr`, `auto`) & Box Properties

Octarine supports four sizing units on `width` and `height` (`UILayoutBoxComponent`):

| Unit | Example | Behavior |
|---|---|---|
| **`px`** | `width: 240px;` | Fixed pixel dimension (`UIUnit::Px`). |
| **`%`** | `width: 50%;` | Percentage of the parent container's inner content area (`UIUnit::Percent`), automatically scaling across window resolutions. |
| **`fr`** | `width: 1fr;` | Proportional share of remaining space along a flex container's main axis (`UIUnit::Fr`). Also configurable via `flex-grow: 1;`. |
| **`auto`** | `width: auto;` | Intrinsic content measurement (`UIUnit::Auto`): measures text bounds (`TextLabelComponent`), sprite dimensions (`SpriteComponent`), or child extents plus padding. |

#### Box & Flex Layout Properties
| Property | Values / Examples | Description |
|---|---|---|
| `width`, `height` | `200px`, `75%`, `1fr`, `auto` | Primary box dimensions. |
| `min-width`, `min-height` | `120px`, `20%` | Lower bound clamp on resolved size (`0` = unconstrained). |
| `max-width`, `max-height` | `600px`, `80%` | Upper bound clamp on resolved size (`0` = unconstrained). |
| `padding` | `12px`, `8px 16px`, `4px 8px 4px 8px` | Inner inset (`padding-left`, `padding-top`, `padding-right`, `padding-bottom`). |
| `margin` | `10px`, `4px 12px`, `4px 8px 4px 8px` | Outer spacing (`margin-left`, `margin-top`, `margin-right`, `margin-bottom`). |
| `flex-direction` (or `direction`) | `row`, `column` | Main axis for `<Panel>`, `<Row>`, `<Column>`, `<Grid>`, `<RadioGroup>`. |
| `gap` (or `spacing`) | `8px` | Spacing in pixels between visible flex children along the main axis. |
| `cross-gap` (or `row-gap`, `column-gap`) | `8px` | Spacing in pixels between wrapped lines or rows/columns. Defaults to `gap`. |
| `flex-wrap` (or `wrap`) | `wrap`, `nowrap` (or `true`, `false`) | Wraps flex children onto additional lines when exceeding container bounds. |
| `columns` (or `grid-columns`) | `1`, `2`, `4` | Fixed number of grid columns. Partitions width into equal slots and enables wrapping. |
| `justify-content` (or `justify`) | `start`, `center`, `end`, `space-between`, `space-around` | Main-axis distribution when flex children do not consume all space. |
| `align-items` (or `align`) | `stretch`, `start`, `center`, `end` | Cross-axis alignment of flex children. |
| `flex-grow` | `0`, `1`, `2` | Flex expansion weight along the parent's main axis. |
| `overflow` | `visible`, `hidden`, `clip` | Enables hardware scissor clipping (`UIClipComponent`) for the element and all descendants. |
| `z-index` | `1`, `10` | Relative render layer offset (`UIZIndexComponent`), accumulated down the hierarchy. |

### Visual, Text, Audio & 9-Slice Properties

| Property | Values / Examples | Description |
|---|---|---|
| `background-color` (or `bg-color`) | `#1a1025`, `#1a1025cc`, `rgba(26, 16, 37, 200)` | Panel/button fill color (`SquarePrimitiveComponent.color`). |
| `fill-color` | `#44d060` | Foreground bar color for `<ProgressBar>` and `<Slider>`. |
| `color` (or `text-color`) | `#ffffff`, `#ffcc00` | Text color (`TextLabelComponent`) or sprite tint (`SpriteComponent`). |
| `border-width` (or `border-thickness`) | `2px` | Border stroke thickness in pixels. |
| `border-color` | `#ffcc00` | Border stroke color. |
| `border-radius` (or `corner-radius`) | `6px` | Rounded corner radius in pixels. |
| `font` (or `font-id`) | `ui-font` | Font asset ID registered in the asset catalog. |
| `text-align` | `left`, `center`, `right` | Horizontal text alignment inside the element's resolved rect. |
| `text-valign` | `top`, `center`, `bottom` | Vertical text alignment inside the element's resolved rect. |
| `text-wrap` | `true`, `false` | Word-wraps text to the element's resolved pixel width. |
| `shadow-color` | `#000000aa` | Text drop-shadow color. |
| `shadow-offset` | `2px 2px` | Text drop-shadow `(x, y)` offset in pixels (`shadow-offset-x`, `shadow-offset-y`). |
| `sound-hover` | `sfx_hover` | Audio clip ID played on pointer hover enter. |
| `sound-click` (or `sound-press`) | `sfx_confirm` | Audio clip ID played on button activation / click. |
| `sound-focus` | `sfx_focus` | Audio clip ID played on gaining keyboard/gamepad focus. |
| `slice` | `8` or `8, 8, 8, 8` | 9-slice border insets `(left, top, right, bottom)` in source texels (`UINineSliceComponent`). |
| `opacity` | `0.0` .. `1.0` | Multiplies the alpha channel of backgrounds, borders, and text. |
| `visible` | `true`, `false` | Initial visibility (`UIRectComponent.visible`). |

---

## 5. Custom Reusable Widgets

Projects can package their own custom primitives and share them across screens or repositories in two ways.

### Declarative `.oxml` Templates (`<Use>`, `<Param>`, Named `<Slot />`)

Define reusable UI components in dedicated `.oxml` files using `<Param>` declarations and `<Slot />` projection points:

#### `assets/ui/widgets/item_slot_card.oxml`
```xml
<OXML>
  <Param name="frame_title" default="Slot" />
  <Param name="rarity" default="common" />

  <Column class="card {rarity}" width="100%" height="auto" padding="8px" gap="4px">
    <!-- Named slot with default fallback content -->
    <Slot name="header">
      <Label text="{frame_title}" font="ui-font" color="#ffcc00" height="18px" />
    </Slot>

    <!-- Default unnamed slot for body content -->
    <Slot />
  </Column>
</OXML>
```

Import the widget with `<Use name="ItemSlotCard" src="assets/ui/widgets/item_slot_card.oxml" />`. Callers can override named slots, inject body content, and project inside `each` repeaters:

```xml
<OXML>
  <Use name="ItemSlotCard" src="assets/ui/widgets/item_slot_card.oxml" />

  <Canvas width="800px" height="600px">
    <Grid columns="3" gap="10px" cross-gap="8px" width="360px">
      <!-- Projected children evaluate expressions in the caller's repeater scope -->
      <ItemSlotCard each="item, idx in {inventory}" rarity="{item.rarity}">
        <!-- Targeted slot override -->
        <Label slot="header" text="Item #{idx}: {item.title}" color="#ffd700" />
        <!-- Default slot content -->
        <Label text="{item.description}" font="ui-font" />
      </ItemSlotCard>

      <!-- Using fallback header without slot="header" -->
      <ItemSlotCard frame_title="Empty Slot">
        <Label text="No item equipped" color="#666070" />
      </ItemSlotCard>
    </Grid>
  </Canvas>
</OXML>
```

#### Slot Features & Invariants
- **Default Slot (`<Slot />`)**: Receives all call-site children that do not specify a `slot` attribute.
- **Named Slots (`<Slot name="slotName">`)**: Matches call-site children specifying `slot="slotName"`.
- **Fallback Content**: If a caller does not provide children targeting a slot, any inner children authored inside `<Slot name="...">Fallback</Slot>` are instantiated instead.
- **Scope Preservation in `each` Repeaters**: Projected slot children are evaluated against the caller's outer scope and loop iteration variables (`{item.*}`, `{idx}`), not the template's internal scope.

### Programmatic Lua Widgets (`ui.register_widget`)

For reusable widgets with custom procedural logic or animations, register a widget factory in Lua with `ui.register_widget(name, descriptor)`:

```lua
ui.register_widget("BadgeCounter", {
    defaults = {
        title = "Items",
        count = 0,
    },
    render = function(props)
        return string.format([[
            <Row width="160px" height="32px" padding="6px" background-color="#2a1e3f" border-radius="6px">
                <Label text="%s: %s" width="100%%" text-align="center" text-valign="center" />
            </Row>
        ]], tostring(props.title), tostring(props.count))
    end,
    on_mount = function(entity, props, view)
        -- Called once when the widget entity is created
    end,
    on_prop_changed = function(entity, prop_name, new_value, view)
        -- Called whenever a bound '{...}' attribute on <BadgeCounter> updates
    end,
    on_unmount = function(entity)
        -- Called when the owning view is unmounted
    end,
})
```

Once registered, `<BadgeCounter title="Shards" count="{shards}" />` can be used as a first-class tag in any `.oxml` file.

---

## 6. Focus Navigation, Z-Order Hit Testing & Scrolling

### Topmost Z-Order Hit Stopping & Clip Culling
`UIButtonSystem` evaluates interactive elements in **topmost `(layer, EntityID)` order**:
1. **Modal & Overlay Blocking**: Clicking an overlapping button on a higher `layer` (or later in the hierarchy at the same layer) consumes the pointer hit so buttons underneath do not fire simultaneously.
2. **Clipped Hit Testing**: Buttons inside a `<ScrollView>` or `overflow: hidden` container (`UIClipComponent`) only accept pointer hits inside the container's active scissor rectangle—scrolled-out items cannot be clicked outside the viewport.

### Keyboard & Gamepad Focus Navigation
Interactive widgets (`<Button>`, `<Toggle>`, `<RadioButton>`, `<Slider>`, or any element with `focusable="true"`) participate in automatic focus navigation:
- **`Tab` / `Shift+Tab`**: Cycles focus through visible, active focusable elements ordered by `(tabIndex, EntityID)`.
- **Arrow Keys / Gamepad D-Pad (`Up`, `Down`, `Left`, `Right`)**: Moves focus to the nearest visible focusable candidate in the chosen cardinal direction using spatial cone projection, or follows explicit overrides (`focus-up`, `focus-down`, `focus-left`, `focus-right`).
- **`Enter` / `Space` / Gamepad `South` (`A`)**: Activates the currently focused widget.

### Scroll Containers (`<ScrollView>`)
A `<ScrollView>` attaches both `UIScrollComponent` and `UIClipComponent`. Moving the mouse wheel while hovering anywhere inside the `<ScrollView>` viewport scrolls the content and clamps `scrollX` / `scrollY` to `[0, max(0, contentSize - viewportSize)]`.

---

## 7. UI Debug Wireframe Overlay (`ui.debug_draw`)

For layout debugging, alignment tuning, and verifying hierarchy bounding boxes at runtime, Octarine provides a built-in wireframe debug overlay driven by `RenderUIPrimitiveSystem` and `UIDebugConfig`:

```lua
-- Toggle overlay on/off
local is_active = ui.debug_draw()

-- Explicitly enable or disable
ui.debug_draw(true)
ui.debug_draw(false)
```

When enabled, wireframe bounding boxes are rendered for all visible `UIRectComponent` entities with positive dimensions at a high layer offset (`rect.layer + 1000`) so they sit cleanly on top of existing UI:

| Color | Thickness | Entity Archetype / Component |
|---|---|---|
| **Gold / Yellow** (`#ffcc00`) | `2.0px` | Currently focused element (`UIFocusComponent.isFocused == true`). |
| **Green** (`#44d060`) | `1.0px` | Interactive clickable button (`UIButtonComponent`). |
| **Magenta** (`#ff40ff`) | `1.0px` | Scissor clip container (`UIClipComponent`). |
| **Cyan** (`#00e5ff`) | `1.0px` | Flex or Grid container (`UIFlexComponent`). |
| **Lavender / Purple** (`#8870a0`) | `1.0px` | Other UI elements (`UIRectComponent` labels, images, non-flex containers). |

---

## 8. Lifecycle, Hot Reload & Asset Baking

### Construction & Teardown
- **`ui.mount(path_or_xml, opts?)`** spawns the ECS entity tree for the view and returns a `view` handle.
- **`view:unmount()`** invokes `on_unmount` hooks for any custom widgets, despawns the root entity and all descendants, and releases Lua references cleanly.
- **Scene Transitions**: When a scene unloads, all UI views mounted during that scene are automatically unmounted and cleaned up.

### Live Hot Reload
When running a development build (`player-debug` or `player-release`), `FrameLoop` polls mounted `.oxml` and `.oss` files for filesystem changes:
- Saving a `.oss` or `.oxml` file on disk automatically re-parses and rebuilds the mounted view in place while **preserving its live `context` and `controller` tables**.

### Headless Bake Validation (`-m bake`)
`.oxml` (`AssetType::UIView`) and `.oss` (`AssetType::UIStyle`) files under `assets/` are discovered by `AssetCatalog` and validated during `./OctarineEngine <game_dir> -m bake`:
- **Syntax & Schema**: Malformed XML tags, missing closing tags, invalid `.oss` properties.
- **File Inclusions**: Missing `<Style src="..." />` stylesheets or `<Use src="..." />` widget template files.
- **Asset Integrity**: Broken `<Image src="..." />` textures, invalid `font` asset IDs, and missing `sound-hover`, `sound-click`, `sound-press`, or `sound-focus` audio clip references fail the bake gate in CI before broken UI can ship.

---

## 9. Lua API Reference (`ui.*` & `view` Handle)

### High-Level Declarative API

| Function | Description |
|---|---|
| `ui.mount(source, opts?) -> view` | Mounts an `.oxml` asset path (e.g. `"assets/ui/hud.oxml"`) or inline `<OXML>...</OXML>` string. `opts`: `{ context = tbl, controller = tbl, parent = entity }`. |
| `ui.observable(tbl) -> proxy` | Wraps a Lua table in a reactive observable proxy that notifies mounted views when keys are mutated. |
| `ui.register_widget(name, descriptor)` | Registers a custom tag usable in `.oxml`. `descriptor`: `{ defaults, render, on_mount, on_prop_changed, on_unmount }`. |
| `ui.debug_draw(enabled?) -> bool` | Toggles (when called with no arguments) or explicitly sets (`true`/`false`) the UI wireframe debug overlay. Returns the updated boolean state. |
| `ui.focus(entity)` | Moves keyboard/gamepad UI focus to `entity`. |
| `ui.get_focused() -> entity?` | Returns the currently focused UI `Entity`, or `nil` if none. |

### `view` Handle Methods

| Member / Method | Description |
|---|---|
| `view.root` | Root `Entity` of the mounted view (`<Canvas>` or root widget). |
| `view.refs.<id>` | Ref handle table for any element with `ref="<id>"` or `id="<id>"`. |
| `view:set_context(tbl)` | Replaces the view's data context table and refreshes all `{...}` bindings, `if` directives, and `each` repeaters. |
| `view:get_context() -> tbl` | Returns the active `context` table or observable proxy. |
| `view:refresh()` | Re-evaluates all bindings against the current `context`. |
| `view:find(id) -> ref?` | Looks up a ref handle by ID (`view.refs[id]`). |
| `view:unmount()` | Destroys the view's entity subtree and unregisters all subscriptions. |

### `view.refs.<id>` Element Handle Methods

Every element with `ref="id"` exposes both its raw ECS `Entity` (`ref.entity`) and ergonomic helper methods:

| Method | Description |
|---|---|
| `ref.entity` | The underlying 64-bit ECS `Entity` handle (usable with `registry.*`). |
| `ref:set_text(str)` / `ref:get_text()` | Reads or updates the element's `TextLabelComponent.text`. |
| `ref:set_visible(bool)` / `ref:is_visible()` | Shows or hides the element and its subtree (`UIRectComponent.visible`). |
| `ref:set_disabled(bool)` / `ref:is_disabled()` | Enables or disables interaction and applies the `:disabled` style state. |
| `ref:set_checked(bool)` / `ref:is_checked()` | Sets or reads the checked state on a `<Toggle>` or `<RadioButton>`. |
| `ref:set_value(num)` / `ref:get_value()` | Sets or reads the normalized fill/value on a `<ProgressBar>` or `<Slider>`. |
| `ref:focus()` | Assigns keyboard/gamepad focus to this element. |

### Low-Level Procedural Helpers (`ui.*`)

All declarative primitives map to low-level helpers that remain available for procedural Lua scripts:
- `ui.canvas(entity, opts?)` — Attaches `UICanvasComponent`.
- `ui.anchor(entity, preset_or_table, opts?)` — Attaches `UIAnchorComponent`.
- `ui.flex(entity, opts?)` — Attaches `UIFlexComponent` (`direction`, `gap`, `cross_gap`, `wrap`, `columns`, `padding`, `padding_left`, `padding_top`, `padding_right`, `padding_bottom`, `justify`, `align`).
- `ui.box(entity, opts?)` — Attaches `UILayoutBoxComponent` (`width`, `height`, `min_width`, `max_width`, `margin`, `flex_grow`).
- `ui.clip(entity, enabled_or_table?)` — Attaches `UIClipComponent` hardware scissor clipping.
- `ui.nine_slice(entity, slice_or_table)` — Attaches `UINineSliceComponent` 9-patch border slicing.
- `ui.scroll(entity, opts?)` — Attaches `UIScrollComponent` + `UIClipComponent`.
- `ui.z_index(entity, z)` — Attaches `UIZIndexComponent`.
- `ui.debug_draw(enabled?)` — Toggles or sets wireframe overlay visualization.
