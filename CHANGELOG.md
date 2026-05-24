# Changelog

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
