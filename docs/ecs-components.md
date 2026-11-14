# ECS Component Reference

This document lists all components available to Lua in Octarine Engine, including their authoring schemas and runtime usertype fields.

Components are added inside the `components` table of an entity definition. All 39 registered components in `components.json` also expose `registry.has_<key>(entity)` and `registry.get_<key>(entity)` accessors at runtime (`transform` and `entity_mask` are `LuaEntityLoader` spawn shorthands rather than standalone registered components, alongside top-level entity keys `name`, `tag`/`tags`, `group`/`groups`, and `entities`).

---

## Core Components

### `name`

Stores a human-readable entity identifier (`NameComponent`). Can also be set via top-level `name = "..."` on an entity table.

```lua
components = { name = "Player" }               -- bare string shorthand
components = { name = { value = "Player" } }   -- or { name = "Player" }
```

Runtime (`registry.get_name(entity)`): `name` (`string`, read/write).

### `transform` (spawn shorthand)

Spawn-time shorthand expanded by `LuaEntityLoader` into `position`, `scale`, `rotation`, and `pivot` components.

| Field      | Type                      | Default      | Description                                              |
|------------|---------------------------|--------------|----------------------------------------------------------|
| `position` | `table {x, y}`            | `{x=0, y=0}` | The local position (world position if top-level).        |
| `scale`    | `table {x, y}` or `number`| `{x=1, y=1}` | The scale of the entity, applied about its anchor. A bare number scales both axes. |
| `rotation` | `number`                  | `0.0`        | Rotation, in the project's `AngleUnit` (degrees by default). |
| `pivot`    | `table {x, y}`            | *(centre)*   | Anchor for rotation *and* scale. Builds a `pivot` component. |

```lua
components = { transform = { scale = 2 } }                  -- uniform: both axes at once
components = { transform = { scale = { x = 2, y = 5 } } }   -- per-axis
```

### `position`

Standalone 2D position component (`{ value = { x, y } }`). Also populated by `transform.position`.

Runtime (`registry.get_position(entity)`): `value` (`vec2`), `x` (`number`), `y` (`number`).

### `rotation`

Standalone orientation component (`number` or `{ value = number }`), authored in the project's configured `AngleUnit` and converted to radians internally.

Runtime (`registry.get_rotation(entity)`): `value` (`number`, read/write in the configured `AngleUnit`).

### `scale`

Standalone 2D scale factor (`number`, `{ x, y }`, or `{ value = { x, y } }`).

Runtime (`registry.get_scale(entity)`): `value` (`vec2`), `x` (`number`), `y` (`number`), `scale` (`number`, writes both axes), `is_uniform()` (`boolean` method).

### `pivot`

The anchor that rotation and scale both act about. It is its own component, independent of
`rotation` in both directions: an entity can rotate with no pivot, and can carry a pivot with no
rotation purely to anchor its scaling.

| Field  | Type           | Default          | Description                                        |
|--------|----------------|------------------|----------------------------------------------------|
| `x`    | `number`       | `0.5`            | Horizontal anchor, normalized to the entity's bounds. |
| `y`    | `number`       | `0.5`            | Vertical anchor, normalized to the entity's bounds.   |

Unlike `position` and `scale`, the standalone component is authored **flat** — `x` and `y` at the
top level, the same shape it has inside the transform table. The nested `{ value = { x, y } }`
spelling those two components use is accepted as well, so either reads correctly:

```lua
components = { transform = { pivot = { x = 0, y = 0 } } }   -- inline in the transform
components = { pivot = { x = 0, y = 0 } }                   -- as its own component
components = { pivot = 0 }                                  -- shorthand: both axes at once
components = { pivot = { value = { x = 0, y = 0 } } }       -- nested, matching scale/position
```

At runtime the component exposes `value` (a `vec2`) plus `x` and `y` accessors:

```lua
local p = registry.get_pivot(entity)   -- registry.has_pivot(entity) to test first
p.x = 0                                -- writes through to the component
```

`pivot` is normalized against whatever the entity draws at: `{x=0, y=0}` is the top-left corner,
`{x=0.5, y=0.5}` the centre, `{x=1, y=1}` the bottom-right. Values outside `0..1` are allowed and
place the anchor outside the bounds, which is a convenient way to make something orbit a point.

The size the anchor resolves against comes from the entity's `sprite`, else its `square`, else its
`box_collider`. An entity with none of those has nothing to anchor to and falls back to `position`.

**Omitting the component anchors to the centre of the entity's geometry** — the same point
`{x=0.5, y=0.5}` resolves to. So you only need a `pivot` to move the anchor somewhere other than
the middle, and rotation behaves identically with or without one.

The anchor is the single point everything agrees on: the sprite and primitive renderers, the
collision broadphase, the collider debug draw, and — importantly — child entities, which orbit
their parent's anchor rather than its top-left corner.

`scale` acts about it too, so an entity grows in place rather than sprawling down and to the right
from its corner. Anchor at `{x=0, y=0}` to get corner-anchored growth instead. Scale `1` is always
a no-op: an unscaled entity never moves because of this.

> **Migrating an existing project.** Scaling about the anchor is a change in where scaled entities
> land. Anything at scale `1` is untouched, and rotation-only behaviour is unchanged, but a sprite
> at scale `2` now sits half its growth up and to the left of where it used to, holding its centre
> instead of its corner. Add `pivot = { x = 0, y = 0 }` to restore the old placement per entity.
>
> Separately, a child that carries its own geometry under a **rotated** parent moves even at scale
> `1`. That is a bug fix: the child's anchor previously did not travel through the parent's
> rotation, so it span about the wrong point. Nothing needs to change in your project, but
> rotated hierarchies will look different — and correct.

### Reading transforms from Lua

`position`, `scale`, and `pivot` all expose their `vec2` as `value` plus `x` and `y` accessors that
write through to the component:

```lua
local pos = registry.get_position(entity)
pos.x = 100          -- equivalent to pos.value.x = 100
local s = registry.get_scale(entity)
s.scale = 2          -- uniform view: writes both axes
if not s:is_uniform() then print(s.x, s.y) end
```

`scale` (the singular accessor) reports the **x** axis, so it only summarises the pair when
`is_uniform()` is true. Reading it on a per-axis scale is safe; it just tells you half the story.

### `rigidbody`

Gives an entity physical properties like velocity.

| Field      | Type           | Default      | Description                              |
|------------|----------------|--------------|------------------------------------------|
| `velocity` | `table {x, y}` | `{x=0, y=0}` | The movement speed in pixels per second. |

---

## Rendering Components

### `sprite`

Renders a 2D texture from the asset manager.

| Field              | Type                 | Default            | Description                                          |
|--------------------|----------------------|--------------------|------------------------------------------------------|
| `texture_asset_id` | `string`             | **Required**       | The ID of the loaded texture asset.                  |
| `width`            | `number`             | **Required**       | The render width.                                    |
| `height`           | `number`             | **Required**       | The render height.                                   |
| `layer`            | `number`             | `1`                | Z-index for rendering order (higher is on top).      |
| `fixed`            | `boolean`            | `false`            | If true, the sprite ignores the camera (UI element). |
| `src_rect_x`       | `number`             | `0`                | X-offset in the source texture.                      |
| `src_rect_y`       | `number`             | `0`                | Y-offset in the source texture.                      |
| `color_mod`        | `table {r, g, b, a}` | `{255,255,255,255}`| Multiplicative color tint (`0-255`).                 |
| `alpha`            | `integer`            | `255`              | Convenience alpha override (`0-255`; wins over `color_mod.a`). |
| `blend_mode`       | `string`             | `"blend"`          | Blend mode (`"none"`, `"blend"`, `"add"`, `"mod"`).  |

Runtime (`registry.get_sprite(entity)`): `width` (read-only), `height` (read-only), `asset_id` (read-only), `layer`, `color_mod`, `alpha`, `blend_mode`.

### `animation`

Adds frame-based animation to a `sprite`.

| Field        | Type     | Default | Description                              |
|--------------|----------|---------|------------------------------------------|
| `num_frames` | `number` | `1`     | Total number of frames in the animation. |
| `speed_rate` | `number` | `1`     | Animation speed in frames per second.    |

Runtime (`registry.get_animation(entity)`): `num_frames`, `current_frame`, `frame_rate_speed`, `should_loop`, `is_finished`.

### `square`

Renders a colored rectangle (optionally rounded, bordered, or filled partially as a bar/track).

| Field              | Type                 | Default                | Description                                           |
|--------------------|----------------------|------------------------|-------------------------------------------------------|
| `position`         | `table {x, y}`       | `{x=0, y=0}`           | Local offset position.                                |
| `width`            | `number`             | `0`                    | Width of the rectangle.                               |
| `height`           | `number`             | `0`                    | Height of the rectangle.                              |
| `color`            | `table {r, g, b, a}` | `{r=0, g=0, b=0, a=0}` | Fill color (`0-255`).                                 |
| `layer`            | `number`             | `1`                    | Rendering layer.                                      |
| `fixed`            | `boolean`            | `false`                | If true, ignores camera (alias: `is_fixed`).          |
| `border_thickness` | `number`             | `0`                    | Border stroke width in pixels.                        |
| `border_color`     | `table {r, g, b, a}` | `{255, 255, 255, 255}` | Border color (`0-255`).                               |
| `track_color`      | `table {r, g, b, a}` | `{0, 0, 0, 0}`         | Background track color behind `fill_ratio` (`0-255`). |
| `corner_radius`    | `number`             | `0`                    | Corner rounding radius in pixels.                     |
| `fill_ratio`       | `number`             | `1.0`                  | Horizontal fill fraction (`0.0..1.0`).                |
| `blend_mode`       | `string`             | `"blend"`              | Blend mode (`"none"`, `"blend"`, `"add"`, `"mod"`).   |

Runtime (`registry.get_square(entity)`): exposes `width`, `height`, `color`, `position`, `layer`, `is_fixed`, `border_thickness`, `border_color`, `track_color`, `corner_radius`, `fill_ratio`, `blend_mode`.

### `circle`

Renders a filled or outlined circle.

| Field        | Type                 | Default                | Description                                         |
|--------------|----------------------|------------------------|-----------------------------------------------------|
| `position`   | `table {x, y}`       | `{x=0, y=0}`           | Local center offset.                                |
| `radius`     | `number`             | `0`                    | Circle radius in pixels.                            |
| `thickness`  | `number`             | `1`                    | Outline stroke width when `filled = false`.         |
| `color`      | `table {r, g, b, a}` | `{r=0, g=0, b=0, a=0}` | Circle color (`0-255`).                             |
| `layer`      | `number`             | `1`                    | Rendering layer.                                    |
| `fixed`      | `boolean`            | `false`                | If true, ignores camera (runtime field: `is_fixed`).|
| `filled`     | `boolean`            | `true`                 | Draws filled interior when `true`, ring when `false`.|
| `blend_mode` | `string`             | `"blend"`              | Blend mode (`"none"`, `"blend"`, `"add"`, `"mod"`). |

### `capsule`

Renders a filled or outlined capsule (rounded line segment).

| Field        | Type                 | Default                | Description                                         |
|--------------|----------------------|------------------------|-----------------------------------------------------|
| `start`      | `table {x, y}`       | `{x=0, y=0}`           | Local start point.                                  |
| `end`        | `table {x, y}`       | `{x=0, y=0}`           | Local end point.                                    |
| `radius`     | `number`             | `0`                    | Capsule radius in pixels.                           |
| `thickness`  | `number`             | `1`                    | Outline stroke width when `filled = false`.         |
| `fill_ratio` | `number`             | `1.0`                  | Progress fill ratio (`0.0..1.0`).                   |
| `color`      | `table {r, g, b, a}` | `{r=0, g=0, b=0, a=0}` | Capsule color (`0-255`).                            |
| `layer`      | `number`             | `1`                    | Rendering layer.                                    |
| `fixed`      | `boolean`            | `false`                | If true, ignores camera (runtime field: `is_fixed`).|
| `filled`     | `boolean`            | `true`                 | Draws filled interior when `true`.                  |
| `blend_mode` | `string`             | `"blend"`              | Blend mode (`"none"`, `"blend"`, `"add"`, `"mod"`). |

### `triangle`

Renders a filled or outlined triangle.

| Field        | Type                 | Default                | Description                                         |
|--------------|----------------------|------------------------|-----------------------------------------------------|
| `p1`, `p2`, `p3` | `table {x, y}`   | `{x=0, y=0}`           | Local triangle vertex coordinates.                  |
| `thickness`  | `number`             | `1`                    | Outline stroke width when `filled = false`.         |
| `color`      | `table {r, g, b, a}` | `{r=0, g=0, b=0, a=0}` | Triangle color (`0-255`).                           |
| `layer`      | `number`             | `1`                    | Rendering layer.                                    |
| `fixed`      | `boolean`            | `false`                | If true, ignores camera (runtime field: `is_fixed`).|
| `filled`     | `boolean`            | `true`                 | Draws filled interior when `true`.                  |
| `blend_mode` | `string`             | `"blend"`              | Blend mode (`"none"`, `"blend"`, `"add"`, `"mod"`). |

### `line`

Renders a line segment.

| Field        | Type                 | Default                | Description                                         |
|--------------|----------------------|------------------------|-----------------------------------------------------|
| `start`      | `table {x, y}`       | `{x=0, y=0}`           | Local start point.                                  |
| `end`        | `table {x, y}`       | `{x=0, y=0}`           | Local end point.                                    |
| `thickness`  | `number`             | `1`                    | Stroke width in pixels.                             |
| `color`      | `table {r, g, b, a}` | `{r=0, g=0, b=0, a=0}` | Line color (`0-255`).                               |
| `layer`      | `number`             | `1`                    | Rendering layer.                                    |
| `fixed`      | `boolean`            | `false`                | If true, ignores camera (runtime field: `is_fixed`).|
| `blend_mode` | `string`             | `"blend"`              | Blend mode (`"none"`, `"blend"`, `"add"`, `"mod"`). |

### `text_label`

Renders text using a loaded font, with optional alignment, word wrapping, drop shadow, and outline effects.

| Field               | Type                                | Default                | Description                                                                                     |
|---------------------|-------------------------------------|------------------------|-------------------------------------------------------------------------------------------------|
| `text`              | `string`                            | **Required**           | The text to display.                                                                            |
| `font_id`           | `string`                            | **Required**           | The ID of the loaded font asset.                                                                |
| `position`          | `table {x, y}`                      | `{x=0, y=0}`           | Local offset position.                                                                          |
| `color`             | `table {r, g, b, a}`                | `{r=0, g=0, b=0, a=0}` | Text color (`0-255`).                                                                           |
| `layer`             | `number`                            | `1`                    | Rendering layer.                                                                                |
| `is_fixed`          | `boolean`                           | `true`                 | If `true`, ignores camera.                                                                      |
| `align`             | `string`                            | `"left"`               | Horizontal alignment inside `ui_rect`: `"left"`, `"center"`, or `"right"`.                      |
| `valign`            | `string`                            | `"top"`                | Vertical alignment inside `ui_rect`: `"top"`, `"center"`, or `"bottom"`.                        |
| `wrap`              | `boolean`                           | `false`                | If `true`, word-wraps text to the entity's resolved `ui_rect` width.                            |
| `wrap_width`        | `number`                            | `0`                    | Explicit wrap width in pixels (`<= 0` disables explicit wrap).                                  |
| `shadow`            | `boolean`                           | `false`                | Shorthand: when `true`, enables a `(1, 1)` pixel drop shadow.                                   |
| `shadow_offset`     | `table {x, y}`                      | `{x=0, y=0}`           | Drop-shadow `(x, y)` pixel offset (`{x=0, y=0}` disables shadow).                               |
| `shadow_color`      | `table {r, g, b, a}`                | `{r=0, g=0, b=0, a=200}`| Drop-shadow color (`0-255`).                                                                    |
| `outline`           | `boolean` / `number` / `table`      | `false`                | Shorthand: `true` (`1px`), thickness in pixels (`0..4`), or `{r, g, b, a}` color table (`1px`). |
| `outline_width`     | `number`                            | `0`                    | Outline stroke thickness in pixels (clamped to `0..4`; alias: `outline_thickness`).             |
| `outline_color`     | `table {r, g, b, a}`                | `{r=0, g=0, b=0, a=255}`| Outline stroke color (`0-255`). Defaults `outline_width` to `1` if unset and alpha `> 0`.       |

At runtime (`local lbl = registry.get_text_label(entity)`), you can mutate `text`, `color`, `layer`, `position`, `is_fixed`, `h_align`, `v_align`, `wrap_width`, `wrap`, `outline_width` (`outline_thickness`), `outline_color`, `shadow_offset`, `shadow_color`, or call `lbl:set_outline(color_or_width, width?)` (accepts a boolean, pixel width, CSS/hex color string like `"#102030"`, or `{r, g, b, a}` table).

---

## Logic & Interaction

### `script`

Attaches custom Lua behavior to an entity.

| Field               | Type       | Description                                                            |
|---------------------|------------|------------------------------------------------------------------------|
| `on_update`         | `function` | Called every frame: `function(self, entity, delta_time)`.              |
| `on_collision`      | `function` | Called on collision enter: `function(self, entity, other)`.            |
| `on_collision_exit` | `function` | Called on collision exit: `function(self, entity, other)`.             |
| `on_debug_gui`      | `function` | Called during ImGui pass: `function(self, entity)`.                    |
| `source`            | `string`   | Optional project-relative script path returning the callback table (enables hot reload). |

### `script_callbacks`

Typed native/Lua callback component exposing `script_table`, `on_hit`, and `on_death` slots.

| Field          | Type       | Default | Description                      |
|----------------|------------|---------|----------------------------------|
| `script_table` | `table`    | `nil`   | Associated state table.          |
| `on_hit`       | `function` | `nil`   | Hit callback function/object.    |
| `on_death`     | `function` | `nil`   | Death callback function/object.  |

### `box_collider`

Defines a rectangular area for collision detection.

| Field            | Type           | Default      | Description                                  |
|------------------|----------------|--------------|----------------------------------------------|
| `width`          | `number`       | `1`          | Width of the collider box.                   |
| `height`         | `number`       | `1`          | Height of the collider box.                  |
| `offset`         | `table {x, y}` | `{x=0, y=0}` | Offset from the entity's transform position. |
| `is_fixed`       | `boolean`      | `false`      | If true, ignores camera transform.           |
| `collision_mask` | `number`       | `1`          | Bitmask for filtering collisions.            |

### `circle_collider`

Defines a circular area for collision detection.

| Field            | Type           | Default      | Description                                  |
|------------------|----------------|--------------|----------------------------------------------|
| `radius`         | `number`       | `1.0`        | Radius of the collider circle.               |
| `offset`         | `table {x, y}` | `{x=0, y=0}` | Offset from transform position (alias: `position`). |
| `is_fixed`       | `boolean`      | `false`      | If true, ignores camera transform.           |
| `collision_mask` | `number`       | `1`          | Bitmask for filtering collisions.            |

### `capsule_collider`

Defines a capsule collider from two local endpoints and a radius.

| Field            | Type           | Default      | Description                        |
|------------------|----------------|--------------|------------------------------------|
| `start`          | `table {x, y}` | `{x=0, y=0}` | Local capsule start endpoint.      |
| `end`            | `table {x, y}` | `{x=0, y=0}` | Local capsule end endpoint.        |
| `radius`         | `number`       | `1.0`        | Capsule radius.                    |
| `is_fixed`       | `boolean`      | `false`      | If true, ignores camera transform. |
| `collision_mask` | `number`       | `1`          | Bitmask for filtering collisions.  |

### `triangle_collider`

Defines a triangle collider from three local vertices.

| Field            | Type           | Default      | Description                        |
|------------------|----------------|--------------|------------------------------------|
| `p1`, `p2`, `p3` | `table {x, y}` | `{x=0, y=0}` | Local triangle vertices.           |
| `is_fixed`       | `boolean`      | `false`      | If true, ignores camera transform. |
| `collision_mask` | `number`       | `1`          | Bitmask for filtering collisions.  |

### `line_collider`

Defines a line-segment collider from two local endpoints.

| Field            | Type           | Default      | Description                        |
|------------------|----------------|--------------|------------------------------------|
| `start`          | `table {x, y}` | `{x=0, y=0}` | Local segment start point.         |
| `end`            | `table {x, y}` | `{x=0, y=0}` | Local segment end point.           |
| `is_fixed`       | `boolean`      | `false`      | If true, ignores camera transform. |
| `collision_mask` | `number`       | `1`          | Bitmask for filtering collisions.  |

### `camera_follow`

Forces the main camera to follow this entity. Has no fields.
Example: `camera_follow = {}`

### `audio_listener`

Marks an entity as the spatial audio listener. Attenuation and Doppler shifts are computed relative to this entity. Only one active listener should exist per scene.

| Field            | Type     | Default  | Description                                       |
|------------------|----------|----------|---------------------------------------------------|
| `max_distance`   | `number` | `1000.0` | Maximum listener hearing distance for culling.    |
| `rolloff`        | `number` | `1.0`    | Attenuation rolloff factor.                       |
| `doppler_factor` | `number` | `1.0`    | Multiplier applied to Doppler pitch shift.        |
| `speed_of_sound` | `number` | `343.0`  | Medium speed of sound in pixels/units per second. |

### `audio_source`

Attaches a triggered or looping sound to an entity, optionally spatialized relative to the `audio_listener`.

| Field              | Type      | Default      | Description                                              |
|--------------------|-----------|--------------|----------------------------------------------------------|
| `clip_id`          | `string`  | **Required** | The ID of the loaded sound asset (must be present).      |
| `volume`           | `number`  | `1.0`        | Playback volume (`0.0-1.0`).                             |
| `pitch`            | `number`  | `1.0`        | Playback pitch multiplier.                               |
| `loop`             | `boolean` | `false`      | If true, the sound loops continuously.                   |
| `play_on_spawn`    | `boolean` | `true`       | Starts playback automatically when spawned.              |
| `despawn_on_finish`| `boolean` | `false`      | Despawns the entity once non-looping playback finishes.  |
| `spatial`          | `boolean` | `false`      | Enables distance attenuation and stereo panning.         |
| `min_distance`     | `number`  | `50.0`       | Distance inside which spatial volume stays at full gain. |
| `max_distance`     | `number`  | `1000.0`     | Distance beyond which spatial audio attenuates/culls.    |
| `doppler`          | `boolean` | `false`      | Enables Doppler shift from relative `rigidbody` velocity.|

### `ui_button`

Makes an entity clickable and input-triggerable. Triggering via a mapped key, controller button, or action calls `on_click`, identical to clicking with the mouse.

| Field               | Type                 | Default            | Description                                                                                                  |
|---------------------|----------------------|--------------------|--------------------------------------------------------------------------------------------------------------|
| `is_active`         | `boolean`            | `true`             | Whether the button is interactive.                                                                           |
| `is_fixed`          | `boolean`            | `true`             | If true, button hit-testing ignores camera scroll.                                                           |
| `on_click`          | `function`           | `nil`              | Function called on click or input trigger: `function(self, entity)`.                                         |
| `on_focus`          | `function`           | `nil`              | Function called when the button receives focus.                                                              |
| `key`               | `string`             | `""`               | Keyboard key(s) that trigger the button (e.g. `"space"`, `"return, space"`). Alias: `input_key`.             |
| `controller_button` | `string`             | `""`               | Gamepad button(s) that trigger the button (e.g. `"a"`, `"south, start"`). Aliases: `gamepad`, `gamepad_button`. |
| `action`            | `string`             | `""`               | Named input action configured in `input` that triggers the button (e.g. `"ui_confirm"`, `"jump"`).           |
| `hover_color`       | `table {r, g, b, a}` | *(unset)*          | Optional hover tint when pointer is over the button.                                                         |
| `press_color`       | `table {r, g, b, a}` | *(unset)*          | Optional press tint while held.                                                                              |
| `hover_sound`       | `string`             | `""`               | Audio clip ID played via `AudioPlayEvent` on hover enter.                                                    |
| `click_sound`       | `string`             | `""`               | Audio clip ID played via `AudioPlayEvent` on click/trigger.                                                  |
| `focus_sound`       | `string`             | `""`               | Audio clip ID played via `AudioPlayEvent` on focus enter.                                                    |

Runtime (`registry.get_ui_button(entity)`): also exposes `is_hovered`, `is_pressed`, `has_hover_color`, `has_press_color`, and `button:trigger(entity?) -> boolean` to programmatically invoke `on_click`.

### `ui_canvas`

Marks an entity as a UI layout root. All children positioned via `ui_anchor`, `ui_flex`, or `ui_layout_box` must descend from an entity with `ui_canvas`.

| Field        | Type      | Default | Description                                                                  |
|--------------|-----------|---------|------------------------------------------------------------------------------|
| `is_fixed`   | `boolean` | `true`  | If `true`, the canvas stays fixed on screen and ignores the camera viewport. |
| `width`      | `number`  | `0.0`   | Explicit canvas width in pixels (`0.0` uses current viewport width).          |
| `height`     | `number`  | `0.0`   | Explicit canvas height in pixels (`0.0` uses current viewport height).        |
| `base_layer` | `integer` | `0`     | Baseline render layer inherited by all descendant UI elements.                |

### `ui_anchor`

Anchors a child entity within its parent's resolved rectangle.

| Field           | Type     | Default | Description                                                                      |
|-----------------|----------|---------|----------------------------------------------------------------------------------|
| `anchor_left`   | `number` | `0.0`   | Normalized parent left edge (`0.0` = parent left, `1.0` = parent right).         |
| `anchor_top`    | `number` | `0.0`   | Normalized parent top edge (`0.0` = parent top, `1.0` = parent bottom).          |
| `anchor_right`  | `number` | `1.0`   | Normalized parent right edge.                                                    |
| `anchor_bottom` | `number` | `1.0`   | Normalized parent bottom edge.                                                   |
| `offset_left`   | `number` | `0.0`   | Pixel offset added after anchor evaluation (`left_px = parent_left + offset`).   |
| `offset_top`    | `number` | `0.0`   | Pixel offset added after anchor evaluation.                                      |
| `offset_right`  | `number` | `0.0`   | Pixel offset added after anchor evaluation.                                      |
| `offset_bottom` | `number` | `0.0`   | Pixel offset added after anchor evaluation.                                      |

### `ui_z_index`

Specifies a local render layer delta for a UI entity.

| Field | Type      | Default | Description                                                                                |
|-------|-----------|---------|--------------------------------------------------------------------------------------------|
| `z`   | `integer` | `0`     | Relative layer offset. Layers accumulate down the hierarchy from `canvas.base_layer`.       |

### `ui_flex`

Configures CSS-like flexbox or grid layout for child entities.

| Field            | Type      | Default     | Description                                                                              |
|------------------|-----------|-------------|------------------------------------------------------------------------------------------|
| `direction`      | `string`  | `"row"`     | Main axis direction (`"row"` or `"column"`).                                             |
| `gap`            | `number`  | `0.0`       | Spacing in pixels between children along the main axis.                                  |
| `cross_gap`      | `number`  | `-1.0`      | Spacing in pixels between wrapped lines or rows/columns (`-1.0` uses `gap`).             |
| `wrap`           | `boolean` | `false`     | When `true`, children that exceed container extent wrap onto new lines.                  |
| `columns`        | `integer` | `0`         | When `> 0`, enables fixed-column grid partitioning into `N` equal-width columns.          |
| `padding_left`   | `number`  | `0.0`       | Left padding inset in pixels (uniform `padding` shorthand also accepted).                |
| `padding_top`    | `number`  | `0.0`       | Top padding inset in pixels.                                                             |
| `padding_right`  | `number`  | `0.0`       | Right padding inset in pixels.                                                           |
| `padding_bottom` | `number`  | `0.0`       | Bottom padding inset in pixels.                                                          |
| `justify`        | `string`  | `"stretch"` | Main-axis distribution (`"start"`, `"center"`, `"end"`, `"space_between"`, `"stretch"`). |
| `align`          | `string`  | `"stretch"` | Cross-axis alignment (`"start"`, `"center"`, `"end"`, `"stretch"`).                       |

### `ui_layout_box`

Defines sizing constraints and outer margins on UI elements.

| Field           | Type     | Default  | Description                                                                                 |
|-----------------|----------|----------|---------------------------------------------------------------------------------------------|
| `width`         | `number` / `string` | `"auto"` | Box width (`"200px"`, `"50%"`, `"1fr"`, `"auto"`).                                          |
| `height`        | `number` / `string` | `"auto"` | Box height (`"200px"`, `"50%"`, `"1fr"`, `"auto"`).                                         |
| `min_width`     | `number` / `string` | `"0px"`  | Lower clamp on width.                                                                       |
| `max_width`     | `number` / `string` | `""`     | Upper clamp on width (empty = unconstrained).                                               |
| `min_height`    | `number` / `string` | `"0px"`  | Lower clamp on height.                                                                      |
| `max_height`    | `number` / `string` | `""`     | Upper clamp on height (empty = unconstrained).                                              |
| `margin_left`   | `number` | `0.0`    | Left outer margin in pixels (uniform `margin` shorthand also accepted).                     |
| `margin_top`    | `number` | `0.0`    | Top outer margin in pixels.                                                                 |
| `margin_right`  | `number` | `0.0`    | Right outer margin in pixels.                                                                |
| `margin_bottom` | `number` | `0.0`    | Bottom outer margin in pixels.                                                              |
| `visible`       | `boolean`| `true`   | When `false`, the element is hidden and skipped during layout resolution.                   |

### `ui_clip`

Enables hardware scissor clipping for the entity and all of its descendants during rendering and pointer hit-testing.

| Field     | Type      | Default | Description                                                     |
|-----------|-----------|---------|-----------------------------------------------------------------|
| `enabled` | `boolean` | `true`  | If `true`, rendering and pointer hits are clipped to this rect. |

### `ui_nine_slice`

Specifies 9-patch border insets on an image or panel, preserving unstretched corner caps while scaling edges and center.

| Field    | Type     | Default | Description                                |
|----------|----------|---------|--------------------------------------------|
| `left`   | `number` | `0.0`   | Left border inset in source texels (uniform `border` shorthand also accepted). |
| `top`    | `number` | `0.0`   | Top border inset in source texels.         |
| `right`  | `number` | `0.0`   | Right border inset in source texels.       |
| `bottom` | `number` | `0.0`   | Bottom border inset in source texels.      |

### `ui_scroll`

Enables mouse-wheel scrolling for a clipped viewport. Usually paired with `ui_clip`.

| Field            | Type      | Default | Description                                                               |
|------------------|-----------|---------|---------------------------------------------------------------------------|
| `scroll_x`       | `number`  | `0.0`   | Current horizontal scroll offset in pixels.                               |
| `scroll_y`       | `number`  | `0.0`   | Current vertical scroll offset in pixels.                                 |
| `content_width`  | `number`  | `0.0`   | Computed total content width (read-only from Lua).                        |
| `content_height` | `number`  | `0.0`   | Computed total content height (read-only from Lua).                       |
| `wheel_step`     | `number`  | `36.0`  | Pixels scrolled per mouse-wheel notch.                                    |
| `horizontal`     | `boolean` | `false` | Enables horizontal mouse-wheel scrolling.                                 |
| `vertical`       | `boolean` | `true`  | Enables vertical mouse-wheel scrolling.                                   |

### `ui_focus`

Configures keyboard and gamepad focus navigation, state flags, and directional links.

| Field        | Type      | Default | Description                                                                                     |
|--------------|-----------|---------|-------------------------------------------------------------------------------------------------|
| `tab_index`  | `integer` | `0`     | Tab ordering key (`0` = sequential document order).                                             |
| `is_focused` | `boolean` | `false` | Whether this entity currently holds UI input focus.                                             |
| `focusable`  | `boolean` | `true`  | Whether this entity can receive keyboard/gamepad focus.                                         |
| `checked`    | `boolean` | `false` | Toggle/checkbox checked state.                                                                  |
| `disabled`   | `boolean` | `false` | Disabled state: suppresses input and applies the `:disabled` style state.                       |
| `inert`      | `boolean` | `false` | Inert state: suspends input and focus, but **preserves normal visuals** (no `:disabled` style). |
| `nav_up`     | `entity`  | `nil`   | Explicit focus target for Up directional navigation.                                            |
| `nav_down`   | `entity`  | `nil`   | Explicit focus target for Down directional navigation.                                          |
| `nav_left`   | `entity`  | `nil`   | Explicit focus target for Left directional navigation.                                          |
| `nav_right`  | `entity`  | `nil`   | Explicit focus target for Right directional navigation.                                         |

### `ui_style_state`

Stores visual overrides (`ui_style_patch`) for interactive states: `hover`, `press`, `focus`, `checked`, `checked_hover`, `checked_press`, and `disabled`. Each `ui_style_patch` contains `bg_color`, `border_color`, `border_width`, `text_color`, `opacity`, and `flags`.

### `ui_rect`

Resolved screen-space pixel rectangle computed every frame by `UILayoutSystem`.

| Property         | Type      | Access    | Description                                                                                     |
|------------------|-----------|-----------|-------------------------------------------------------------------------------------------------|
| `left`           | `number`  | Read-only | Left pixel coordinate in screen/canvas space.                                                   |
| `top`            | `number`  | Read-only | Top pixel coordinate in screen/canvas space.                                                    |
| `right`          | `number`  | Read-only | Right pixel coordinate in screen/canvas space.                                                  |
| `bottom`         | `number`  | Read-only | Bottom pixel coordinate in screen/canvas space.                                                 |
| `width`          | `number`  | Read-only | Computed pixel width (`right - left`).                                                          |
| `height`         | `number`  | Read-only | Computed pixel height (`bottom - top`).                                                         |
| `center`         | `vec2`    | Read-only | Computed center position `(x, y)`.                                                              |
| `layer`          | `integer` | Read-only | Resolved render layer (`canvas.base_layer` + accumulated `z_index`).                            |
| `blocks_pointer` | `boolean` | Read/Write| When `true` (via `pointer-events: stop`), pointer events cannot reach lower layers below this.  |

### `color_grid`

Renders an infinite or bounded scrolling two-color checkerboard grid. Handled by `RenderColorGridSystem` for background effects.

| Field             | Type                  | Default                 | Description                                                                 |
|-------------------|-----------------------|-------------------------|-----------------------------------------------------------------------------|
| `cell_width`      | `number`              | `32.0`                  | Width of each grid cell in pixels (min `8.0`).                              |
| `cell_height`     | `number`              | `32.0`                  | Height of each grid cell in pixels (min `8.0`).                             |
| `cell_size`       | `number`              | `32.0`                  | Shorthand property setting both `cell_width` and `cell_height`.             |
| `color1`          | `table {r, g, b, a}`  | `{40, 40, 40, 255}`     | First grid square color. Also aliased as `color_a`.                         |
| `color2`          | `table {r, g, b, a}`  | `{70, 70, 70, 255}`     | Second grid square color. Also aliased as `color_b`.                        |
| `offset`          | `table {x, y}`        | `{x=0, y=0}`            | Local pixel offset of the grid origin.                                      |
| `scroll_velocity` | `table {x, y}`        | `{x=0, y=0}`            | Pixels per second to continuously scroll the grid.                          |
| `bounds`          | `table {x, y}`        | `{x=0, y=0}`            | Dimensions of grid coverage in pixels. `{0, 0}` covers the entire viewport. |
| `layer`           | `integer`             | `0`                     | Render queue layer.                                                         |
| `is_fixed`        | `boolean`             | `false`                 | If true, stays fixed relative to the window rather than moving with camera. |
| `blend_mode`      | `string`              | `"blend"`               | Blend mode (`"none"`, `"blend"`, `"add"`, `"mod"`).                         |

```lua
components = {
  color_grid = {
    cell_size = 48,
    color1 = { 25, 25, 35, 255 },
    color2 = { 35, 35, 50, 255 },
    scroll_velocity = { x = 20, y = 0 }
  }
}
```

### `lifetime`

Automatically counts down remaining time and queues the entity for destruction when time expires. Processed by `LifetimeSystem`.

| Field      | Type     | Default | Description                                              |
|------------|----------|---------|----------------------------------------------------------|
| `duration` | `number` | `1.0`   | Total lifespan in seconds. A bare number is also accepted. |

```lua
components = { lifetime = { duration = 2.5 } }  -- table authoring
components = { lifetime = 2.5 }                 -- bare number shorthand
```

Properties and Methods:
- `lifetime.duration` — Read/write total duration.
- `lifetime.remaining_duration` — Read-only remaining time in seconds.
- `lifetime:extend(seconds)` — Adds additional seconds to the remaining duration.
- `lifetime:reset()` — Resets remaining duration back to the total `duration`.
- `lifetime:is_alive()` — Returns true if remaining duration is greater than 0.

---

## Common Entity Patterns

Components compose naturally. These combinations cover most game object archetypes.

### Player

```lua
{
    tag  = "player",
    name = "Player",
    components = {
        transform      = { position = { x = 200, y = 300 } },
        sprite         = { texture_asset_id = "player", width = 32, height = 32, layer = 2 },
        rigidbody      = { velocity = { x = 0, y = 0 } },
        entity_mask    = { value = 1 },
        box_collider   = { width = 32, height = 32, collision_mask = 2 },
        camera_follow  = {},
        audio_listener = {},
        script         = { speed = 100, on_update = player_update },
    }
}
```

### Enemy

```lua
{
    tag  = "enemy",
    name = "Enemy",
    components = {
        transform    = { position = { x = 600, y = 300 } },
        sprite       = { texture_asset_id = "enemy", width = 32, height = 32, layer = 2 },
        rigidbody    = { velocity = { x = -60, y = 0 } },
        entity_mask  = { value = 2 },
        box_collider = { width = 32, height = 32, collision_mask = 1 },
        script       = {
            hp = 50,
            on_collision = function(self, entity, other)
                self.hp = self.hp - 10
                if self.hp <= 0 then blam(entity) end
            end,
        },
    }
}
```

### Ambient sound source

```lua
{
    components = {
        transform    = { position = { x = 400, y = 400 } },
        audio_source = { clip_id = "ambient-wind", loop = true, volume = 0.6, spatial = true },
    }
}
```

### HUD overlay

Text and shapes with `fixed = true` / `is_fixed = true` stay in screen space,
unaffected by the camera. Layer them high so they always draw on top.

```lua
-- Score label
{
    components = {
        transform  = { position = { x = 20, y = 20 } },
        text_label = {
            text     = "Score: 0",
            font_id  = "hud-font",
            color    = { r = 255, g = 255, b = 255, a = 255 },
            layer    = 10,
            is_fixed = true,
        },
        script = {
            on_update = function(self, entity, dt)
                -- update text via registry when score changes
            end,
        },
    }
}

-- Semi-transparent health bar background
{
    components = {
        transform = { position = { x = 20, y = 50 } },
        square    = {
            width  = 200,
            height = 16,
            color  = { r = 60, g = 0, b = 0, a = 180 },
            layer  = 9,
            fixed  = true,
        },
    }
}
```

---

## Internal / Advanced

### `entity_mask`

Explicitly sets the collision bitmask for an entity.

| Field   | Type     | Default | Description        |
|---------|----------|---------|--------------------|
| `value` | `number` | `1`     | The bitmask value. |

### `chunk_resident`

Tracks an entity spawned by `TilemapSystem` during LDtk chunk streaming, binding it to its home chunk and prop index for persistence and hysteresis windowing.

| Field        | Type      | Default | Description                                              |
|--------------|-----------|---------|----------------------------------------------------------|
| `chunk_x`    | `integer` | `0`     | Grid X coordinate of the chunk that spawned this entity. |
| `chunk_y`    | `integer` | `0`     | Grid Y coordinate of the chunk that spawned this entity. |
| `prop_index` | `integer` | `0`     | Unique prop index within the chunk.                      |
