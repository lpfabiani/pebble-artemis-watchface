# Changelog

## [2.2] — 2026-05-29

### Added
- New options to "Show mission info": Touch, Shake, Both, Always and Never
- Bluetooth disconnect indicator (satellite) and vibration

## [2.1] — 2026-05-28

### Added
- **Three-mode display state machine** (`DISPLAY_LOGO` / `DISPLAY_EVENT` / `DISPLAY_INFO`): logo shown by default, replaced by an event banner when a special event is active, replaced by info slots after a shake or touch.
- **Shake or Touch to reveal info**: shaking the watch shows mission telemetry slots for a configurable duration (10 / 20 / 30 / 60 s), then auto-reverts to the logo. Gated by a new phone setting.
  - **"No Artemis mission ongoing"** message: when info mode is triggered outside a mission the existing event overlay is reused to display a centred message instead of empty telemetry slots.
- **Timeline Peek support**: in Logo and Event modes the bottom zone (moon, time, date) compresses smoothly as the peek drawer slides in; in Info mode the bottom zone is hidden only while the peek is active, then restored.
- `artemis_event.c/.h` — new module owning the event overlay layer and all special-event detection logic (previously mixed into `artemis_info`).
- **Phone settings** — new "Watch Behaviour" section in the Clay config page:
  - Toggle: *Show mission info on shake* (`INFO_ON_SHAKE`, default on)
  - Select: *Info display duration* (`INFO_DISPLAY_S`: 10 / 20 / 30 / 60 s)
- **Bottom-up slot fill on round screens**: info slots are now placed pairs-first at the widest chord area (bottom of the top zone), with single-wide rows above — avoiding bezel clipping that affected the previous top-down layout.

### Changed
- `artemis_main.c` — moon, time, and date layers are now children of a single `s_time_area_layer` container; peek compression and bottom-zone hide/show require only one `layer_set_frame` / `layer_set_hidden` call.
- `artemis_info.c` — event overlay and detection logic removed; `artemis_info_refresh()` returns `void` and updates slot content only; visibility is managed exclusively by `main.c`'s state machine.
- Data refresh from phone gated on info visibility: `artemis_comms_request_data()` is only called while the info slots are shown.
- `SETTINGS_VERSION` bumped to 4 (new `info_on_shake` / `info_display_s` fields).
- `artemis_mission.h` introduced to hold mission-specific constants (`LAUNCH_EPOCH`, `MISSION_END_HOURS`, special-event table, `FIELD_LABELS[]`), keeping them out of `artemis.h`.

### Fixed
- Double `artemis_update_display()` call in `artemis_comms.c` when `ARTEMIS_COMPLETE` key was present in an incoming message.
- Tick handler guarded with `MINUTE_UNIT` check to prevent spurious per-second calls on certain firmware / emulator versions.


## [2.0] — 2026-05-27

### Added
- New UI, including:
  - Time moved from top to bottom
  - Artemis 'A' PDC vector logo and star sky in top zone when the mission. Logo scales in-place to fill the top zone and intentionally overflows ~1/6 into the moon zone so the arc at the base sits naturally over the moon image.
  - Moon photo in the bottom behind date and time
  - Artemis font being used for time and date.
  - Date format on Round screens simplified to `%a %d` (e.g. `TUE 26`) to fit the narrow chord width.
- Recognizes user preference for 12/24 hour presentation.

### Changed
- **Source architecture refactored** — monolithic `main.c` (≈1,300 lines) split into six focused modules:
  - `main.c` — entry point, display orchestrator, shared global definitions (~150 lines)
  - `artemis.h` — single shared header (debug flag, storage keys, slot counts, `FieldType` enum, Night Sky palette, font resource IDs and heights, `ArtemisSettings` / `ArtemisData` structs, extern declarations)
  - `artemis_main.c/.h` — **time and date**, sky background (Orion star field), moon bitmap
  - `artemis_logo.c/.h` — Artemis 'A' PDC vector logo with bounding-box scaling
  - `artemis_info.c/.h` — **top-zone info**: slot layout and rendering, separator decorations, special-event overlay
  - `artemis_comms.c/.h` — AppMessage phone↔watch inbox/outbox
  - `src/c/README.md` documenting source architecture, layer tree, z-ordering rules, and the display state machine.
  - Doxygen `@file` / `@brief` headers on all C and H source files.
- **Changes in code**:
  - A lot of re-do and re-organization for the previous file separation.
  - `s_info_layer` as parent container for all field layers and the decorations layer, allowing the entire top-zone info area to be shown or hidden with a single `layer_set_hidden` call.
  - Display orchestration simplified: `artemis_info_refresh()` returns `bool` (top zone occupied or not); `main.c`'s `artemis_update_display()` uses that value to show or hide the logo.
- Configuration page (`config.js`) cleaned up: platform detection block and `themeOptions` variable removed; commented-out Color Theme section deleted.

### Removed
- Configurable color themes (Space, Dark, Clear, B&W, NASA, Custom) — replaced by the fixed Night Sky palette.
- Color picker and theme-selector settings (`COLOR_BACKGROUND`, `COLOR_ACCENT`, `COLOR_VALUES`, `COLOR_HIGHLIGHTS`, `COLOR_THEME`) from both the Clay configuration page and `ArtemisSettings`.
- Battery indicator (arc on round screens, bar on rectangular screens).
- `artemis_config.h` (deleted; no references remain).

---

## [1.3] — 2026-04-07

### Added
- Dynamic milestone events: the JS sends the next 5 upcoming milestones from the API timeline to the watch. The watch stores them in flash and fires a 5-minute banner when each one occurs — even when the phone is disconnected.
- `artemis_config.h`: mission constants, special event table, field type enum, field labels, default settings values, and all data structs extracted from `main.c` into a dedicated header.

### Fixed
- Clay color pickers send values as hex strings (`"0xRRGGBB"`) — the C code was reading them as integers, causing a crash on save. Fixed with `prv_fetch_color()` which handles both types.
- Clay `select` fields send values as strings (e.g. `"1"`) — slot assignments were being read as raw ASCII bytes (e.g. `49` instead of `1`), causing an out-of-bounds crash. Fixed with `prv_fetch_int()`.

### Changed
- `FETCH_*` macros replaced with `prv_fetch_int()` and `prv_fetch_color()` helper functions — handles both integer and string tuple types from Clay.
- Special event banners now come from two sources: the hardcoded lunar flyby table (static, higher priority) and upcoming milestones from the API (dynamic, stored on-watch for offline use).


## [1.2] — 2026-04-06

### Fixed
- Configuration page no longer crashes the watchface when saving settings
- Special event banners now dismiss after 5 minutes and return to the normal data screen. Previously each event stayed visible until the next event started.

---

## [1.1] — 2026-04-05

### Added
- Support for 5 Pebble platforms: Emery, Basalt, Aplite, Chalk, Gabbro
- 6 configurable data slots (5 on smaller/round screens) — choose any telemetry field per position
- Color theme system: Space, Dark, Clear, B&W, NASA, and fully Custom themes with 4 color pickers
- Configuration page built with Clay framework (field layout, update interval, unit system, colors)
- Special event banners for the lunar flyby (Apr 6–7): Moon observation, behind the Moon, closest approach, max distance from Earth, signal restored, Moon observation ends
- Optional vibration on special event transitions
- Moon distance field
- Next milestone field showing name and countdown
- Additional telemetry fields: G-force, altitude, periapsis, apoapsis, DSN signal delay, tracking station, downlink rate
- MET displayed as `Xd Yh Zm` format
- Round-screen safe-zone separator lines

### Changed
- Layout positions adjusted for all platforms

---

## [1.0] — 2026-04-05

### Added
- Initial release
- Live telemetry from `artemis.cdnspace.ca` API: mission phase, speed, distance to Earth
- Mission Elapsed Time calculated locally from launch epoch
- Next milestone name and countdown
- 5-platform build: Emery, Basalt, Aplite, Chalk, Gabbro
- Battery indicator (arc on round, bar on rectangular)
- 30-minute data refresh with 5-minute throttle


# Possible future improvements:

- When Artemis III comes:
  - Pre-launch countdown — when current time < LAUNCH_EPOCH, INFO shows a countdown ("T−3d 14h 22m") in the top zone instead of telemetry
  - Post-mission commemorative screen — when mission_complete: DISPLAY_LOGO shows total mission stats (duration, max speed, max Earth distance) from stored ArtemisData
  - Special icon for Artemis III mission.
- Mission abstraction — extract all Artemis II–specific constants (LAUNCH_EPOCH, MISSION_END_HOURS, crew names, special events) into a single artemis_mission.c (the .h exists already); allows forking for Artemis III

