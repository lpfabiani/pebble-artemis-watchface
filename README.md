# Artemis II Watchface

A Pebble watchface for the NASA Artemis II mission. Shows live telemetry data fetched from the [artemis.cdnspace.ca](https://artemis.cdnspace.ca) API, with configurable data fields, color themes, and special event alerts during the lunar flyby.

**Version:** 1.2  
**Download:** [Pebble App Store](https://apps.repebble.com/71a8d7de19f04fdca2eb2c43)

---

## Features

- **Live telemetry** — speed, distance to Earth and Moon, altitude, G-force, orbital parameters, DSN signal delay, tracking station, downlink rate
- **Mission Elapsed Time** — calculated locally, updates every minute without API calls
- **Mission Phase** — current flight phase (Trans-Lunar, Lunar Orbit, etc.)
- **Next Milestone** — name and countdown to the next mission event
- **Special event banners** — full-screen alerts during the lunar flyby (Moon observation, closest approach, signal blackout, signal restored), shown for 5 minutes each with optional vibration
- **6 configurable data slots** — choose what to display in each position
- **6 color themes** — Space, Dark, Clear, B&W, NASA, and fully custom
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
- **Vibrate on special events** — vibrate when a lunar flyby event banner appears

### Color Theme
- **Space** (default) — black background, cyan accents, white values
- **Dark** — dark navy, blue accents
- **Clear** — white background, dark blue accents
- **B&W** — pure black and white
- **NASA** — dark navy with gold accents
- **Custom** — pick your own background, accent, value, and highlight colors

Color settings are not available on B&W watches (Aplite).

---

## Special Events — Lunar Flyby (Apr 6–7, 2026)

The watchface shows a full-screen banner for 5 minutes at each of these moments:

| Time (UTC) | Event |
|------------|-------|
| Apr 6, 18:45 | Moon Observation Begins |
| Apr 6, 22:47 | Behind the Moon |
| Apr 6, 23:02 | Closest to Moon |
| Apr 6, 23:05 | Max Distance from Earth |
| Apr 6, 23:27 | Signal Restored |
| Apr 7, 01:20 | Moon Observation Ends |

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
  pkjs/index.js     — phone-side JS, fetches API data
  pkjs/config.js    — Clay configuration page definition
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
