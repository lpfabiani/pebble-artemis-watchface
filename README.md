# Artemis II Watchface

A Pebble watchface for the NASA Artemis II mission. Shows live telemetry data fetched from the [artemis.cdnspace.ca](https://artemis.cdnspace.ca) API, with configurable data fields and special event alerts during the lunar flyby.

**Version:** 2.0
**Download:** [Pebble App Store](https://apps.repebble.com/71a8d7de19f04fdca2eb2c43)

---

## What's New in v2.0

### Total redesign
The layout is completely redesigned including:

- Official Artemis Logo in SVG: Scales to future screen sizes (https://commons.wikimedia.org/wiki/File:Artemis_Logo_Color_Reverse_RGB_(905749837641).svg)
- Official Artemis Inter font being used in date and time (https://www.flumpstudio.com/projects/nasa-artemis-syz9t)
- The iconic "A New View of the Moon" photo (https://www.nasa.gov/image-detail/amf-art002e009287/
- Color theme selection has been removed. All screens now use the Night Sky palette for clarity and consistency.

### App icon
A dedicated launcher icon (`artemis icon.png`) is now included and shown in the Pebble watch menu.

### Removed
- Battery bar
- "ARTEMIS II" header
- Color theme selector (6 themes + custom colors)

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

**No information is being shown at this moment, as there's no ongoing mission. It will be updated soon.**

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
src/c/          — watchface C code
src/pkjs        — phone-side JS, fetches API data
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
