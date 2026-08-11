/**
 * @file artemis.h
 * @brief Single shared header: build flags, types, palette, fonts, data structs, and extern state.
 *
 * The one header every module includes. Contains everything that is referenced
 * by more than one translation unit: the debug flag, persistent-storage keys,
 * slot counts, the FieldType enum, the Night Sky color palette, platform-specific
 * font resource IDs and pre-computed heights, the ArtemisSettings / ArtemisData
 * structs, extern declarations for all shared globals, and prototypes for
 * overlay_geometry() and artemis_update_display() (both defined in main.c).
 *
 * Mission-specific data (launch epoch, crew, special events) is no longer
 * compiled in — it's synced from the cloud (missions/active.json on GitHub)
 * into the persisted ArtemisMission struct, s_mission, defined right here.
 *
 * @author LP Fabiani
 * @date 2026
 */
#pragma once
#include <pebble.h>

// Uncoment to set date and time for the Artemis II launch.
//#define DEMO_MODE
#define DEMO_MODE_TIME "06:35"
#define DEMO_MODE_DATE "Wed 1"
#define DEMO_MODE_DATE_LONG "Wed, Apr 1"

// ─── Debug logging ────────────────────────────────────────────────────────────
//#define DEBUG_ENABLED  // comment to disable APP_LOG debug output

#ifdef DEBUG_ENABLED
  #define ARTEMIS_LOG APP_LOG
#else
  #define ARTEMIS_LOG(lvl, fmt, ...)
#endif

// ─── Persistent storage ───────────────────────────────────────────────────────
#define SETTINGS_KEY     1
#define ARTEMIS_KEY      2
#define SETTINGS_VERSION 7  // bump when struct layout changes to force reset

// ─── Platform slot count ──────────────────────────────────────────────────────
#define MAX_SLOTS 6
#if defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_CHALK)
  #define NUM_SLOTS 5
#else
  #define NUM_SLOTS 6
#endif

// ─── Display modes ────────────────────────────────────────────────────────────
typedef enum {
  DISPLAY_LOGO  = 0,   // default — logo visible, bottom zone normal
  DISPLAY_EVENT = 1,   // special event active — event banner visible
  DISPLAY_INFO  = 2,   // after shake/touch — info slots visible, bottom zone hidden
} DisplayMode;

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

// ─── Night Sky palette ────────────────────────────────────────────────────────
// Single fixed theme. All render code uses these constants — no inline colors.
#ifdef PBL_COLOR
  #define ARTEMIS_COLOR_SKY          GColorBlack
  #define ARTEMIS_COLOR_SKY_STARS    GColorWhite
  #define ARTEMIS_COLOR_VALUES       GColorWhite
  #define ARTEMIS_COLOR_ACCENT       GColorVividCerulean // #0055AA — labels, lines, next-event ETA
  // Color screens show a color gradient in the moon, so the date is more visible on white
  #define ARTEMIS_COLOR_TIME         GColorBlack
  #define ARTEMIS_COLOR_DATE         GColorWhite
#else
  #define ARTEMIS_COLOR_SKY          GColorBlack
  #define ARTEMIS_COLOR_SKY_STARS    GColorWhite
  #define ARTEMIS_COLOR_VALUES       GColorWhite
  #define ARTEMIS_COLOR_ACCENT       GColorWhite
  // Date and time are against a full-white moon
  #define ARTEMIS_COLOR_TIME         GColorBlack
  #define ARTEMIS_COLOR_DATE         GColorBlack
#endif

// ─── Fonts ────────────────────────────────────────────────────────────────────
#if defined(PBL_PLATFORM_GABBRO)   // Round 2 — large round
  #define FONT_TIME    RESOURCE_ID_FONT_ARTEMIS_56
  #define FONT_DATE    RESOURCE_ID_FONT_ARTEMIS_24
  #define FONT_EVENT   RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_TIME_H  56
  #define FONT_DATE_H  24
  #define FONT_EVENT_H 18
#elif defined(PBL_PLATFORM_EMERY)  // Time 2 — large square
  #define FONT_TIME    RESOURCE_ID_FONT_ARTEMIS_50
  #define FONT_DATE    RESOURCE_ID_FONT_ARTEMIS_24
  #define FONT_EVENT   RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_TIME_H  50
  #define FONT_DATE_H  24
  #define FONT_EVENT_H 18
#elif defined(PBL_PLATFORM_CHALK)  // Round — small round
  #define FONT_TIME    RESOURCE_ID_FONT_ARTEMIS_40
  #define FONT_DATE    RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_EVENT   RESOURCE_ID_FONT_ARTEMIS_14
  #define FONT_TIME_H  40
  #define FONT_DATE_H  18
  #define FONT_EVENT_H 14
#else                               // Basalt, Aplite — small square
  #define FONT_TIME    RESOURCE_ID_FONT_ARTEMIS_40
  #define FONT_DATE    RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_EVENT   RESOURCE_ID_FONT_ARTEMIS_14
  #define FONT_TIME_H  40
  #define FONT_DATE_H  18
  #define FONT_EVENT_H 14
#endif

  // ─── Info trigger modes ───────────────────────────────────────────────────────
  typedef enum {
    INFO_TRIGGER_NEVER  = 0,  // logo always; telemetry never shown
    INFO_TRIGGER_SHAKE  = 1,  // accelerometer tap → timed info
    INFO_TRIGGER_TOUCH  = 2,  // touchscreen tap → timed info (touch-capable watches only)
    INFO_TRIGGER_BOTH   = 3,  // either input → timed info
    INFO_TRIGGER_ALWAYS = 10,  // telemetry is the permanent default view
  } InfoTrigger;

// ─── Data structs ─────────────────────────────────────────────────────────────
typedef struct {
  uint8_t  version;
  int32_t  update_interval_min;
  bool     use_miles;
  uint8_t  slots[MAX_SLOTS];
  bool     vibrate_events;
  bool     vibrate_bt_disconnect;  // vibrate when Bluetooth connection is lost
  uint8_t  info_trigger;    // InfoTrigger
  uint8_t  info_display_s;  // seconds to keep info visible (5–60)
} ArtemisSettings;

// ─── Cloud-delivered mission data ─────────────────────────────────────────────
// Replaces the compile-time constants formerly in artemis_mission.h. Synced
// from the phone (which fetches missions/active.json from GitHub) once every
// 24 hours, persisted under MISSION_KEY. A freshly-installed watch starts with
// this struct zeroed, which reproduces the old "no mission yet" fallback
// behaviour (launch_epoch 0 → MISSION_PHASE_COMPLETE, empty name/crew, all
// stats omitted) with no separate fallback code required.
//
// Struct sized to stay under Pebble's 256-byte persist_write_data limit:
// 16 + 36 + 4 + 2 + 4*4 + 4 + 1 + 5*32 ≈ 244 bytes.
#define MISSION_KEY         3
#define MAX_MISSION_EVENTS  5

typedef struct {
  uint32_t epoch;            // UTC unix timestamp when the event fires
  char     message[24];      // "LINE1\nLINE2" banner text
  uint16_t display_minutes;  // 0 = 5-minute default
} MissionEvent;

typedef struct {
  char         name[16];               // e.g. "Artemis III"
  char         crew[36];               // "Line1\nLine2", "" to omit
  uint32_t     launch_epoch;           // UTC unix seconds; 0 = no mission yet
  uint16_t     end_hours;              // nominal mission duration
  int32_t      stats_met_s;            // post-mission stats; 0 = omit from display
  int32_t      stats_max_dist_km;
  int32_t      stats_max_speed_kmh;
  int32_t      stats_moon_dist_km;
  uint32_t     last_sync_epoch;        // when this data was last synced; 0 = never
  uint8_t      num_events;             // number of valid entries in events[]
  MissionEvent events[MAX_MISSION_EVENTS];
} ArtemisMission;

// Default/fallback phase text (e.g. "Artemis III coming in 2027"), shown in
// place of the T-minus/completed-ago/next-event text whenever present and the
// watch isn't showing an active mission or a special event. Persisted under
// its own key, separate from ArtemisMission — that struct is already close to
// persist_write_data's 256-byte-per-key limit (~244/256 bytes).
#define MISSION_MSG_KEY  4
#define MISSION_MSG_LEN  36

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

// ─── Shared globals — defined in main.c ──────────────────────────────────────
extern ArtemisSettings s_settings;
extern ArtemisData     s_artemis;
extern ArtemisMission  s_mission;
extern char            s_mission_default_msg[MISSION_MSG_LEN];
extern Layer          *s_root_layer;
extern int             s_root_w, s_root_h;
extern int             s_split_y;
extern GFont           s_font_time;
extern GFont           s_font_date;
extern GFont           s_font_event;

// ─── Geometry helper — defined in main.c ─────────────────────────────────────
// Returns the top-zone rectangle used by the logo and event overlay.
void overlay_geometry(int *out_top, int *out_h);

// ─── Display orchestration — defined in main.c ───────────────────────────────
// Runs the LOGO / EVENT / INFO state machine and shows/hides layers accordingly.
void artemis_update_display(void);

// Apply info_on_shake and info_on_touch settings: re-subscribe or unsubscribe
// AccelTapService and TouchService accordingly. Call after loading or changing
// either setting. Defined in main.c.
void artemis_apply_interaction_settings(void);

// ─── Layer helper ────────────────────────────────────────────────────────────
TextLayer *artemis_make_text_layer(Layer *root, GRect r, GColor col,
                                 GFont font, GTextAlignment align);
