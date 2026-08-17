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
| `launchEpoch` | number or string | Liftoff time. Either a raw UTC unix-seconds number, a human-readable date string (see "Date formats" below), or `"TBD"` / `"TBD <descriptor>"` for a launch that isn't scheduled yet — see "Undated (TBD) events" below. `0` (or omit) keeps the watch on the "no mission" placeholder. |
| `endHours` | number | Nominal mission duration in hours (used as a watch-side fallback; the live telemetry API's `mission_complete` flag is authoritative when connected). |
| `stats.metS` | number | Total mission elapsed time in seconds. `0` omits this stat. |
| `stats.maxDistKm` | number | Max distance from Earth in km. `0` omits this stat. |
| `stats.maxSpeedKmh` | number | Max speed in km/h. `0` omits this stat. |
| `stats.moonDistKm` | number | Closest approach to the Moon in km. `0` omits this stat. |
| `events` | array | Timed banner events. Each fires a 5-minute (or `displayMinutes`-minute) banner on the watch face. |
| `events[].epoch` | number, string, or `"TBD"` | UTC unix seconds when the event fires, a human-readable date string (see "Date formats" below), or the literal string `"TBD"` for a milestone that hasn't been officially dated yet — see "Undated (TBD) events" below. |
| `events[].message` | string | Banner text, two lines joined with `\n`, e.g. `"CLOSEST\nTO MOON"`. Keep each line short — the watch wraps at ~18 characters. |
| `events[].displayMinutes` | number | How long the banner stays up. `0` = 5 minutes. |

## Date formats

`launchEpoch` and `events[].epoch` accept either a raw unix-seconds number
(verify with `date -d @<epoch> -u`), or a human-readable date string,
converted to epoch by the phone companion before it's forwarded to the watch:

- `"2026-04-06 19:27"` or `"2026-04-06T19:27"` / `"2026-04-06T19:27:00"` —
  ISO-style date(+time). With **no** timezone marker, this is assumed to be
  **US Eastern time** (the timezone NASA's own launch schedules and press
  materials use), with EST/EDT picked automatically for the date given.
- `"2026-04-06"` — date only, midnight Eastern.
- `"June 5, 2026 2:00 PM"` — looser formats also work, same Eastern
  assumption when no timezone is stated.
- `"2026-06-05T18:00:00Z"` or anything containing an explicit `UTC`/`GMT` or
  a `+HH:MM`/`-HH:MM` offset — parsed exactly as stated, overriding the
  Eastern-time default.
- A plain number is still always UTC unix seconds, unchanged.

An unparseable string is dropped (an event is skipped; `launchEpoch` falls
back to `0`) and logged by the phone app.

## Undated (TBD) events

Set `events[].epoch` to the literal string `"TBD"` for a milestone that's
known to happen (e.g. "landing") but doesn't have an official date yet —
typically the whole event list for a mission that hasn't launched or even
been scheduled. The phone companion drops `"TBD"` events silently (unlike a
genuinely unparseable date, this isn't logged as an error) — they're never
forwarded to the watch and never count toward the next-scheduled-event
countdown. Once a real date is confirmed, replace `"TBD"` with an actual
epoch or date string and the event starts being forwarded normally.

`launchEpoch` accepts the same `"TBD"` — the watch is kept on its normal
"no mission" pre-launch state (same as `launchEpoch: 0`). Optionally add a
rough descriptor after it, e.g. `"TBD 2027"` or `"TBD late 2026"`: the phone
then sends `"Upcoming in <descriptor>"` (e.g. `"Upcoming in 2027"`) as the
phase text — **overriding `defaultMessage`** for as long as the launch stays
undated, since it's the more specific, schedule-derived signal. Plain
`"TBD"` with no descriptor falls back to `defaultMessage` as usual.

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
- `defaultMessage` takes priority over the next-scheduled-event countdown and
  the normal T-minus/completed-ago text — it's meant for cases like "we don't
  have a real launch date yet, don't show a countdown to a placeholder one."
  It only ever replaces the pre-launch/post-mission phase overlay; it never
  appears during an active mission or while a special event banner is showing.
  A `launchEpoch: "TBD <descriptor>"` takes priority over even
  `defaultMessage` (see "Undated (TBD) events" above), since it's more
  specific to the current schedule state.
