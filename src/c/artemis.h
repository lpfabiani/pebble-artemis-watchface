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
 * Mission-specific data (launch epoch, special events, field display labels)
 * lives in artemis_mission.h and is included only by artemis_info.c.
 *
 * @author LP Fabiani
 * @date 2026
 */
#pragma once
#include <pebble.h>

// ─── Debug logging ────────────────────────────────────────────────────────────
#define DEBUG_ENABLED 1  // set to 0 to disable APP_LOG debug output

// ─── Persistent storage ───────────────────────────────────────────────────────
#define SETTINGS_KEY     1
#define ARTEMIS_KEY      2
#define SETTINGS_VERSION 3  // bump when struct layout changes to force reset

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

// ─── Night Sky palette ────────────────────────────────────────────────────────
// Single fixed theme. All render code uses these constants — no inline colors.
#ifdef PBL_COLOR
  #define ARTEMIS_COLOR_SKY          GColorBlack
  #define ARTEMIS_COLOR_SKY_HORIZON  GColorDukeBlue      // #000055 — unused, kept for reference
  #define ARTEMIS_COLOR_SKY_STARS    GColorWhite
  #define ARTEMIS_COLOR_VALUES       GColorWhite
  #define ARTEMIS_COLOR_ACCENT       GColorVividCerulean // #0055AA — labels, lines, next-event ETA
  #define ARTEMIS_COLOR_TIME         GColorBlack
  #define ARTEMIS_COLOR_DATE         GColorWhite
#else
  #define ARTEMIS_COLOR_SKY          GColorBlack
  #define ARTEMIS_COLOR_SKY_HORIZON  GColorBlack
  #define ARTEMIS_COLOR_SKY_STARS    GColorWhite
  #define ARTEMIS_COLOR_VALUES       GColorWhite
  #define ARTEMIS_COLOR_ACCENT       GColorWhite
  #define ARTEMIS_COLOR_TIME         GColorBlack
  #define ARTEMIS_COLOR_DATE         GColorBlack
#endif

// ─── Fonts ────────────────────────────────────────────────────────────────────
#if defined(PBL_PLATFORM_GABBRO)   // Round 2 — large round
  #define FONT_TIME    RESOURCE_ID_FONT_ARTEMIS_56
  #define FONT_DATE    RESOURCE_ID_FONT_ARTEMIS_24
  #define FONT_LABEL   RESOURCE_ID_FONT_ARTEMIS_24
  #define FONT_TIME_H  56
  #define FONT_DATE_H  25
  #define FONT_LABEL_H 25
#elif defined(PBL_PLATFORM_EMERY)  // Time 2 — large square
  #define FONT_TIME    RESOURCE_ID_FONT_ARTEMIS_50
  #define FONT_DATE    RESOURCE_ID_FONT_ARTEMIS_24
  #define FONT_LABEL   RESOURCE_ID_FONT_ARTEMIS_24
  #define FONT_TIME_H  50
  #define FONT_DATE_H  25
  #define FONT_LABEL_H 25
#elif defined(PBL_PLATFORM_CHALK)  // Round — small round
  #define FONT_TIME    RESOURCE_ID_FONT_ARTEMIS_40
  #define FONT_DATE    RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_LABEL   RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_TIME_H  40
  #define FONT_DATE_H  18
  #define FONT_LABEL_H 18
#else                               // Basalt, Aplite — small square
  #define FONT_TIME    RESOURCE_ID_FONT_ARTEMIS_40
  #define FONT_DATE    RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_LABEL   RESOURCE_ID_FONT_ARTEMIS_18
  #define FONT_TIME_H  48
  #define FONT_DATE_H  18
  #define FONT_LABEL_H 18
#endif

// ─── Data structs ─────────────────────────────────────────────────────────────
typedef struct {
  uint8_t  version;
  int32_t  update_interval_min;
  bool     use_miles;
  uint8_t  slots[MAX_SLOTS];
  bool     vibrate_events;
} ArtemisSettings;

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
extern Layer          *s_root_layer;
extern int             s_root_w, s_root_h;
extern int             s_split_y;
extern GFont           s_font_time;
extern GFont           s_font_date;
extern GFont           s_font_label;

// ─── Shared globals — defined in artemis_info.c ──────────────────────────────
extern int s_active_slots[MAX_SLOTS];
extern int s_num_active;

// ─── Geometry helper — defined in main.c ─────────────────────────────────────
// Returns the top-zone rectangle used by the logo and event overlay.
void overlay_geometry(int w, int h, int *out_top, int *out_h);

// ─── Display orchestration — defined in main.c ───────────────────────────────
// Calls artemis_info_refresh() and shows/hides the logo accordingly.
void artemis_update_display(void);
