# Mission data file

`active.json` is fetched directly by the watchface's phone companion at
`https://raw.githubusercontent.com/lpfabiani/pebble-artemis-watchface/main/missions/active.json`,
forwarded to the watch over Bluetooth, and cached there. The watch re-checks
once every 24 hours — editing this file and pushing to `main` is enough to
update every installed watchface; no app rebuild or store resubmission needed.

## Schema (schemaVersion 1)

| Field | Type | Meaning |
|---|---|---|
| `name` | string | Mission name shown pre-launch and post-mission, e.g. `"Artemis III"` |
| `defaultMessage` | string | Optional fallback phase text, e.g. `"Artemis III coming in 2027"`. When non-empty, it replaces the T-minus/completed-ago/next-event text entirely, any time the watch isn't showing an active mission or a special event banner. `""` (or omit) to fall back to the normal auto-generated text. Max ~35 characters. |
| `crew` | string | Two comma-separated lines, `\n`-joined, e.g. `"Wiseman, Glover\nKoch, Hansen"`. `""` omits the crew line (use before the crew is announced). |
| `launchEpoch` | number | UTC unix seconds of liftoff. Convert at https://www.unixtimestamp.com, verify with `date -d @<epoch> -u`. `0` keeps the watch on the "no mission" placeholder. |
| `endHours` | number | Nominal mission duration in hours (used as a watch-side fallback; the live telemetry API's `mission_complete` flag is authoritative when connected). |
| `stats.metS` | number | Total mission elapsed time in seconds. `0` omits this stat. |
| `stats.maxDistKm` | number | Max distance from Earth in km. `0` omits this stat. |
| `stats.maxSpeedKmh` | number | Max speed in km/h. `0` omits this stat. |
| `stats.moonDistKm` | number | Closest approach to the Moon in km. `0` omits this stat. |
| `events` | array | Timed banner events. Each fires a 5-minute (or `displayMinutes`-minute) banner on the watch face. |
| `events[].epoch` | number | UTC unix seconds when the event fires. |
| `events[].message` | string | Banner text, two lines joined with `\n`, e.g. `"CLOSEST\nTO MOON"`. Keep each line short — the watch wraps at ~18 characters. |
| `events[].displayMinutes` | number | How long the banner stays up. `0` = 5 minutes. |

## Workflow notes

- The phone only forwards **future** events (ones whose `epoch` hasn't passed
  yet), at most 5 of them — this bounds what's sent over Bluetooth and stored
  on the watch (mirrors the existing upcoming-milestone limit). Within those
  constraints it prefers events landing in the **next 48 hours**, but it always
  includes at least the single soonest future event even if it's further out
  than 48 hours — the watch always has *something* to count down to. Events
  further out than what's sent simply get picked up on a later 24h sync once
  they enter the window.
- **Outside an active mission (pre-launch or post-mission), the watch replaces
  its normal "T-minus launch" / "completed Xd ago" phase text with a countdown
  to the next scheduled event**, formatted as `<first line of message> in Xd Xh
  Xm`. For example, an event with `"message": "CREW\nANNOUNCEMENT"` would show
  as `"CREW in 2d 3h"`. Phrase the **first line** of `message` so it reads
  naturally as a standalone noun phrase before " in <countdown>" — e.g. prefer
  `"CREW\nANNOUNCEMENT"` (reads as "CREW in 2d 3h") over a first line that only
  makes sense paired with the second line.
- Set any `stats.*` field to `0` until the real value is confirmed (e.g. before
  splashdown). Each stat is independently optional — the watch shows the
  "no stats yet" screen only while *all four* are `0`, and otherwise displays
  whichever ones are non-zero.
- For a brand-new mission with no known events yet, ship `"events": []` — the
  watch falls back to its normal T-minus-launch / completed-X-ago phase text.
- `defaultMessage` takes priority over *all* of the above phase text, including
  the next-scheduled-event countdown — it's meant for cases like "we don't
  have a real launch date yet, don't show a countdown to a placeholder one."
  It only ever replaces the pre-launch/post-mission phase overlay; it never
  appears during an active mission or while a special event banner is showing.
