# Artemis Watchface — Source Architecture

## File Structure

```
src/c/
├── README.md             this file
├── artemis.h             the one header every module includes. Contains:
│                         debug flag, storage keys, slot counts, FieldType
│                         enum, Night Sky color palette, platform font
│                         resource IDs and heights, ArtemisSettings /
│                         ArtemisData structs, extern declarations for all
│                         shared globals, and prototypes for
│                         overlay_geometry() and artemis_update_display().
│
├── artemis_mission.h     mission-specific data included only by artemis_info.c:
│                         LAUNCH_EPOCH, MISSION_END_HOURS, EVENT_DISPLAY_S,
│                         SpecialEvent struct + SPECIAL_EVENTS[] table,
│                         NUM_SPECIAL_EVENTS, FIELD_LABELS[].
│                         Not pulled into artemis.h — keeps static arrays out
│                         of unrelated compilation units.
│
├── artemis_comms.c/.h    phone ↔ watch AppMessage: inbox/outbox handlers,
│                         data request, Clay settings sync.
│                         Owns: nothing (reads/writes shared ArtemisData and
│                         ArtemisSettings via artemis.h; calls into
│                         artemis_info to rebuild slots on config change).
│
├── artemis_info.c/.h     top-zone information display: slot layout, slot
│                         rendering, decorations (separator lines), and
│                         special event detection.
│                         Owns: s_info_layer, s_decorations_layer,
│                               s_field_*_layers, s_event_overlay_layer,
│                               s_active_slots, s_num_active,
│                               slot/label buffers.
│                         API: artemis_info_create(root)
│                              artemis_info_destroy()
│                              artemis_info_show()
│                              artemis_info_hide()
│                              artemis_info_refresh() → bool
│                                returns true if the top zone is occupied
│                                (event overlay or fields); false when the
│                                top zone is empty (mission complete, no
│                                event), signalling main to show the logo.
│
├── artemis_logo.c/.h     Artemis 'A' PDC vector logo: load, scale, draw.
│                         Owns: s_logo_layer, s_logo_pdc,
│                               s_logo_draw_size, s_logo_draw_offset.
│                         API: artemis_logo_create(root)
│                              artemis_logo_destroy()
│                              artemis_logo_show()
│                              artemis_logo_hide()
│                              artemis_logo_refresh()
│
├── artemis_main.c/.h     always-visible background and bottom zone:
│                         sky background (black fill + star field),
│                         moon bitmap, time text, date text.
│                         Owns: s_sky_layer, s_moon_bitmap_layer,
│                               s_moon_bitmap, s_time_layer, s_date_layer,
│                               font globals (s_font_time/date/label).
│                         API: artemis_main_create(root)
│                              artemis_main_destroy()
│                              artemis_main_show()
│                              artemis_main_hide()
│                              artemis_main_refresh()   ← called every tick
│
└── main.c                init, deinit, window load/unload, tick handler,
                          display state orchestration (prv_update_display_ui).
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
| `s_font_label` | `GFont` | custom font for slot labels |
| `s_active_slots[]` | `int[]` | active slot indices (no FIELD_NONE) |
| `s_num_active` | `int` | count of active slots |

`overlay_geometry(w, h, *top, *h_out)` is also declared in `artemis.h`
(defined in `main.c`) because it is needed by both `artemis_logo` and
`artemis_info` to compute the top-zone rectangle.

---

## Module API Contract

Every display module (`artemis_main`, `artemis_logo`, `artemis_info`)
exposes the same four operations:

- **`create(root)`** — allocate layers and add them to the layer tree.
  Sets up update_procs. Does not make layers visible.
- **`destroy()`** — free all owned layers and resources.
- **`show()` / `hide()`** — `layer_set_hidden` only. No content update.
- **`refresh()`** — update content (re-render text, mark dirty).
  Does not change visibility.

`main.c` always calls show/hide before refresh, never mixing them.

`artemis_info_refresh()` additionally returns `bool`:
`true` = top zone is occupied (event or fields); `false` = empty.
`main.c` uses this return value to decide whether to show the logo.

---

## Display State Machine (main.c — artemis_update_display)

Priority: **Special event** > **Mission complete (logo)** > **Normal fields**

```
artemis_info_refresh() ──► true  ──► artemis_logo_hide()
                      └──► false ──► artemis_logo_show()
```

Inside `artemis_info_refresh()`:

```
special event active?
  yes ──► hide fields, show event overlay, return true
  no  ──► mission_complete?
            yes ──► hide fields, hide event overlay, return false
            no  ──► show fields, hide event overlay, return true
```

---

## Layer Tree

Layers listed in draw order (bottom → top). Later siblings draw over earlier ones.

```
s_main_window
└── s_root_layer                       window root, full screen
    │
    ├── s_sky_layer                    full screen — solid black + star pixels
    │                                  (artemis_main)
    │
    ├── s_moon_bitmap_layer            bottom zone, starts at s_split_y
    │   └── (s_moon_bitmap)            GBitmap resource, not a layer
    ├── s_time_layer                   bottom zone — overlaid on moon
    ├── s_date_layer                   bottom zone — overlaid on moon
    │                                  (all three: artemis_main)
    │
    ├── s_info_layer                   top zone container, 0 → s_split_y+1
    │   │                              hide/show this to toggle all info
    │   ├── s_field_label_layers[0]    slot 0 label  ┐
    │   ├── s_field_value_layers[0]    slot 0 value  │
    │   ├── s_field_label_layers[1]                  │ active slots only;
    │   ├── s_field_value_layers[1]                  │ up to 6 pairs
    │   ├── ...                                      │
    │   └── s_decorations_layer        separator lines, drawn on top of fields
    │                                  (all: artemis_info)
    │
    ├── s_event_overlay_layer          top zone — full-width event text
    │                                  independent; hidden by default
    │                                  (artemis_info)
    │
    └── s_logo_layer                   top zone — Artemis 'A' PDC vector
        └── (s_logo_pdc)               GDrawCommandImage, not a layer
                                       hidden by default (artemis_logo)
```

### Z-order notes

- `s_info_layer` sits above the moon/time/date layers. Hiding it clears the
  top zone without affecting the bottom zone.
- `s_decorations_layer` is the last child of `s_info_layer` so separator
  lines draw on top of field text.
- `s_event_overlay_layer` is a sibling of `s_info_layer` (not a child) so
  it can be shown independently — e.g. an event fires while fields are
  visible: hide `s_info_layer`, show overlay.
- `s_logo_layer` and `s_event_overlay_layer` are never visible at the same
  time. They are permanent children of root (never destroyed at runtime),
  just toggled hidden.
