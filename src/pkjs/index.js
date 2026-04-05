/* Artemis II Watchface — Phone-Side JS */
/* Uses https://artemis.cdnspace.ca live telemetry API */

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

// ─── Constants ────────────────────────────────────────────────────────────────
var API_ALL_URL   = 'https://artemis.cdnspace.ca/api/all';
var TIMELINE_URL  = 'https://artemis.cdnspace.ca/api/timeline';

// Throttle: don't re-fetch if we fetched in last 5 minutes
var THROTTLE_MS = 5 * 60 * 1000;

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

    var missionComplete = (typeof metMs !== 'number' || metMs < 0);

    // ── Fetch timeline for phase + milestone ──────────────────────────────
    xhrRequest(TIMELINE_URL, function(timelineText) {
      var timeline;
      try { timeline = JSON.parse(timelineText); } catch(e) {
        timeline = { phases: [], milestones: [] };
      }

      var phases     = timeline.phases     || [];
      var milestones = timeline.milestones || [];

      var phase = missionComplete
        ? (phases.length ? phases[phases.length - 1].phase : 'Complete')
        : getCurrentPhase(phases, metMs);

      var nextMs         = missionComplete ? null : getNextMilestone(milestones, metMs);
      var milestoneName  = nextMs ? shortenMilestone(nextMs.name) : 'Mission Complete';
      var milestoneMetMs = nextMs ? Math.min(nextMs.metMs, 2147483647) : -1;

      localStorage.setItem('lastArtemisF', String(Date.now()));

      console.log('Phase: ' + phase + ' Speed: ' + speedKmS.toFixed(2) +
                  ' Earth: ' + Math.round(distKm) + ' Moon: ' + Math.round(moonDistKm) +
                  ' Next: ' + milestoneName);

      Pebble.sendAppMessage({
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
      },
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
  });
}

// ─── Pebble event listeners ───────────────────────────────────────────────────
Pebble.addEventListener('ready', function() {
  console.log('Artemis II JS ready');
  fetchArtemisData();
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload['REQUEST_ARTEMIS']) {
    fetchArtemisData();
  }
});
