/**
 * @file artemis_mission.h
 * @brief Artemis II mission data: timing constants, special events table, and field labels.
 *
 * Contains mission-specific information used exclusively by artemis_info.c:
 * the launch epoch for MET calculation, the hardcoded special events table
 * (key mission moments shown as top-zone banner overlays), and the display-name
 * strings for each FieldType value.
 *
 * Include this header only in translation units that need mission data — primarily
 * artemis_info.c. It must not be pulled into artemis.h to avoid instantiating
 * the SPECIAL_EVENTS static array in every compilation unit.
 *
 * @author LP Fabiani
 * @date 2026
 */
#pragma once
#include "artemis.h"

// ─── Mission timing ───────────────────────────────────────────────────────────
#define LAUNCH_EPOCH       ((time_t)1775082900)  // Apr 1 2026 22:35 UTC
#define MISSION_END_HOURS  229

// ─── Special events ───────────────────────────────────────────────────────────
// Each event banner is shown for EVENT_DISPLAY_S seconds (or display_minutes
// if non-zero), then dismissed. All epochs are UTC unix timestamps. EDT = UTC-4.
// Source: https://www.nasa.gov/missions/nasa-answers-your-most-pressing-artemis-ii-questions
#define EVENT_DISPLAY_S  (5 * 60)

typedef struct {
  uint32_t    epoch;
  const char *message;
  uint16_t    display_minutes;  // 0 = use EVENT_DISPLAY_S default
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

// ─── Field display labels ─────────────────────────────────────────────────────
// Parallel to FieldType enum; index matches enum value. Used only by
// artemis_info.c (prv_render_slot) to populate label text layers.
static const char *FIELD_LABELS[FIELD_COUNT] = {
  "", "MET", "SPEED", "EARTH", "MOON", "PHASE", "NEXT EVENT",
  "G-FORCE", "ALTITUDE", "PERIAPSIS", "APOAPSIS", "SIGNAL", "STATION", "DOWNLINK"
};
