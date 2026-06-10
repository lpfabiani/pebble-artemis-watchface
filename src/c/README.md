# Artemis Watchface — Source Architecture

## File Structure

```
src/c/
├── README.md             this file
│
├── artemis.h             single shared header every module includes. Contains:
│                         debug flag, storage keys, slot counts, DisplayMode
│                         and FieldType enums, Night Sky color palette,
│                         platform font resource IDs and heights,
│                         ArtemisSettings / ArtemisData structs, extern
│                         declarations for all shared globals, and prototypes
│                         for overlay_geometry(), artemis_update_display(),
│                         and artemis_apply_interaction_settings().
│                         Mission-specific constants are NOT here — see
│                         artemis_mission.h.
│
├── artemis_mission.h     single file to swap when porting to a new mission.
│                         Contains:
│                           MISSION_NAME      display name ("Artemis II")
│                           MISSION_CREW      two-line crew string
│                           LAUNCH_EPOCH      UTC unix timestamp of launch
│                           MISSION_END_HOURS nominal mission duration
│                           MISSION_STATS_*   post-mission stats (0 = unknown)
│                           SPECIAL_EVENTS_INIT  hardcoded event table macro
│                         Included by: artemis_event.c, artemis_info.c, main.c
│
├── artemis_comms.c/.h    phone ↔ watch AppMessage: inbox/outbox handlers,
│                         data request, Clay settings sync. Calls
│                         artemis_apply_interaction_settings() after settings
│                         change.
│
├── artemis_event.c/.h    reusable event overlay — instance-based centered
│                         text banner.
│                         Struct: ArtemisEventOverlay { layer, last_msg }
│                         API:
│                           artemis_event_create(parent) → overlay*
│                             heap-allocates struct; creates TextLayer as
│                             child of parent, hidden by default.
│                           artemis_event_destroy(ev)
│                           artemis_event_show(ev, msg, vibrate)
│                             measures msg with
│                             graphics_text_layout_get_content_size,
│                             reframes the layer so the text block is
│                             vertically centred in the top zone, sets text,
│                             shows layer. Vibrates if msg != last_msg and
│                             vibrate is true.
│                           artemis_event_hide(ev)
│                           artemis_event_check() → const char*  [free fn]
│                             pure: queries SPECIAL_EVENTS table and
│                             s_artemis.upcoming; returns active msg or NULL.
│                         Two instances in use:
│                           main.c       → s_event_overlay  (NASA events)
│                           artemis_info → s_phase_overlay  (phase text)
│
├── artemis_info.c/.h     top-zone info display: phase-aware slot rendering
│                         or phase text overlay.
│                         Phase detection (MissionPhase enum):
│                           PRELAUNCH  now < LAUNCH_EPOCH
│                           ACTIVE     launched, !mission_complete, within
│                                      MISSION_END_HOURS
│                           COMPLETE   mission_complete flag OR past
│                                      MISSION_END_HOURS (watch-side fallback)
│                         Create behaviour:
│                           ACTIVE     → slot TextLayers + s_decorations_layer
│                           non-active → s_phase_overlay only (no slot allocs)
│                         Refresh behaviour:
│                           ACTIVE     → render slot label/value pairs
│                           non-active → call artemis_event_show on
│                                        s_phase_overlay with phase text:
│                                        PRELAUNCH: name + crew + T-minus
│                                        COMPLETE:  name + crew + stats +
│                                                   "completed Xd Xh ago"
│                         Owns: s_info_layer, s_phase_overlay,
│                               s_decorations_layer (ACTIVE only),
│                               s_field_*_layers (ACTIVE only),
│                               s_active_slots, s_num_active, slot buffers.
│                         API: artemis_info_create(root)
│                              artemis_info_destroy()
│                              artemis_info_show() / artemis_info_hide()
│                              artemis_info_refresh()
│                                routes to slots or phase overlay; lazy-
│                                creates s_phase_overlay if mission completes
│                                while the watchface is running.
│                              artemis_info_rebuild_slots()
│                                no-op outside ACTIVE phase.
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
├── artemis_clock.c/.h    always-visible background and bottom zone.
│                         sky background (black fill + star field) +
│                         s_time_area_layer container (moon bitmap, time,
│                         date). Container allows peek and DISPLAY_INFO
│                         hide via a single layer call.
│                         Owns: s_sky_layer, s_time_area_layer,
│                               s_moon_bitmap_layer, s_moon_bitmap,
│                               s_time_layer, s_date_layer,
│                               s_satellite_layer (Bluetooth indicator).
│                         API: artemis_clock_create(root)
│                              artemis_clock_destroy()
│                              artemis_clock_show() / artemis_clock_hide()
│                              artemis_clock_peek(unobstructed_h)
│                              artemis_clock_refresh()  ← called every tick
│                              artemis_clock_set_bluetooth_status(connected)
│
└── main.c                init, deinit, window load/unload, tick handler,
                          display state machine (DISPLAY_LOGO / EVENT / INFO),
                          shake/touch handlers + info timer, Bluetooth handler,
                          Timeline Peek callbacks,
                          artemis_apply_interaction_settings().
                          Owns: s_main_window, s_root_layer, s_root_w/h,
                                s_split_y, s_settings, s_artemis,
                                s_event_overlay (ArtemisEventOverlay*).
                          No mission phase awareness — phase logic lives
                          entirely in artemis_info.c.
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
| `s_font_event` | `GFont` | custom font for event/slot labels |

`overlay_geometry(*top, *h)` is declared in `artemis.h` (defined in `main.c`)
because it is needed by `artemis_event` and `artemis_logo` to compute the
top-zone centering rectangle.

`artemis_apply_interaction_settings()` is also declared in `artemis.h`
(defined in `main.c`) so `artemis_comms.c` can call it after a settings
change without a circular dependency.

---

## Module API Contract

Every display module exposes the same four operations:

- **`create(root)`** — allocate layers and add them to the layer tree.
  Sets up update_procs. Does not make layers visible.
- **`destroy()`** — free all owned layers and resources.
- **`show()` / `hide()`** — `layer_set_hidden` only. No content update.
- **`refresh()`** — update content. Does not change visibility.

`artemis_event` differs: it is instance-based (`ArtemisEventOverlay*`) and
has no `refresh()`. `artemis_event_show(ev, msg, vibrate)` combines update
and show in one call. `artemis_event_check()` is a free function with no
associated instance.

`artemis_info_refresh()` is phase-aware and routes to one of three paths:
ACTIVE → renders live telemetry slots every tick; COMPLETE with stats →
calls `prv_render_stats()` once (guarded by `s_stats_rendered`), then
no-ops on subsequent ticks; PRELAUNCH / COMPLETE without stats → calls
`artemis_event_show` on `s_phase_overlay` every tick (T-minus and "ago"
counters update). Callers (`main.c`) do not need to know which path ran.

A mid-session ACTIVE → COMPLETE transition is detected in `artemis_info_refresh()`:
slot layers are destroyed and rebuilt for the stat count before the first render.

---

## Display State Machine (main.c — artemis_update_display)

Three modes, evaluated every tick and on every relevant event:

| Mode | Trigger | Top zone | Bottom zone |
|---|---|---|---|
| `DISPLAY_LOGO` | default | logo visible | normal |
| `DISPLAY_EVENT` | special event active | NASA event banner | normal |
| `DISPLAY_INFO` | shake/touch (if enabled) | phase-aware info | hidden |

```
artemis_event_check() ──► event_msg?
  yes AND mode ≠ INFO ──► DISPLAY_EVENT
  no  AND mode ≠ INFO ──► DISPLAY_LOGO
  (mode == INFO) ──────► stay INFO until timer fires → DISPLAY_LOGO
```

Events override logo but **never interrupt info** (info is "sticky").

### DISPLAY_INFO — phase-aware content

What the user sees when they shake depends on mission phase. Phase is computed
watch-side from `LAUNCH_EPOCH` and `MISSION_END_HOURS` in `artemis_mission.h`
plus the `mission_complete` flag from the phone (authoritative). No phone
connection needed for any phase transition.

#### Pre-launch (`PRELAUNCH`)

Centered text block rendered by `s_phase_overlay` (`ArtemisEventOverlay`).
No slot layers are allocated.

```
Artemis II              ← MISSION_NAME
Wiseman, Glover         ← MISSION_CREW line 1  (omitted if MISSION_CREW = "")
Koch, Hansen            ← MISSION_CREW line 2
T-9d 10h 30m           ← time until LAUNCH_EPOCH
  or T-2h 15m          ←   (days omitted when < 1 day)
  or T-45m             ←   (hours omitted when < 1 hour)
```

#### During mission (`ACTIVE`)

Slot layers (`s_field_label_layers` / `s_field_value_layers`) created for each
configured slot. Count comes from user settings (up to 6, up to 5 on small/round
screens). Separator lines + column dividers drawn by `s_decorations_layer`.

Each slot shows a **label** (accent color) and a **value** (white). Available
fields, selected per slot in the phone settings:

| Label | Field | Value format |
|---|---|---|
| `MET` | Mission Elapsed Time | `Xd Xh Xm` |
| `SPEED` | Spacecraft speed | `X.XX km/s` or `mi/s` |
| `EARTH` | Distance to Earth | `X,XXX km` or `mi` |
| `MOON` | Distance to Moon | `X,XXX km` or `mi` |
| `PHASE` | Mission phase string | text from phone |
| *(milestone name)* | Next milestone countdown | `in Xd Xh` / `in Xh Xm` / `in Xm` / `passed` |
| `G-FORCE` | G-force | `X.XXXX g` |
| `ALTITUDE` | Altitude above Earth | `X,XXX km` or `mi` |
| `PERIAPSIS` | Periapsis altitude | `X km` or `mi` |
| `APOAPSIS` | Apoapsis altitude | `X,XXX km` or `mi` |
| `SIGNAL` | DSN signal delay | `X.XX s` |
| `STATION` | DSN tracking station | text from phone |
| `DOWNLINK` | Downlink rate | `X kbps` or `X.X Mbps` |

`NEXT EVENT` uses the milestone name from the phone as its label (accent color)
and the countdown as its value (also accent color).

#### Post-mission with stats (`COMPLETE`, `MISSION_STATS_*` > 0)

Slot layers created for the mission stats defined in `artemis_mission.h`. Count
is the number of non-zero `MISSION_STATS_*` constants (0–4). Same slot geometry
and decorations as ACTIVE, but content is static — rendered once and never
re-drawn until a settings change (km/mi unit) triggers a refresh.

| Label | Stat constant | Value format |
|---|---|---|
| `MET` | `MISSION_STATS_MET_S` | `Xd Xh` |
| `EARTH` | `MISSION_STATS_MAX_DIST_KM` | `X,XXX km` or `mi` (max distance from Earth) |
| `SPEED` | `MISSION_STATS_MAX_SPEED_KMH` | `X,XXX km/h` or `mph` (peak speed) |
| `MOON` | `MISSION_STATS_MOON_DIST_KM` | `X,XXX km` or `mi` (closest approach, from surface) |

Any stat set to 0 (or left commented out in `artemis_mission.h`) is omitted;
the slot layout adjusts to the remaining count.

#### Post-mission without stats (`COMPLETE`, all `MISSION_STATS_*` = 0)

Centered text block rendered by `s_phase_overlay`. No slot layers allocated.

```
Artemis II              ← MISSION_NAME
completed 30d 12h ago   ← elapsed since LAUNCH_EPOCH + MISSION_END_HOURS (< 30 days)
  or completed 45d ago  ←   (hours omitted after 30 days)
  or completed 3h ago   ←   (days omitted in the first hours, edge case)
```

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
    │   │                              one layer_set_frame/hidden controls all
    │   ├── s_moon_bitmap_layer        Y=0 relative — clips when compressed
    │   │   └── (s_moon_bitmap)        GBitmap resource, not a layer
    │   ├── s_time_layer               vertically centred in container
    │   ├── s_date_layer               below time layer
    │   └── s_satellite_layer          Bluetooth status indicator (top-right)
    │                                  (all: artemis_clock)
    │
    ├── s_info_layer                   top zone container, 0 → s_split_y+1
    │   │                              hidden by default; shown in DISPLAY_INFO
    │   │
    │   │   ── ACTIVE mission ──────────────────────────────────────────────
    │   ├── s_field_label_layers[0]    slot 0 label  ┐
    │   ├── s_field_value_layers[0]    slot 0 value  │ count from user settings
    │   ├── ...                                      │ up to 6 pairs
    │   ├── s_field_label_layers[N]                  │ (5 on small/round)
    │   ├── s_field_value_layers[N]                  ┘
    │   └── s_decorations_layer        separator lines, drawn on top of fields
    │                                  (artemis_info — ACTIVE only)
    │
    │   ── POST-MISSION with stats ───────────────────────────────────────
    │   ├── s_field_label_layers[0]    stat 0 label  ┐
    │   ├── s_field_value_layers[0]    stat 0 value  │ count = non-zero
    │   ├── ...                                      │ MISSION_STATS_* (0–4)
    │   ├── s_field_label_layers[N]                  │ static; rendered once
    │   ├── s_field_value_layers[N]                  ┘
    │   └── s_decorations_layer        same geometry as ACTIVE
    │                                  (artemis_info — COMPLETE+stats only)
    │
    │   ── PRE-LAUNCH or POST-MISSION without stats ───────────────────────
    │   └── s_phase_overlay            ArtemisEventOverlay* — centered text
    │       └── (TextLayer)            name+crew+T-minus  or  name+"completed ago"
    │                                  (artemis_info — no slot layers allocated)
    │
    ├── s_event_overlay                ArtemisEventOverlay* — NASA event banner
    │   └── (TextLayer)                hidden by default; shown in DISPLAY_EVENT
    │                                  (main.c owns pointer; layer parented to
    │                                   root by artemis_event_create)
    │
    └── s_logo_layer                   top zone — Artemis 'A' PDC vector
        └── (s_logo_pdc)               GDrawCommandImage, not a layer
                                       shown by default; hidden in EVENT/INFO
                                       (artemis_logo)
```

### Z-order notes

- `s_time_area_layer` is a container — hiding or reframing it affects moon,
  time, date, and satellite indicator in one call.
- `s_info_layer` sits above `s_time_area_layer`. Its children depend on phase:
  ACTIVE → slot layers + decorations (count from user settings);
  COMPLETE with stats → slot layers + decorations (count from non-zero
  `MISSION_STATS_*`, 0–4, rendered once); PRELAUNCH or COMPLETE without stats
  → `s_phase_overlay` only (no slot layers allocated, saves ~12 TextLayer allocs).
- `s_decorations_layer` is the last child of `s_info_layer` (ACTIVE only) so
  separator lines draw on top of field text.
- `s_event_overlay` (owned by `main.c`, created by `artemis_event_create`) is
  a sibling of `s_info_layer`, not a child — shown independently, always above
  info content.
- Exactly one of `s_logo_layer`, `s_event_overlay`, `s_info_layer` is visible
  at any time — enforced by the state machine in `main.c`.
- `s_phase_overlay` (inside `s_info_layer`) and `s_event_overlay` (root-level)
  are both `ArtemisEventOverlay` instances using the same rendering code
  (`artemis_event_show`), but serve different contexts and are never shown
  simultaneously.
