# Artemis II Watchface

A Pebble watchface for the NASA Artemis II mission. Shows live telemetry data fetched from the [artemis.cdnspace.ca](https://artemis.cdnspace.ca) API, with configurable data fields and special event alerts during the lunar flyby.

**Version:** 2.0.alpha1  
**Download:** [Pebble App Store](https://apps.repebble.com/71a8d7de19f04fdca2eb2c43)

---

## What's New in v2.0

### Night Sky redesign
The layout is completely redesigned around a fixed 60/40 vertical split:

- **Top zone (60%)** — data slots, Artemis logo (mission complete), event banners. No battery bar, no header.
- **Bottom zone (40%)** — real moon photo with time and date overlaid. Always visible.
- **Night sky background** — solid black with ~24 stars scattered across the screen, scaled to each platform.

### Single fixed color palette
Color theme selection has been removed. All screens now use the Night Sky palette:

| Role | Color |
|------|-------|
| Background / sky | Black |
| Time, data values | White |
| Labels, lines, next-event ETA | Artemis blue (#0055AA) |
| Date | Artemis red (#FF0000) |
| Moon image tint | Light gray (B&W) / photo (color) |

### Moon photo
A real Artemis II moon photo is displayed in the bottom zone, centered and clipped to fit:

| Platform | Asset |
|----------|-------|
| Aplite (B&W) | `moon-140_60_bw.png` |
| Basalt, Chalk, Emery, Gabbro | `moon-200_90_color.png` |

### App icon
A dedicated launcher icon (`artemis icon.png`) is now included and shown in the Pebble watch menu.

### Platform-optimized fonts
Fonts are now loaded per-platform rather than all at once, reducing RAM usage:

| Platform | Time font | Date & label font |
|----------|-----------|-------------------|
| Emery, Gabbro | Artemis 52 | Artemis 18 |
| Chalk | Artemis 36 | Artemis 18 |
| Basalt, Aplite | Artemis 36 | Artemis 14 |

`FONT_ARTEMIS_24` and `FONT_ARTEMIS_42` are no longer bundled.

### Removed
- Battery bar
- "ARTEMIS II" header
- Color theme selector (6 themes + custom colors)

---

## Features

- **Live telemetry** — speed, distance to Earth and Moon, altitude, G-force, orbital parameters, DSN signal delay, tracking station, downlink rate
- **Mission Elapsed Time** — calculated locally, updates every minute without API calls
- **Mission Phase** — current flight phase (Trans-Lunar, Lunar Orbit, etc.)
- **Next Milestone** — name and countdown to the next mission event
- **Special event banners** — full-screen alerts during key moments, shown for 5 minutes each with optional vibration
- **6 configurable data slots** — choose what to display in each position
- **5 Pebble platforms** — Emery, Basalt, Aplite, Chalk, Gabbro

---

## Supported Platforms

| Platform | Model | Display |
|----------|-------|---------|
| Emery | Pebble Time 2 | 200×228 color |
| Basalt | Pebble Time | 144×168 color |
| Aplite | Pebble Classic | 144×168 B&W |
| Chalk | Pebble Time Round | 180×180 color round |
| Gabbro | Pebble Time Round 2 | 260×260 color round |

---

## Data Fields

Each configurable slot can show one of the following:

| Field | Description |
|-------|-------------|
| None | Empty slot |
| Mission Elapsed Time | Days, hours, minutes since launch |
| Spacecraft Speed | km/s or mi/s |
| Distance to Earth | km or miles |
| Distance to Moon | km or miles |
| Mission Phase | Current flight phase name |
| Next Event | Upcoming milestone name + countdown |
| Crew G-Force | Current G-load on the crew |
| Altitude | Altitude above Earth or Moon |
| Closest Orbital Point | Periapsis in km or miles |
| Farthest Orbital Point | Apoapsis in km or miles |
| Signal Delay | One-way signal delay in seconds |
| Tracking Station | Active DSN station name |
| Downlink Rate | Data rate in kbps or Mbps |

---

## Settings

Open the watchface settings from the Pebble app on your phone.

### Field Layout
Choose what to display in each of the 6 positions (5 on smaller/round screens). On round screens, positions are grouped in pairs.

### Data Updates
- **Update Interval** — how often to fetch fresh data from the API (15, 30, or 60 minutes)
- **Use Miles** — display distances and speeds in imperial units
- **Vibrate on special events** — vibrate when an event banner appears

---

## Building from Source

Requires the [Rebble SDK](https://developer.rebble.io).

```bash
npm install
pebble build
pebble install --emulator emery
```

### Project Structure

```
src/
  c/main.c          — watchface C code
  c/artemis_config.h — constants, palette, settings structs
  pkjs/index.js     — phone-side JS, fetches API data
  pkjs/config.js    — Clay configuration page definition
resources/          — fonts, PDC logos, moon images, app icon
package.json        — SDK manifest and message keys
wscript             — build script
```

---

## API

Data is fetched from the community-maintained Artemis telemetry API:

- `https://artemis.cdnspace.ca/api/all` — telemetry, state vector, DSN
- `https://artemis.cdnspace.ca/api/timeline` — mission phases and milestones

Requests are throttled to at most once every 5 minutes on the phone side.

---

## License

MIT
