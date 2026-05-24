  // ─── Platform detection ───────────────────────────────────────────────────────
  var isColor = true;  // default: assume color if detection unavailable
  try {
    if (typeof Pebble !== 'undefined' && typeof Pebble.getActiveWatchInfo === 'function') {
      var watchInfo = Pebble.getActiveWatchInfo();
      if (watchInfo && watchInfo.platform) {
        isColor = ['basalt', 'chalk', 'emery'].indexOf(watchInfo.platform) !== -1;
      }
    }
  } catch (e) {
    // Older SDK — fall back to color defaults
  }

var fieldOptions = [
  { label: "None",                  value: "0"  },
  { label: "Mission Elapsed Time",  value: "1"  },
  { label: "Spacecraft Speed",      value: "2"  },
  { label: "Distance to Earth",     value: "3"  },
  { label: "Distance to Moon",      value: "4"  },
  { label: "Mission Phase",         value: "5"  },
  { label: "Next Event",            value: "6"  },
  { label: "Crew G-Force",          value: "7"  },
  { label: "Altitude",              value: "8"  },
  { label: "Closest Orbital Point", value: "9"  },
  { label: "Farthest Orbital Point",value: "10" },
  { label: "Signal Delay",          value: "11" },
  { label: "Tracking Station",      value: "12" },
  { label: "Downlink Rate",         value: "13" }
];

// This must be aligned with artemis_config.h
var themeOptions = isColor
    ? [
        { label: "B&W Dark",        value: "0" },
        { label: "B&W Clear",       value: "1" },
        { label: "Space (default)", value: "2" },
        { label: "Dark",            value: "3" },
        { label: "Clear",           value: "4" },
        { label: "NASA",            value: "5" },
        { label: "Custom",          value: "6" }
      ]
    : [
        { label: "Dark",  value: "0" },
        { label: "Light", value: "1" }
      ];

module.exports = [
  {
    "type": "heading",
    "defaultValue": "Artemis II Settings"
  },

  // ── Field Layout ───────────────────────────────────────────────────────────
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Field Layout"
      },
      {
        "type": "text",
        "defaultValue": "Choose what to display in each position. On round screens, positions are laid out in pairs: 1+2, 3+4, then individual rows."
      },
      {
        "type": "select",
        "messageKey": "SLOT_1",
        "label": "Position 1",
        "defaultValue": "1",
        "options": fieldOptions
      },
      {
        "type": "select",
        "messageKey": "SLOT_2",
        "label": "Position 2",
        "defaultValue": "2",
        "options": fieldOptions
      },
      {
        "type": "select",
        "messageKey": "SLOT_3",
        "label": "Position 3",
        "defaultValue": "3",
        "options": fieldOptions
      },
      {
        "type": "select",
        "messageKey": "SLOT_4",
        "label": "Position 4",
        "defaultValue": "4",
        "options": fieldOptions
      },
      {
        "type": "select",
        "messageKey": "SLOT_5",
        "label": "Position 5",
        "defaultValue": "5",
        "options": fieldOptions
      },
      {
        "capabilities": ["EMERY"],
        "type": "select",
        "messageKey": "SLOT_6",
        "label": "Position 6",
        "defaultValue": "6",
        "options": fieldOptions
      }
      {
        "capabilities": ["GABBRO"],
        "type": "select",
        "messageKey": "SLOT_6",
        "label": "Position 6",
        "defaultValue": "6",
        "options": fieldOptions
      }
    ]
  },

  // ── Data Updates ───────────────────────────────────────────────────────────
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Data Updates"
      },
      {
        "type": "select",
        "messageKey": "UPDATE_INTERVAL",
        "label": "Update Interval",
        "defaultValue": "30",
        "options": [
          { "label": "15 minutes", "value": "15" },
          { "label": "30 minutes", "value": "30" },
          { "label": "60 minutes", "value": "60" }
        ]
      },
      {
        "type": "toggle",
        "messageKey": "USE_MILES",
        "label": "Use Miles (instead of km)",
        "defaultValue": false
      },
      {
        "type": "toggle",
        "messageKey": "VIBRATE_EVENTS",
        "label": "Vibrate on special events",
        "defaultValue": true
      }
    ]
  },

  // ── Color Theme ────────────────────────────────────────────────────────────
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Color Theme"
      },
      {
        "type": "select",
        "messageKey": "COLOR_THEME",
        "label": "Theme",
        "defaultValue": isColor ? "2" : "0",
        "options": themeOptions_bw
      },
      {
        "capabilities": ["COLOR"],
        "type": "color",
        "messageKey": "COLOR_BACKGROUND",
        "label": "Background",
        "defaultValue": "0x000000",
        "sunlight": true
      },
      {
        "capabilities": ["COLOR"],
        "type": "color",
        "messageKey": "COLOR_ACCENT",
        "label": "Accent (labels, title, lines)",
        "defaultValue": "0x55FFFF",
        "sunlight": true
      },
      {
        "capabilities": ["COLOR"],
        "type": "color",
        "messageKey": "COLOR_VALUES",
        "label": "Values (time, data)",
        "defaultValue": "0xFFFFFF",
        "sunlight": true
      },
      {
        "capabilities": ["COLOR"],
        "type": "color",
        "messageKey": "COLOR_HIGHLIGHTS",
        "label": "Highlights (ETA, battery)",
        "defaultValue": "0xFFFF00",
        "sunlight": true
      }
    ]
  },

  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
