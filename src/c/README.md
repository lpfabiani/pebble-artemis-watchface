# Artemis Watchface — Source Architecture

## File Structure

```
src/c/
├── README.md             this file
├── artemis.h             the one header every module includes. Contains:
│                         debug flag, storage keys, slot counts, DisplayMode
│                         enum, FieldType enum, Night Sky color palette,
│                         platform font resource IDs and heights,
│                         ArtemisSettings / ArtemisData structs (including
│                         info_on_shake / info_display_s), extern declarations
│                         for all shared globals, and prototypes for
│                         overlay_geometry(), artemis_update_display(), and
│                         artemis_apply_shake_setting().
│
├── artemis_comms.c/.h    phone ↔ watch AppMessage: inbox/outbox handlers,
│                         data request, Clay settings sync (including
│                         INFO_ON_SHAKE and INFO_DISPLAY_S). Calls
│                         artemis_apply_shake_setting() after settings change.
│
├── artemis_event.c/.h    special event detection and overlay banner.
│                         Owns: s_event_overlay_layer, s_active_event_msg.
│                         API: artemis_event_create(root)
│                              artemis_event_destroy()
│                              artemis_event_show()
│                              artemis_event_hide()
│                              artemis_event_check() → const char*
│                                scans hardcoded table + API milestones;
│                                vibrates on transition; returns msg or NULL.
│                              artemis_event_update(msg)
│                                measures msg with
│                                graphics_text_layout_get_content_size,
│                                reframes the overlay layer so the text block
│                                is vertically centred in the top zone, then
│                                calls text_layer_set_text.
│
├── artemis_info.c/.h     top-zone info slot display: slot layout, slot
│                         rendering, decorations (separator lines).
│                         Owns: s_info_layer, s_decorations_layer,
│                               s_field_*_layers, s_active_slots, s_num_active,
│                               slot/label buffers.
│                         API: artemis_info_create(root)
│                              artemis_info_destroy()
│                              artemis_info_show()
│                              artemis_info_hide()
│                              artemis_info_refresh()
│                                updates slot content only; no visibility
│                                changes (managed by main.c state machine).
│                              artemis_info_rebuild_slots()
│                         Internal: prv_create_slots() — round screen variant
│                              fills rows bottom-up: singles at the top with
│                              prv_isqrt chord-adapted width, pairs at the
│                              bottom (widest chord area). Avoids bezel
│                              clipping at the narrow top of the top zone.
│
├── artemis_logo.c/.h     Artemis 'A' PDC vector logo: load, scale, draw.
│                         Shown by default (DISPLAY_LOGO); hidden in
│                         DISPLAY_EVENT and DISPLAY_INFO modes.
│                         Owns: s_logo_layer, s_logo_pdc,
│                               s_logo_draw_size, s_logo_draw_offset.
│                         API: artemis_logo_create(root)
│                              artemis_logo_destroy()
│                              artemis_logo_show()
│                              artemis_logo_hide()
│                              artemis_logo_refresh()
│
├── artemis_clock.c/.h     always-visible background and bottom zone.
│                         sky background (black fill + star field) +
│                         s_time_area_layer container (moon bitmap, time,
│                         date). Container allows peek and DISPLAY_INFO
│                         hide via a single layer call.
│                         Owns: s_sky_layer, s_time_area_layer,
│                               s_moon_bitmap_layer, s_moon_bitmap,
│                               s_time_layer, s_date_layer.
│                         API: artemis_clock_create(root)
│                              artemis_clock_destroy()
│                              artemis_clock_show() / hide_bottom()
│                                show/hide s_time_area_layer (used by
│                                main.c in DISPLAY_INFO mode).
│                              artemis_clock_peek(unobstructed_h)
│                                reframes s_time_area_layer and re-centres
│                                time+date block for Timeline Peek.
│                              artemis_clock_refresh()  ← called every tick
│
└── main.c                init, deinit, window load/unload, tick handler,
                          display state machine (DISPLAY_LOGO / EVENT / INFO),
                          shake handler + info timer, Timeline Peek callbacks,
                          artemis_apply_shake_setting().
                          Owns: s_main_window, s_root_layer, s_root_w/h,
                                s_split_y, s_settings, s_artemis.
```

---

## Shared State (artemis.h)

Globals defined in `main.c` and declared `extern` in `artemis.h` so all
modules can read them:

| Variable | Type | Description |
|---|---|---|
| `s_settings` | `ArtemisSettings` | user prefs from persistent storage |
| `s_artemis` | `ArtemisData` | live telemetry from phone |
| `s_root_layer` | `Layer *` | window root, used as parent by creators |
| `s_root_w`, `s_root_h` | `int` | screen dimensions |
| `s_split_y` | `int` | y-coordinate dividing top/bottom zones |
| `s_font_time` | `GFont` | custom font for time display |
| `s_font_date` | `GFont` | custom font for date display |
| `s_font_event` | `GFont` | custom font for slot labels |
| `s_active_slots[]` | `int[]` | active slot indices (no FIELD_NONE) |
| `s_num_active` | `int` | count of active slots |

`overlay_geometry(w, h, *top, *h_out)` is declared in `artemis.h` (defined
in `main.c`) because it is needed by `artemis_logo`, `artemis_event`, and
`artemis_info` to compute the top-zone rectangle.

`artemis_apply_shake_setting()` is also declared in `artemis.h` (defined in
`main.c`) so `artemis_comms.c` can call it after a settings change without
a circular dependency.

---

## Module API Contract

Every display module (`artemis_clock`, `artemis_logo`, `artemis_info`)
exposes the same four operations:

- **`create(root)`** — allocate layers and add them to the layer tree.
  Sets up update_procs. Does not make layers visible.
- **`destroy()`** — free all owned layers and resources.
- **`show()` / `hide()`** — `layer_set_hidden` only. No content update.
- **`refresh()`** — update content (re-render text, mark dirty).
  Does not change visibility.

`main.c` always calls show/hide before refresh, never mixing them.

`artemis_event` is a new module separate from `artemis_info`. It owns the
event overlay layer and all event detection logic.

`artemis_info_refresh()` returns `void` and only updates slot content.
Visibility is managed entirely by `main.c`'s state machine.

---

## Display State Machine (main.c — artemis_update_display)

Three modes, evaluated every tick and on every relevant event:

| Mode | Trigger | Top zone | Bottom zone |
|---|---|---|---|
| `DISPLAY_LOGO` | default | logo visible | normal |
| `DISPLAY_EVENT` | special event active | event banner | normal |
| `DISPLAY_INFO` | shake (if enabled) | info slots | hidden |

```
artemis_event_check() ──► event_msg?
  yes AND mode ≠ INFO ──► DISPLAY_EVENT
  no  AND mode ≠ INFO ──► DISPLAY_LOGO
  (mode == INFO) ──────► stay INFO until timer fires → DISPLAY_LOGO
```

Events override logo but **never interrupt info** (info is "sticky").
Vibration fires on any event transition, even while in INFO mode.

### Timeline Peek

`.change` fires each animation frame; `.did_change` fires once at rest.

| Mode | Peek response |
|---|---|
| `DISPLAY_LOGO` | `artemis_clock_peek(unobstructed_h)` — compresses s_time_area_layer |
| `DISPLAY_EVENT` | same — event overlay is in top zone, unaffected |
| `DISPLAY_INFO` | bottom zone visible normally; hidden only while peek is active |

---

## Layer Tree

Layers listed in draw order (bottom → top). Later siblings draw over earlier ones.

```
s_main_window
└── s_root_layer                       window root, full screen
    │
    ├── s_sky_layer                    full screen — solid black + star pixels
    │                                  (artemis_clock)
    │
    ├── s_time_area_layer              bottom zone container, s_split_y → s_root_h
    │   │                              one layer_set_frame/hidden controls all three
    │   ├── s_moon_bitmap_layer        Y=0 relative — clips naturally when compressed
    │   │   └── (s_moon_bitmap)        GBitmap resource, not a layer
    │   ├── s_time_layer               vertically centred in container
    │   └── s_date_layer               below time layer
    │                                  (all: artemis_clock)
    │
    ├── s_info_layer                   top zone container, 0 → s_split_y+1
    │   │                              hidden by default; shown in DISPLAY_INFO
    │   ├── s_field_label_layers[0]    slot 0 label  ┐
    │   ├── s_field_value_layers[0]    slot 0 value  │
    │   ├── s_field_label_layers[1]                  │ active slots only;
    │   ├── s_field_value_layers[1]                  │ up to 6 pairs
    │   ├── ...                                      │
    │   └── s_decorations_layer        separator lines, drawn on top of fields
    │                                  (all: artemis_info)
    │
    ├── s_event_overlay_layer          top zone — event banner text
    │                                  hidden by default; shown in DISPLAY_EVENT
    │                                  (artemis_event)
    │
    └── s_logo_layer                   top zone — Artemis 'A' PDC vector
        └── (s_logo_pdc)               GDrawCommandImage, not a layer
                                       shown by default; hidden in EVENT/INFO
                                       (artemis_logo)
```

### Z-order notes

- `s_time_area_layer` is a container — hiding it or calling `layer_set_frame`
  on it affects all three children (moon, time, date) in one call.
- `s_info_layer` sits above `s_time_area_layer`. Hiding it clears the top
  zone without affecting the bottom zone.
- `s_decorations_layer` is the last child of `s_info_layer` so separator
  lines draw on top of field text.
- `s_event_overlay_layer` (owned by `artemis_event`) is a sibling of
  `s_info_layer`, not a child, so it can be shown independently.
- Exactly one of `s_logo_layer`, `s_event_overlay_layer`, `s_info_layer`
  is visible at any time — enforced by the state machine in `main.c`.
