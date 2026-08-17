/* Artemis II Watchface — Phone-Side JS */
/* Uses https://artemis.cdnspace.ca live telemetry API */

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

// ─── Debug logging ────────────────────────────────────────────────────────────
var DEBUG_ENABLED = false;

// ─── Constants ────────────────────────────────────────────────────────────────
var API_ALL_URL   = 'https://artemis.cdnspace.ca/api/all';
var TIMELINE_URL  = 'https://artemis.cdnspace.ca/api/timeline';
var MISSION_URL   = 'https://raw.githubusercontent.com/lpfabiani/pebble-artemis-watchface/main/missions/active.json';

// Throttle: don't re-fetch if we fetched in last 5 minutes
var THROTTLE_MS = 5 * 60 * 1000;

// Mission-data throttle: the watch already paces requests to once every 24h
// (see MISSION_SYNC_INTERVAL_S in main.c); this is just a safety net against
// duplicate REQUEST_MISSION messages (e.g. watch reboot mid-cycle).
var MISSION_THROTTLE_MS    = 23 * 60 * 60 * 1000;
var MISSION_EVENT_WINDOW_MS = 48 * 60 * 60 * 1000;
var MAX_MISSION_EVENTS     = 5;

// Artemis II hardcoded fallback — used only when /api/all fails after mission end
var MAX_UPCOMING = 5;

// For Artemis II
var LAUNCH_EPOCH_MS = 1775082900000;  // Apr 1 2026 22:35 UTC in ms
var MISSION_END_MS = LAUNCH_EPOCH_MS + 229 * 60 * 60 * 1000;

// ─── Helpers ──────────────────────────────────────────────────────────────────
function xhrRequest(url, callback, errorCallback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    if (this.status >= 200 && this.status < 300) {
      callback(this.responseText);
    } else {
      console.log('XHR error ' + this.status + ' for ' + url);
      if (errorCallback) errorCallback('HTTP ' + this.status);
    }
  };
  xhr.onerror = function() {
    console.log('XHR network error for ' + url);
    if (errorCallback) errorCallback('network error');
  };
  xhr.open('GET', url);
  xhr.send();
}

function getCurrentPhase(phases, metMs) {
  if (!phases || phases.length === 0) return '...';
  for (var i = 0; i < phases.length; i++) {
    var p = phases[i];
    if (metMs >= p.startMetMs && metMs < p.endMetMs) return p.phase;
  }
  return phases[phases.length - 1].phase;
}

function getNextMilestone(milestones, metMs) {
  if (!milestones || milestones.length === 0) return null;
  var next = null;
  for (var i = 0; i < milestones.length; i++) {
    var m = milestones[i];
    if (m.metMs > metMs && (!next || m.metMs < next.metMs)) next = m;
  }
  return next;
}

// Returns the next N milestones after metMs, sorted by ascending metMs.
function getUpcomingMilestones(milestones, metMs) {
  if (!milestones || milestones.length === 0) return [];
  var upcoming = [];
  for (var i = 0; i < milestones.length; i++) {
    if (milestones[i].metMs > metMs) upcoming.push(milestones[i]);
  }
  upcoming.sort(function(a, b) { return a.metMs - b.metMs; });
  return upcoming.slice(0, MAX_UPCOMING);
}

// Shorten milestone name to ≤18 chars at word boundary
function shortenMilestone(name) {
  if (!name || name.length <= 18) return name;
  var truncated = name.substring(0, 18);
  var lastSpace = truncated.lastIndexOf(' ');
  return (lastSpace > 6 ? truncated.substring(0, lastSpace) : truncated).trim() + '.';
}

function safeInt(val, multiplier) {
  if (typeof val !== 'number' || isNaN(val)) return 0;
  return Math.round(val * (multiplier || 1));
}

// ─── Human-readable mission dates ─────────────────────────────────────────────
// missions/*.json's launchEpoch and events[].epoch may be written as either a
// raw unix-seconds number (unchanged) or a human-readable date string. Strings
// with no explicit timezone marker are assumed to be US Eastern time (Kennedy
// Space Center) — that's the timezone NASA's own launch schedules and press
// materials use — with automatic EST/EDT selection per the current US DST rule
// (2nd Sunday of March 2am -> 1st Sunday of November 2am).
var TZ_MARKER_RE = /(Z|[+-]\d{2}:?\d{2})$|\bUTC\b|\bGMT\b/i;
var TBD_RE = /^TBD(?:\s+(.+))?$/i;
var ISO_DATE_RE = /^(\d{4})-(\d{2})-(\d{2})$/;
var ISO_DATETIME_RE = /^(\d{4})-(\d{2})-(\d{2})[T ](\d{2}):(\d{2})(?::(\d{2}))?$/;

// nth Sunday (1-indexed) of a given UTC month. Used only to evaluate the US
// DST rule against wall-clock numbers, never against a real timezone.
function nthSundayUtc(year, month0, n) {
  var first = new Date(Date.UTC(year, month0, 1));
  var firstSunday = 1 + ((7 - first.getUTCDay()) % 7);
  return firstSunday + (n - 1) * 7;
}

// True if the given Eastern-local wall-clock moment falls within US DST
// (EDT, UTC-4); false means standard time (EST, UTC-5).
function isEasternDst(year, month0, day, hour) {
  if (month0 < 2 || month0 > 10) return false;      // Jan, Feb, Dec
  if (month0 > 2 && month0 < 10) return true;        // Apr - Oct
  if (month0 === 2) {                                 // March
    var startDay = nthSundayUtc(year, 2, 2);
    return day > startDay || (day === startDay && hour >= 2);
  }
  var endDay = nthSundayUtc(year, 10, 1);             // November
  return day < endDay || (day === endDay && hour < 2);
}

// Converts Eastern-local wall-clock numbers (as typed, no timezone attached)
// to a UTC unix-seconds epoch.
function easternWallClockToEpoch(year, month0, day, hour, minute, second) {
  var offsetHours = isEasternDst(year, month0, day, hour) ? 4 : 5;
  var ms = Date.UTC(year, month0, day, hour, minute, second || 0) + offsetHours * 3600000;
  return Math.floor(ms / 1000);
}

// Accepts a unix-seconds number (passthrough) or a date string. Strings with
// an explicit timezone marker are parsed as-is; strings without one have
// their wall-clock numbers extracted (via ISO regex, or by round-tripping a
// loose string through Date's local getters to cancel out the phone's own
// timezone) and are then treated as Eastern time. Returns null if the value
// can't be parsed.
function toEpochSeconds(value) {
  if (typeof value === 'number') return isNaN(value) ? null : Math.floor(value);
  if (typeof value !== 'string' || !value.trim()) return null;
  var str = value.trim();

  if (TZ_MARKER_RE.test(str)) {
    var ms = Date.parse(str);
    return isNaN(ms) ? null : Math.floor(ms / 1000);
  }

  var m = ISO_DATETIME_RE.exec(str);
  if (m) {
    return easternWallClockToEpoch(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +(m[6] || 0));
  }
  m = ISO_DATE_RE.exec(str);
  if (m) {
    return easternWallClockToEpoch(+m[1], +m[2] - 1, +m[3], 0, 0, 0);
  }

  // Loose, non-ISO format (e.g. "June 15 2027 1:00 PM"): parse once, then
  // read back the LOCAL fields. Since the parse and the read both use the
  // phone's own timezone, this round trip recovers the plain wall-clock
  // numbers as typed, regardless of what timezone the phone is actually in.
  var d = new Date(str);
  if (isNaN(d.getTime())) return null;
  return easternWallClockToEpoch(
    d.getFullYear(), d.getMonth(), d.getDate(),
    d.getHours(), d.getMinutes(), d.getSeconds()
  );
}

// ─── Main fetch function ──────────────────────────────────────────────────────
function fetchArtemisData() {
  // Throttle check
  var lastFetch = parseInt(localStorage.getItem('lastArtemisF') || '0', 10);
  if (Date.now() - lastFetch < THROTTLE_MS) {
    console.log('Throttled: ' + Math.round((Date.now() - lastFetch) / 1000) + 's since last fetch');
    return;
  }

  console.log('Fetching /api/all...');

  xhrRequest(API_ALL_URL, function(allText) {
    var all;
    try { all = JSON.parse(allText); } catch(e) {
      console.log('api/all parse failed: ' + e.message);
      return;
    }

    // ── Extract telemetry fields ───────────────────────────────────────────
    var telemetry   = all.telemetry   || {};
    var stateVector = all.stateVector || {};
    var dsn         = all.dsn         || {};

    var metMs      = telemetry.metMs        || stateVector.metMs || 0;
    var speedKmS   = telemetry.speedKmS     || 0;
    var distKm     = telemetry.earthDistKm  || 0;
    var moonDistKm = telemetry.moonDistKm   || 0;
    var altitudeKm = telemetry.altitudeKm   || 0;
    var periKm     = telemetry.periapsisKm  || 0;
    var apoKm      = telemetry.apoapsisKm   || 0;
    var gForce     = telemetry.gForce       || 0;

    // DSN fields (first active dish)
    var dishes      = dsn.dishes || [];
    var activeDish  = null;
    for (var i = 0; i < dishes.length; i++) {
      if (dishes[i].downlinkActive) { activeDish = dishes[i]; break; }
    }
    if (!activeDish && dishes.length > 0) activeDish = dishes[0];

    var stationName  = activeDish ? (activeDish.stationName || '') : '';
    var downlinkKbps = activeDish ? Math.round((activeDish.downlinkRate || 0) / 1000) : 0;
    var rtltSec      = activeDish ? (activeDish.rtltSeconds || 0) : 0;

    // ── Fetch timeline for phase + milestone ──────────────────────────────
    xhrRequest(TIMELINE_URL, function(timelineText) {
      var timeline;
      try { timeline = JSON.parse(timelineText); } catch(e) {
        timeline = { phases: [], milestones: [] };
      }

      var phases     = timeline.phases     || [];
      var milestones = timeline.milestones || [];

      var currentPhase = getCurrentPhase(phases, metMs);
      var missionComplete = (metMs < 0) || (speedKmS === 0) || (currentPhase === 'EDL');

      if (DEBUG_ENABLED) {
        console.log('=== ARTEMIS DATA DEBUG ===');
        console.log('metMs: ' + metMs);
        console.log('speedKmS: ' + speedKmS);
        console.log('phase: ' + currentPhase);
        console.log('metMs < 0: ' + (metMs < 0));
        console.log('speedKmS === 0: ' + (speedKmS === 0));
        console.log('phase === EDL: ' + (currentPhase === 'EDL'));
        console.log('missionComplete: ' + missionComplete);
        console.log('===========================');
      }

      var phase = missionComplete
        ? (phases.length ? phases[phases.length - 1].phase : 'Complete')
        : currentPhase;

      var nextMs         = missionComplete ? null : getNextMilestone(milestones, metMs);
      var milestoneName  = nextMs ? shortenMilestone(nextMs.name) : 'Mission Complete';
      var milestoneMetMs = nextMs ? Math.min(nextMs.metMs, 2147483647) : -1;

      // Pack next 5 upcoming milestones for offline event detection on the watch
      var upcoming = missionComplete ? [] : getUpcomingMilestones(milestones, metMs);
      var msMsg = {};
      for (var i = 0; i < MAX_UPCOMING; i++) {
        var ms = upcoming[i];
        msMsg['ARTEMIS_MS' + i + '_NAME']  = ms ? shortenMilestone(ms.name) : '';
        msMsg['ARTEMIS_MS' + i + '_EPOCH'] = ms
          ? Math.min(Math.round((LAUNCH_EPOCH_MS + ms.metMs) / 1000), 2147483647)
          : 0;
      }

      localStorage.setItem('lastArtemisF', String(Date.now()));

      console.log('Phase: ' + phase + ' Speed: ' + speedKmS.toFixed(2) +
                  ' Earth: ' + Math.round(distKm) + ' Moon: ' + Math.round(moonDistKm) +
                  ' Next: ' + milestoneName + ' Upcoming: ' + upcoming.length);

      Pebble.sendAppMessage(Object.assign({
        'ARTEMIS_PHASE':          phase,
        'ARTEMIS_SPEED':          safeInt(speedKmS, 100),
        'ARTEMIS_DISTANCE':       safeInt(distKm),
        'ARTEMIS_MOON_DIST':      safeInt(moonDistKm),
        'ARTEMIS_MILESTONE_NAME': milestoneName,
        'ARTEMIS_MILESTONE_MET':  milestoneMetMs,
        'ARTEMIS_COMPLETE':       missionComplete ? 1 : 0,
        'ARTEMIS_G_FORCE':        safeInt(gForce, 10000),
        'ARTEMIS_ALTITUDE':       safeInt(altitudeKm),
        'ARTEMIS_PERIAPSIS':      safeInt(periKm),
        'ARTEMIS_APOAPSIS':       safeInt(apoKm),
        'ARTEMIS_SIGNAL':         safeInt(rtltSec, 100),
        'ARTEMIS_STATION':        stationName.substring(0, 19),
        'ARTEMIS_DOWNLINK':       downlinkKbps
      }, msMsg),
      function() { console.log('Sent OK'); },
      function(e) { console.log('Send failed: ' + JSON.stringify(e)); }
      );

    }, function(err) {
      // Timeline failed — send without phase/milestone
      localStorage.setItem('lastArtemisF', String(Date.now()));
      Pebble.sendAppMessage({
        'ARTEMIS_PHASE':     '...',
        'ARTEMIS_SPEED':     safeInt(speedKmS, 100),
        'ARTEMIS_DISTANCE':  safeInt(distKm),
        'ARTEMIS_MOON_DIST': safeInt(moonDistKm),
        'ARTEMIS_COMPLETE':  missionComplete ? 1 : 0,
        'ARTEMIS_G_FORCE':   safeInt(gForce, 10000),
        'ARTEMIS_ALTITUDE':  safeInt(altitudeKm),
        'ARTEMIS_PERIAPSIS': safeInt(periKm),
        'ARTEMIS_APOAPSIS':  safeInt(apoKm),
        'ARTEMIS_SIGNAL':    safeInt(rtltSec, 100),
        'ARTEMIS_STATION':   stationName.substring(0, 19),
        'ARTEMIS_DOWNLINK':  downlinkKbps
      },
      function() { console.log('Sent (no timeline) OK'); },
      function(e) { console.log('Send failed: ' + JSON.stringify(e)); }
      );
    });

  }, function(err) {
    console.log('api/all fetch error: ' + err);
    localStorage.setItem('lastArtemisF', String(Date.now()));
    if (Date.now() >= MISSION_END_MS) {
      console.log('API error after mission end — sending COMPLETE');
      Pebble.sendAppMessage({ 'ARTEMIS_COMPLETE': 1 },
        function() { console.log('Sent COMPLETE OK'); },
        function(e) { console.log('Send COMPLETE failed: ' + JSON.stringify(e)); }
      );
    }
  });
}

// ─── Mission data fetch ───────────────────────────────────────────────────────
// Fetches missions/active.json, keeps only events landing in the next 48h
// (soonest MAX_MISSION_EVENTS first — mirrors getUpcomingMilestones), and
// forwards everything to the watch in one AppMessage. MISSION_SYNCED=1 is
// always sent last so the watch can tell a complete batch arrived before
// persisting (see artemis_comms.c inbox handler).
function fetchMissionData() {
  var lastFetch = parseInt(localStorage.getItem('lastMissionF') || '0', 10);
  if (Date.now() - lastFetch < MISSION_THROTTLE_MS) {
    console.log('Mission fetch throttled: ' + Math.round((Date.now() - lastFetch) / 1000) + 's since last');
    return;
  }

  console.log('Fetching mission data...');
  xhrRequest(MISSION_URL, function(text) {
    var mission;
    try { mission = JSON.parse(text); } catch (e) {
      console.log('mission JSON parse failed: ' + e.message);
      return;
    }

    localStorage.setItem('lastMissionF', String(Date.now()));

    var stats  = mission.stats  || {};
    var events = mission.events || [];
    var now    = Date.now();
    var windowEnd = now + MISSION_EVENT_WINDOW_MS;

    // launchEpoch: "TBD" (optionally followed by a rough descriptor, e.g.
    // "TBD 2027") means the launch isn't scheduled yet. A descriptor
    // overrides the phone-sent default message with "Upcoming in <it>" —
    // taking priority over missions/active.json's own defaultMessage, since
    // it's the more specific, schedule-derived signal.
    var launchEpoch = 0;
    var upcomingMsg = null;
    var launchRaw = mission.launchEpoch;
    var tbdMatch = (typeof launchRaw === 'string') ? TBD_RE.exec(launchRaw.trim()) : null;
    if (tbdMatch) {
      if (tbdMatch[1]) upcomingMsg = 'Upcoming in ' + tbdMatch[1].trim();
    } else {
      launchEpoch = toEpochSeconds(launchRaw);
      if (launchEpoch === null) {
        console.log('mission launchEpoch unparseable: ' + JSON.stringify(launchRaw));
        launchEpoch = 0;
      }
    }

    // Normalize each event's epoch to a number up front, dropping any that
    // fail to parse, so all the filtering logic below stays untouched.
    // epoch: "TBD" marks a not-yet-announced milestone (e.g. an unconfirmed
    // Artemis III phase) — skipped quietly, not logged as a data error.
    var normalizedEvents = [];
    for (var i = 0; i < events.length; i++) {
      var rawEpoch = events[i].epoch;
      if (typeof rawEpoch === 'string' && rawEpoch.trim().toUpperCase() === 'TBD') continue;
      var evEpoch = toEpochSeconds(rawEpoch);
      if (evEpoch === null) {
        console.log('mission event unparseable, dropped: ' + JSON.stringify(events[i]));
        continue;
      }
      normalizedEvents.push({ epoch: evEpoch, message: events[i].message, displayMinutes: events[i].displayMinutes });
    }
    events = normalizedEvents;

    // Build future-only list (strictly after now), sorted soonest-first.
    var future = [];
    for (var i = 0; i < events.length; i++) {
      var evMs = (events[i].epoch || 0) * 1000;
      if (evMs > now) future.push(events[i]);
    }
    future.sort(function(a, b) { return a.epoch - b.epoch; });

    // Keep all events within the 48h window, up to MAX_MISSION_EVENTS.
    // If nothing falls within the window, always include at least the soonest
    // future event — so the watch always has something to count down to.
    var upcoming = [];
    for (var i = 0; i < future.length; i++) {
      var evMs = future[i].epoch * 1000;
      if (evMs <= windowEnd) {
        upcoming.push(future[i]);
        if (upcoming.length >= MAX_MISSION_EVENTS) break;
      } else if (upcoming.length === 0) {
        upcoming.push(future[i]);  // nearest future event, beyond 48h window
        break;
      } else {
        break;  // sorted: everything past here is also beyond the window
      }
    }

    var msg = {
      'MISSION_NAME':                String(mission.name || '').substring(0, 15),
      'MISSION_CREW':                String(mission.crew || '').substring(0, 35),
      'MISSION_LAUNCH_EPOCH':        launchEpoch,
      'MISSION_END_HOURS':           mission.endHours || 0,
      'MISSION_STATS_MET_S':         stats.metS         || 0,
      'MISSION_STATS_MAX_DIST_KM':   stats.maxDistKm    || 0,
      'MISSION_STATS_MAX_SPEED_KMH': stats.maxSpeedKmh  || 0,
      'MISSION_STATS_MOON_DIST_KM':  stats.moonDistKm   || 0,
      'MISSION_DEFAULT_MSG':         String(upcomingMsg || mission.defaultMessage || '').substring(0, 35)
    };
    for (var j = 0; j < MAX_MISSION_EVENTS; j++) {
      var ev = upcoming[j];
      msg['MISSION_EVT' + j + '_EPOCH'] = ev ? (ev.epoch || 0) : 0;
      msg['MISSION_EVT' + j + '_MSG']   = ev ? String(ev.message || '').substring(0, 23) : '';
      msg['MISSION_EVT' + j + '_MIN']   = ev ? (ev.displayMinutes || 0) : 0;
    }
    msg['MISSION_SYNCED'] = 1;

    console.log('Mission: ' + msg['MISSION_NAME'] + ' launch=' + msg['MISSION_LAUNCH_EPOCH'] +
                ' events(fwd)=' + upcoming.length + '/' + events.length);

    Pebble.sendAppMessage(msg,
      function() { console.log('Mission data sent OK'); },
      function(e) { console.log('Mission send failed: ' + JSON.stringify(e)); }
    );
  }, function(err) {
    console.log('mission fetch error: ' + err);
  });
}

// ─── Pebble event listeners ───────────────────────────────────────────────────
Pebble.addEventListener('ready', function() {
  console.log('Artemis Missions Watchface JS ready');
  fetchArtemisData();
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload['REQUEST_ARTEMIS']) {
    fetchArtemisData();
  }
  if (e.payload['REQUEST_MISSION']) {
    fetchMissionData();
  }
});
