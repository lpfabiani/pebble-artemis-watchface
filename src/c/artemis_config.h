#pragma once
#include <pebble.h>

// ─── Debug logging ────────────────────────────────────────────────────────────
#define DEBUG_ENABLED 1  // Set to 1 to enable debug logging, 0 to disable

// ─── Persistent storage ───────────────────────────────────────────────────────
#define SETTINGS_KEY     1
#define ARTEMIS_KEY      2
#define SETTINGS_VERSION 3  // bump when struct changes to force reset

// ─── Mission constants ────────────────────────────────────────────────────────
#define LAUNCH_EPOCH       ((time_t)1775082900)  // Apr 1 2026 22:35 UTC
#define MISSION_END_HOURS  229

// ─── Special events ───────────────────────────────────────────────────────────
// Each event banner is shown for EVENT_DISPLAY_S seconds, then dismissed.
// All times are UTC unix epochs.  EDT = UTC-4.
// Source: https://www.nasa.gov/missions/nasa-answers-your-most-pressing-artemis-ii-questions
#define EVENT_DISPLAY_S  (5 * 60)

typedef struct {
  uint32_t    epoch;
  const char *message;
  uint16_t    display_minutes;  // notification length in minutes; 0 = use default (EVENT_DISPLAY_S)
} SpecialEvent;

static const SpecialEvent SPECIAL_EVENTS[] = {
  // PAST EVENTS - ARTEMIS II:
  /*
  { 1775501100UL, "MOON OBS.\nBEGINS",     0 },  // Apr 6 18:45 UTC (2:45 PM EDT)
  { 1775515620UL, "BEHIND\nTHE MOON",      0 },  // Apr 6 22:47 UTC (6:47 PM EDT)
  { 1775516520UL, "CLOSEST\nTO MOON",      0 },  // Apr 6 23:02 UTC (7:02 PM EDT)
  { 1775516700UL, "MAX DIST\nFROM EARTH",  0 },  // Apr 6 23:05 UTC (7:05 PM EDT)
  { 1775518020UL, "SIGNAL\nRESTORED",      0 },  // Apr 6 23:27 UTC (7:27 PM EDT)
  { 1775524800UL, "MOON OBS.\nENDS",       0 },  // Apr 7 01:20 UTC (9:20 PM EDT)
  */
};
#define NUM_SPECIAL_EVENTS ((int)(sizeof(SPECIAL_EVENTS) / sizeof(SPECIAL_EVENTS[0])))

// ─── Platform slot count ──────────────────────────────────────────────────────
#define MAX_SLOTS 6
#if defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_CHALK)
  #define NUM_SLOTS 5
#else
  #define NUM_SLOTS 6
#endif

// ─── Field types ──────────────────────────────────────────────────────────────
typedef enum {
  FIELD_NONE       = 0,
  FIELD_MET        = 1,
  FIELD_SPEED      = 2,
  FIELD_EARTH_DIST = 3,
  FIELD_MOON_DIST  = 4,
  FIELD_PHASE      = 5,
  FIELD_NEXT_EVENT = 6,
  FIELD_G_FORCE    = 7,
  FIELD_ALTITUDE   = 8,
  FIELD_PERIAPSIS  = 9,
  FIELD_APOAPSIS   = 10,
  FIELD_SIGNAL     = 11,
  FIELD_STATION    = 12,
  FIELD_DOWNLINK   = 13,
  FIELD_COUNT      = 14
} FieldType;

static const char *FIELD_LABELS[FIELD_COUNT] = {
  "", "MET", "SPEED", "EARTH", "MOON", "PHASE", "NEXT EVENT",
  "G-FORCE", "ALTITUDE", "PERIAPSIS", "APOAPSIS", "SIGNAL", "STATION", "DOWNLINK"
};

// ─── Night Sky palette ────────────────────────────────────────────────────────
// Single fixed theme. All render code uses these constants — no inline colors.
#ifdef PBL_COLOR
  #define ARTEMIS_COLOR_SKY    GColorBlack        // top of gradient
  #define ARTEMIS_COLOR_SKY_HORIZON   GColorDukeBlue     // bottom of gradient (#000055)
  #define ARTEMIS_COLOR_SKY_STARS     GColorWhite
  #define ARTEMIS_COLOR_VALUES  GColorWhite        // time, data values
  #define ARTEMIS_COLOR_ACCENT  GColorVividCerulean   // Artemis logo blue (#0055AA); labels, lines, next-event ETA
  #define ARTEMIS_COLOR_TIME    GColorBlack
  #define ARTEMIS_COLOR_DATE    GColorWhite    // Artemis logo red (#FF0000)
#else
  #define ARTEMIS_COLOR_SKY    GColorBlack
  #define ARTEMIS_COLOR_SKY_HORIZON   GColorBlack
  #define ARTEMIS_COLOR_SKY_STARS     GColorWhite
  #define ARTEMIS_COLOR_VALUES  GColorWhite
  #define ARTEMIS_COLOR_ACCENT  GColorWhite
  #define ARTEMIS_COLOR_TIME    GColorBlack
  #define ARTEMIS_COLOR_DATE    GColorBlack
#endif

/* ── Color theme enum removed: single Night Sky palette replaces all themes ──
#ifdef PBL_COLOR
typedef enum {
    COLOR_MODE_BW_DARK = 0,
    COLOR_MODE_BW_CLEAR = 1,
    COLOR_MODE_SPACE = 2,
    COLOR_MODE_DARK = 3,
    COLOR_MODE_CLEAR = 4,
    COLOR_MODE_NASA = 5,
    COLOR_MODE_CUSTOM = 6
} ColorMode;
#define DEFAULT_COLOR_THEME      COLOR_MODE_BW_DARK
#else
typedef enum {
    COLOR_MODE_BW_DARK = 0,
    COLOR_MODE_BW_CLEAR = 1,
} ColorMode;
#define DEFAULT_COLOR_THEME      COLOR_MODE_BW_DARK
#endif
── */

// --- FONTS
#if defined(PBL_PLATFORM_GABBRO) // Pebble Round 2 - Round big
  #define FONT_TIME  RESOURCE_ID_FONT_ARTEMIS_56
  #define FONT_DATE  RESOURCE_ID_FONT_ARTEMIS_24
  #define FONT_LABEL RESOURCE_ID_FONT_ARTEMIS_24
  #define FONT_TIME_H  56
  #define FONT_DATE_H  25
  #define FONT_LABEL_H 25
#elif defined(PBL_PLATFORM_EMERY)  // Pebble Time 2 - Square big
  #define FONT_TIME  RESOURCE_ID_FONT_ARTEMIS_50
  #define FONT_DATE  RESOURCE_ID_FONT_ARTEMIS_24
  #define FONT_LABEL RESOURCE_ID_FONT_ARTEMIS_24
  #define FONT_TIME_H  50
  #define FONT_DATE_H  25
  #define FONT_LABEL_H 25
#elif defined(PBL_PLATFORM_CHALK) // Pebble Round - Round small
  #define FONT_TIME  RESOURCE_ID_FONT_ARTEMIS_40
  #define FONT_DATE  RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_LABEL RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_TIME_H  40
  #define FONT_DATE_H  18
  #define FONT_LABEL_H 18
#else  // Basalt, Aplite - Square mall
  #define FONT_TIME  RESOURCE_ID_FONT_ARTEMIS_40
  #define FONT_DATE  RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_LABEL RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_TIME_H  48
  #define FONT_DATE_H  18
  #define FONT_LABEL_H 18
#endif


#ifdef PBL_COLOR
  #define LOGO_RESOURCE RESOURCE_ID_IMAGE_ARTEMIS_LOGO_COLOR
#else
  #define LOGO_RESOURCE RESOURCE_ID_IMAGE_ARTEMIS_LOGO_BW_WHITE
#endif


// ─── Default settings ─────────────────────────────────────────────────────────
#define DEFAULT_UPDATE_INTERVAL  30
#define DEFAULT_USE_MILES        false
/* ── Color defaults removed: single Night Sky palette ──
#define DEFAULT_COLOR_BACKGROUND 0x000000
#define DEFAULT_COLOR_ACCENT     0x55FFFF
#define DEFAULT_COLOR_VALUES     0xFFFFFF
#define DEFAULT_COLOR_HIGHLIGHTS 0xFFFF00
── */
#define DEFAULT_VIBRATE_EVENTS   true

// Default slot assignments (platform-specific)
// Small/round platforms (Aplite, Basalt, Chalk) have NUM_SLOTS=5; slot 5 unused.
#if defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_CHALK)
  #define DEFAULT_SLOT_0  FIELD_MET
  #define DEFAULT_SLOT_1  FIELD_PHASE
  #define DEFAULT_SLOT_2  FIELD_EARTH_DIST
  #define DEFAULT_SLOT_3  FIELD_MOON_DIST
  #define DEFAULT_SLOT_4  FIELD_NEXT_EVENT
  #define DEFAULT_SLOT_5  FIELD_NONE
#else
  #define DEFAULT_SLOT_0  FIELD_MET
  #define DEFAULT_SLOT_1  FIELD_PHASE
  #define DEFAULT_SLOT_2  FIELD_EARTH_DIST
  #define DEFAULT_SLOT_3  FIELD_MOON_DIST
  //  #define DEFAULT_SLOT_1  FIELD_NONE
  //  #define DEFAULT_SLOT_3  FIELD_NONE
  //  #define DEFAULT_SLOT_2  FIELD_NONE
  #define DEFAULT_SLOT_4  FIELD_SPEED
  #define DEFAULT_SLOT_5  FIELD_NEXT_EVENT
#endif

// ─── Structs ──────────────────────────────────────────────────────────────────
typedef struct {
  uint8_t  version;
  int32_t  update_interval_min;
  bool     use_miles;
  uint8_t  slots[MAX_SLOTS];
  /* ── Color theme fields removed: single Night Sky palette ──
  uint8_t  color_theme;
  uint32_t color_background;
  uint32_t color_accent;
  uint32_t color_values;
  uint32_t color_highlights;
  ── */
  bool     vibrate_events;
} ArtemisSettings;

// Up to 5 upcoming milestones stored on the watch for offline event detection
#define MAX_UPCOMING 5
typedef struct {
  char     name[19];   // shortened milestone name (≤18 chars + null)
  uint32_t epoch;      // UTC unix timestamp when milestone occurs
} UpcomingMilestone;

typedef struct {
  char     phase[20];
  int32_t  speed_x100;
  int32_t  distance_km;
  int32_t  moon_distance_km;
  char     milestone_name[32];
  int32_t  milestone_met_ms;
  bool     mission_complete;
  uint32_t last_update_epoch;
  int32_t  g_force_x10000;
  int32_t  altitude_km;
  int32_t  periapsis_km;
  int32_t  apoapsis_km;
  int32_t  signal_x100;
  char     dsn_station[20];
  int32_t  downlink_kbps;
  UpcomingMilestone upcoming[MAX_UPCOMING];
} ArtemisData;
