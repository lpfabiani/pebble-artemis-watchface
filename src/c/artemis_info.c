/**
 * @file artemis_info.c
 * @brief Top-zone info display: slot rendering, decorations, and event overlay.
 *
 * Owns @c s_info_layer and its children. In active mission: slot label/value
 * layers + decorations layer. Pre-launch and post-mission: an ArtemisEventOverlay
 * (@c s_phase_overlay) that shows crew, T-minus countdown, or completion stats.
 * Phase detection is internal — callers always use the same public API.
 *
 * @author LP Fabiani
 * @date 2026
 */
#include "artemis_info.h"
#include "artemis.h"
#include "artemis_event.h"

// ─── Variables ────────────────────────────────────────────────────────────
// ─── Field display labels ─────────────────────────────────────────────────────
// Parallel to FieldType enum; index matches enum value. Used only by
// artemis_info.c (prv_render_slot) to populate label text layers.
static const char *FIELD_LABELS[FIELD_COUNT] = {
  "", "MET", "SPEED", "EARTH", "MOON", "PHASE", "NEXT EVENT",
  "G-FORCE", "ALTITUDE", "PERIAPSIS", "APOAPSIS", "SIGNAL", "STATION", "DOWNLINK"
};


// ─── Mission phase ────────────────────────────────────────────────────────────
typedef enum {
  MISSION_PHASE_PRELAUNCH,
  MISSION_PHASE_ACTIVE,
  MISSION_PHASE_COMPLETE,
} MissionPhase;

static MissionPhase prv_get_mission_phase(void) {
  time_t now = time(NULL);
  time_t launch_epoch = (time_t)s_mission.launch_epoch;
  if (now < launch_epoch) return MISSION_PHASE_PRELAUNCH;
  if (s_artemis.mission_complete
      || now >= launch_epoch + (time_t)s_mission.end_hours * 3600)
    return MISSION_PHASE_COMPLETE;
  return MISSION_PHASE_ACTIVE;
}

static const MissionEvent *prv_next_event(time_t now) {
  const MissionEvent *best = NULL;
  for (int i = 0; i < s_mission.num_events; i++) {
    const MissionEvent *ev = &s_mission.events[i];
    if (ev->epoch == 0) continue;
    if ((time_t)ev->epoch <= now) continue;
    if (!best || ev->epoch < best->epoch) best = ev;
  }
  return best;
}

static void prv_first_line(const char *msg, char *buf, size_t size) {
  const char *nl = strchr(msg, '\n');
  size_t len = nl ? (size_t)(nl - msg) : strlen(msg);
  if (len >= size) len = size - 1;
  memcpy(buf, msg, len);
  buf[len] = '\0';
}

static char s_phase_buf[160];

static const char *prv_build_phase_text(MissionPhase phase) {
  char *p = s_phase_buf;
  int rem = (int)sizeof(s_phase_buf);
  int n;

  time_t now = time(NULL);

  time_t launch_epoch = (time_t)s_mission.launch_epoch;

  // Default/fallback message (e.g. "Artemis III coming in 2027") takes
  // priority over the auto-generated T-minus/completed-ago/next-event text
  // whenever present — outside an active mission and (by virtue of this
  // function's only caller) outside a special event too.
  if (phase != MISSION_PHASE_ACTIVE && s_mission_default_msg[0] != '\0') {
    snprintf(p, rem, "%s", s_mission_default_msg);
    return s_phase_buf;
  }

  if (phase != MISSION_PHASE_ACTIVE) {
    const MissionEvent *ev = prv_next_event(now);
    if (ev) {
      char first_line[24];
      prv_first_line(ev->message, first_line, sizeof(first_line));
      int32_t sec = (int32_t)((time_t)ev->epoch - now);
      if (sec < 0) sec = 0;
      int d = (int)(sec / 86400);
      int h = (int)((sec % 86400) / 3600);
      int m = (int)((sec % 3600) / 60);
      if (d > 0)      snprintf(p, rem, "%s in %dd %dh %dm", first_line, d, h, m);
      else if (h > 0) snprintf(p, rem, "%s in %dh %dm", first_line, h, m);
      else            snprintf(p, rem, "%s in %dm", first_line, m);
      return s_phase_buf;
    }
  }

  if (phase == MISSION_PHASE_PRELAUNCH) {
    n = snprintf(p, rem, "%s\n", s_mission.name);
    p += n; rem -= n;
    if (s_mission.crew[0] != '\0') {
      n = snprintf(p, rem, "%s\n", s_mission.crew);
      p += n; rem -= n;
    }
    int32_t sec = (int32_t)(launch_epoch - now);
    if (sec < 0) sec = 0;
    int d = (int)(sec / 86400);
    int h = (int)((sec % 86400) / 3600);
    int m = (int)((sec % 3600) / 60);
    if (d > 0)      snprintf(p, rem, "T-%dd %dh %dm", d, h, m);
    else if (h > 0) snprintf(p, rem, "T-%dh %dm", h, m);
    else            snprintf(p, rem, "T-%dm", m);
  } else {
    // COMPLETE, no stats: just name + elapsed. Stats are shown via slot layers.
    time_t end_epoch = launch_epoch + (time_t)s_mission.end_hours * 3600;
    int32_t sec = (int32_t)(now - end_epoch);
    if (sec < 0) sec = 0;
    int d = (int)(sec / 86400);
    int h = (int)((sec % 86400) / 3600);
    n = snprintf(p, rem, "%s\n", s_mission.name);
    p += n; rem -= n;
    if (d < 30) snprintf(p, rem, "completed %dd %dh ago", d, h);
    else        snprintf(p, rem, "completed %dd ago", d);
  }

  return s_phase_buf;
}

// ─── Owned globals ────────────────────────────────────────────────────────────
static ArtemisEventOverlay *s_phase_overlay    = NULL;
static Layer               *s_info_layer       = NULL;
static Layer               *s_decorations_layer = NULL;
static MissionPhase         s_last_phase       = MISSION_PHASE_PRELAUNCH;
static bool                 s_stats_rendered   = false;

static TextLayer *s_field_label_layers[MAX_SLOTS];
static TextLayer *s_field_value_layers[MAX_SLOTS];

static int  s_active_slots[MAX_SLOTS];
static int  s_num_active = 0;

#define FIELD_BUF_SIZE  20
#define VALUE_BUF_SIZE  24
static char s_slot_label_bufs[MAX_SLOTS][FIELD_BUF_SIZE];
static char s_slot_value_bufs[MAX_SLOTS][VALUE_BUF_SIZE];

// ─── Format helpers ───────────────────────────────────────────────────────────
static void prv_format_commas(int32_t value, char *buf, size_t size) {
  char tmp[16];
  bool negative = (value < 0);
  snprintf(tmp, sizeof(tmp), "%d", negative ? -(int)value : (int)value);
  
  int len   = strlen(tmp);
  int out   = 0;
  int first = len - ((len - 1) / 3) * 3;
  if (negative && out < (int)size - 1) buf[out++] = '-';
  for (int i = 0; i < len && out < (int)size - 1; i++) {
    if (i > 0 && (i - first) % 3 == 0 && out < (int)size - 2) buf[out++] = ',';
    buf[out++] = tmp[i];
  }

  buf[out] = '\0';
}

static const char *prv_unit(const char *km_unit, const char *mi_unit) {
  return s_settings.use_miles ? mi_unit : km_unit;
}

static int32_t prv_to_miles(int32_t km) {
  // Split km into its thousands and remainder, multiply each part separately.
  // Both sub-expressions stay within int32_t range for any valid input,
  // and the result is bit-for-bit identical to the naive formula
  // Not necessary for moon-distance scale, but better safe
  return (km / 1000) * 621 + (km % 1000) * 621 / 1000;
}

// ─── MET ─────────────────────────────────────────────────────────────────────
static void prv_format_met(char *buf, size_t size, time_t now) {
  int32_t sec = (int32_t)(now - (time_t)s_mission.launch_epoch);
  if (sec < 0) sec = 0;
  int32_t tm = sec / 60, m = tm % 60, th = tm / 60, h = th % 24, d = th / 24;
  snprintf(buf, size, "%dd %dh %dm", (int)d, (int)h, (int)m);
}

// ─── Milestone countdown ──────────────────────────────────────────────────────
static void prv_format_milestone_eta(char *buf, size_t size, time_t now) {
  if (s_artemis.milestone_met_ms < 0 || s_artemis.last_update_epoch == 0) {
    snprintf(buf, size, "--"); return;
  }
  int32_t sec = (int32_t)(now - (time_t)s_mission.launch_epoch);
  if (sec < 0) sec = 0;
  int32_t rem = s_artemis.milestone_met_ms - sec * 1000;
  if (rem <= 0) { snprintf(buf, size, "passed"); return; }
  int32_t rm = rem / 60000, rh = rm / 60, rd = rh / 24;
  rh = rh % 24; rm = rm % 60;
  if (rd > 0)      snprintf(buf, size, "in %dd %dh", (int)rd, (int)rh);
  else if (rh > 0) snprintf(buf, size, "in %dh %dm", (int)rh, (int)rm);
  else             snprintf(buf, size, "in %dm", (int)rm);
}

// ─── Slot rendering ───────────────────────────────────────────────────────────

// Renders one slot by its settings-array index (si).
// All FIELD_NONE slots are never created, so si is always valid here.
static void prv_render_slot(int si, time_t now) {
  FieldType ft = FIELD_NONE;
  if ((uint8_t)s_settings.slots[si] < (uint8_t)FIELD_COUNT)
    ft = (FieldType)s_settings.slots[si];
  TextLayer *lbl = s_field_label_layers[si];
  TextLayer *val = s_field_value_layers[si];
  if (!lbl || !val) return;

  if (ft == FIELD_NONE) {
    text_layer_set_text(lbl, "");
    text_layer_set_text(val, "");
    return;
  }

  strncpy(s_slot_label_bufs[si], FIELD_LABELS[ft], sizeof(s_slot_label_bufs[si]) - 1);
  text_layer_set_text(lbl, s_slot_label_bufs[si]);

  char *vbuf = s_slot_value_bufs[si];
  char num[FIELD_BUF_SIZE];

  switch (ft) {
    case FIELD_MET:
      prv_format_met(vbuf, VALUE_BUF_SIZE, now);
      break;

    case FIELD_SPEED: {
      // Speed is always positive or zero
      int32_t sx = s_artemis.speed_x100;
      if (s_settings.use_miles) {
        int32_t mx = prv_to_miles(sx);
        snprintf(vbuf, VALUE_BUF_SIZE, "%d.%02d mi/s", (int)(mx/100), (int)(mx%100));
      } else {
        snprintf(vbuf, VALUE_BUF_SIZE, "%d.%02d km/s", (int)(sx/100), (int)(sx%100));
      }
      break;
    }

    case FIELD_EARTH_DIST: {
      int32_t d = s_settings.use_miles ? prv_to_miles(s_artemis.distance_km) : s_artemis.distance_km;
      prv_format_commas(d, num, sizeof(num));
      snprintf(vbuf, VALUE_BUF_SIZE, "%s %s", num, prv_unit("km", "mi"));
      break;
    }

    case FIELD_MOON_DIST: {
      int32_t d = s_settings.use_miles ? prv_to_miles(s_artemis.moon_distance_km) : s_artemis.moon_distance_km;
      prv_format_commas(d, num, sizeof(num));
      snprintf(vbuf, VALUE_BUF_SIZE, "%s %s", num, prv_unit("km", "mi"));
      break;
    }

    case FIELD_PHASE:
      strncpy(vbuf, s_artemis.phase, VALUE_BUF_SIZE);
      vbuf[VALUE_BUF_SIZE-1] = '\0';
      break;

    case FIELD_NEXT_EVENT:
      // Label = milestone name, value = countdown
      strncpy(s_slot_label_bufs[si], s_artemis.milestone_name, sizeof(s_slot_label_bufs[si]) - 1);
      text_layer_set_text(lbl, s_slot_label_bufs[si]);
      prv_format_milestone_eta(vbuf, VALUE_BUF_SIZE, now);
      // Accent color (blue) for ETA countdown
      text_layer_set_text_color(val, ARTEMIS_COLOR_ACCENT);
      text_layer_set_text(val, vbuf);
      return;  // early return — color already set

    case FIELD_G_FORCE: {
      int32_t g = s_artemis.g_force_x10000;
      snprintf(vbuf, VALUE_BUF_SIZE, "%d.%04d g",
              (int)(g / 10000), (int)(g < 0 ? (-g) % 10000 : g % 10000));      break;
    }

    case FIELD_ALTITUDE: {
      int32_t d = s_settings.use_miles ? prv_to_miles(s_artemis.altitude_km) : s_artemis.altitude_km;
      prv_format_commas(d, num, sizeof(num));
      snprintf(vbuf, VALUE_BUF_SIZE, "%s %s", num, prv_unit("km", "mi"));
      break;
    }

    case FIELD_PERIAPSIS: {
      int32_t d = s_settings.use_miles ? prv_to_miles(s_artemis.periapsis_km) : s_artemis.periapsis_km;
      snprintf(vbuf, VALUE_BUF_SIZE, "%d %s", (int)d, prv_unit("km", "mi"));
      break;
    }

    case FIELD_APOAPSIS: {
      int32_t d = s_settings.use_miles ? prv_to_miles(s_artemis.apoapsis_km) : s_artemis.apoapsis_km;
      prv_format_commas(d, num, sizeof(num));
      snprintf(vbuf, VALUE_BUF_SIZE, "%s %s", num, prv_unit("km", "mi"));
      break;
    }

    case FIELD_SIGNAL: {
      int32_t s = s_artemis.signal_x100;
      snprintf(vbuf, VALUE_BUF_SIZE, "%d.%02d s", (int)(s/100), (int)(s%100));
      break;
    }

    case FIELD_STATION:
      strncpy(vbuf, s_artemis.dsn_station, VALUE_BUF_SIZE - 1 );
      vbuf[VALUE_BUF_SIZE - 1] = '\0';
      break;

    case FIELD_DOWNLINK: {
      int32_t kbps = s_artemis.downlink_kbps;
      if (kbps >= 1000) snprintf(vbuf, VALUE_BUF_SIZE, "%d.%d Mbps", (int)(kbps/1000), (int)((kbps%1000)/100));
      else              snprintf(vbuf, VALUE_BUF_SIZE, "%d kbps", (int)kbps);
      break;
    }

    default:
      snprintf(vbuf, VALUE_BUF_SIZE, "--");
      break;
  }

  text_layer_set_text_color(val, ARTEMIS_COLOR_VALUES);
  text_layer_set_text(val, vbuf);
}

static void prv_render_all_slots(void) {
  time_t now = time(NULL);
  for (int i = 0; i < s_num_active; i++)
      prv_render_slot(s_active_slots[i], now);
}

// ─── Post-mission stat rendering ──────────────────────────────────────────────
static void prv_populate_stat_slot(int si, const char *label, const char *value) {
  strncpy(s_slot_label_bufs[si], label, FIELD_BUF_SIZE - 1);
  s_slot_label_bufs[si][FIELD_BUF_SIZE - 1] = '\0';
  strncpy(s_slot_value_bufs[si], value, VALUE_BUF_SIZE - 1);
  s_slot_value_bufs[si][VALUE_BUF_SIZE - 1] = '\0';
  if (s_field_label_layers[si])
    text_layer_set_text(s_field_label_layers[si], s_slot_label_bufs[si]);
  if (s_field_value_layers[si]) {
    text_layer_set_text_color(s_field_value_layers[si], ARTEMIS_COLOR_VALUES);
    text_layer_set_text(s_field_value_layers[si], s_slot_value_bufs[si]);
  }
}

static void prv_render_stats(void) {
  int si = 0;
  char num[16];
  char val[VALUE_BUF_SIZE];

  if (s_mission.stats_met_s > 0) {
    int32_t ms = s_mission.stats_met_s;
    snprintf(val, sizeof(val), "%dd %dh",
             (int)(ms / 86400), (int)((ms % 86400) / 3600));
    prv_populate_stat_slot(si++, "Duration", val);
  }

  if (s_mission.stats_max_dist_km > 0) {
    int32_t d = s_settings.use_miles
        ? prv_to_miles(s_mission.stats_max_dist_km)
        : s_mission.stats_max_dist_km;
    prv_format_commas(d, num, sizeof(num));
    snprintf(val, sizeof(val), "%s %s", num, prv_unit("km", "mi"));
    prv_populate_stat_slot(si++, "Earth", val);
  }

  if (s_mission.stats_max_speed_kmh > 0) {
    int32_t s = s_settings.use_miles
        ? prv_to_miles(s_mission.stats_max_speed_kmh)
        : s_mission.stats_max_speed_kmh;
    prv_format_commas(s, num, sizeof(num));
    snprintf(val, sizeof(val), "%s %s", num, prv_unit("km/h", "mph"));
    prv_populate_stat_slot(si++, "Speed", val);
  }

  if (s_mission.stats_moon_dist_km > 0) {
    int32_t d = s_settings.use_miles
        ? prv_to_miles(s_mission.stats_moon_dist_km)
        : s_mission.stats_moon_dist_km;
    prv_format_commas(d, num, sizeof(num));
    snprintf(val, sizeof(val), "%s %s", num, prv_unit("km", "mi"));
    prv_populate_stat_slot(si++, "Moon", val);
  }
}

// ─── Decorations ──────────────────────────────────────────────────────────────
#ifdef PBL_ROUND
static int32_t prv_isqrt(int32_t n) {
  if (n <= 0) return 0;
  int32_t x = n, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + n / x) / 2; }
  return x;
}

static void prv_draw_horizontal_line(GContext *ctx, int y, int r) {
  int dy = y - r;
  int32_t hw = prv_isqrt(r * r - dy * dy) - 10;
  if (hw < 4) return;
  graphics_draw_line(ctx, GPoint(r - hw, y), GPoint(r + hw, y));
}
#endif // PBL_ROUND


static void decorations_update_proc(Layer *layer, GContext *ctx) {
#ifndef PBL_COLOR
  return;
#else
  GRect bounds = layer_get_bounds(layer);
  int layer_width = bounds.size.w;
  graphics_context_set_stroke_color(ctx, ARTEMIS_COLOR_ACCENT);
  graphics_context_set_stroke_width(ctx, 1);

#ifdef PBL_ROUND
  int layer_radius = layer_width / 2;
  // Rows fill bottom-up: pairs at bottom (widest chord), singles above.
  int num_lines   = 1 + (int)(s_num_active / 2);
  int num_singles = (s_num_active + 1) % 2 + 1;
  int num_pairs   = (s_num_active - num_singles) / 2;
  int sep = (int)(0.9 * s_split_y) / num_lines;
  // Separator line at the top of each row
  for (int i = 0; i < num_lines; i++) {
    prv_draw_horizontal_line(ctx, s_split_y - (num_lines - i) * sep, layer_radius);
  }
  // Zone separator at s_split_y
  prv_draw_horizontal_line(ctx, s_split_y, layer_radius);
  // Column divider spans pair rows at the bottom of the top zone
  if (num_pairs > 0) {
    int pair_top_y = s_split_y - num_pairs * sep;
    graphics_draw_line(ctx, GPoint(layer_radius, pair_top_y), GPoint(layer_radius, s_split_y));
  }

#else // rect
  int lm = 4, rm = layer_width - 4;
  // Zone separator: top zone / bottom zone dividing line
  graphics_draw_line(ctx, GPoint(lm, s_split_y), GPoint(rm, s_split_y));
#endif // PBL_ROUND
#endif // PBL_COLOR
}

// ─── Font selection ───────────────────────────────────────────────────────────
static GFont prv_select_font(int height, bool bold) {
  if (bold) {
    if (height >= 36) return fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    if (height >= 28) return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    if (height >= 20) return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  } else {
    if (height >= 36) return fonts_get_system_font(FONT_KEY_GOTHIC_28);
    if (height >= 28) return fonts_get_system_font(FONT_KEY_GOTHIC_24);
    if (height >= 20) return fonts_get_system_font(FONT_KEY_GOTHIC_18);
    return fonts_get_system_font(FONT_KEY_GOTHIC_14);
  }
}

static void prv_build_active_slots(void) {
  s_num_active = 0;
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (s_settings.slots[i] != FIELD_NONE) {
      s_active_slots[s_num_active++] = i;
    }
  }
}

static int prv_count_stats(void) {
  int n = 0;
  if (s_mission.stats_met_s > 0)         n++;
  if (s_mission.stats_max_dist_km > 0)   n++;
  if (s_mission.stats_max_speed_kmh > 0) n++;
  if (s_mission.stats_moon_dist_km > 0)  n++;
  return n;
}

static void prv_build_stat_slots(void) {
  s_num_active = prv_count_stats();
  for (int i = 0; i < s_num_active; i++)
    s_active_slots[i] = i;
}

// ─── Slot layer lifecycle ─────────────────────────────────────────────────────
static void prv_destroy_slots(void) {
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (s_field_label_layers[i]) {
      text_layer_destroy(s_field_label_layers[i]);
      s_field_label_layers[i] = NULL;
    }
    if (s_field_value_layers[i]) {
      text_layer_destroy(s_field_value_layers[i]);
      s_field_value_layers[i] = NULL;
    }
  }
}

// ─── Chrome: decorations layer only (battery + header removed) ───────────────
// Must be called after s_info_layer is created and slots are added, so that
// s_decorations_layer is the last child of s_info_layer (drawn on top of fields).
static void prv_create_chrome(void) {
  GRect bounds = layer_get_bounds(s_info_layer);
  int w = bounds.size.w, h = bounds.size.h;

  s_decorations_layer = layer_create(GRect(0, 0, w, h));
  layer_set_update_proc(s_decorations_layer, decorations_update_proc);
  layer_add_child(s_info_layer, s_decorations_layer);
}

// ─── Slot creation ────────────────────────────────────────────────────
// Rebuilds slot layers from scratch based on current s_settings.slots[].
// Safe to call multiple times (destroys existing layers first).
#ifdef PBL_ROUND
// ─── Slot layout: round screens ──────────────────────────────────────────────
// Rows fill bottom-up: singles at the top (chord-adapted width via prv_isqrt),
// pairs at the bottom (fixed two columns, widest chord area near s_split_y).
// This prevents round-bezel clipping at the narrow top of the top zone.
static void prv_create_slots(void) {
  if (!s_root_layer) return;
  prv_destroy_slots();
  if (s_num_active == 0) return;

  int r = s_root_w / 2;

  // DISTRIBUTION:
  //   TOTAL || SINGLE ROWS    || PAIR ROWS
  //    0    ||       0        ||     0
  //    1    ||       1        ||     0
  //    2    ||       2        ||     0
  //    3    ||       1        ||     1
  //    4    ||       2        ||     1
  //    5    ||       1        ||     2
  //    6    ||       2        ||     2
  int num_lines   = 1 + (int)(s_num_active / 2);
  int num_singles = (s_num_active + 1) % 2 + 1;
  int num_pairs   = (s_num_active - num_singles) / 2;

  int sep          = (int)(0.9 * s_split_y) / num_lines;
  int label_h      = (sep / 2) - 2;
  int val_h        = label_h;
  int val_y_offset = label_h + 2;

  int col_l_x = 10, col_w = (s_root_w / 2) - 14;
  int col_r_x = s_root_w / 2 + 4;

  GFont label_font = s_font_event;
  GFont value_font = prv_select_font(val_h, false);

  int ai = 0;

  // Singles at the top rows — chord-adapted width via prv_isqrt
  for (int s = 0; s < num_singles; s++) {
    int y  = s_split_y - (num_lines - s) * sep;
    int si = s_active_slots[ai++];
    int dy = y + label_h - r;
    int32_t hw = prv_isqrt(r * r - dy * dy) - 12;
    if (hw < 20) hw = 20;
    int sx = r - hw, sw = hw * 2;
    s_field_label_layers[si] = artemis_make_text_layer(s_info_layer, GRect(sx, y, sw, label_h),
      ARTEMIS_COLOR_ACCENT, label_font, GTextAlignmentCenter);
    s_field_value_layers[si] = artemis_make_text_layer(s_info_layer, GRect(sx, y + val_y_offset, sw, val_h),
      ARTEMIS_COLOR_VALUES, value_font, GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_field_value_layers[si], GTextOverflowModeTrailingEllipsis);
    ARTEMIS_LOG(APP_LOG_LEVEL_DEBUG, "SINGLE %d x=%d y=%d w=%d", si, sx, y, sw);
  }

  // Pairs at the bottom rows — fixed two columns at the widest chord area
  for (int p = 0; p < num_pairs; p++) {
    int y = s_split_y - (num_pairs - p) * sep;
    for (int side = 0; side < 2; side++) {
      int si = s_active_slots[ai++];
      int cx = (side == 0) ? col_l_x : col_r_x;
      s_field_label_layers[si] = artemis_make_text_layer(s_info_layer, GRect(cx, y, col_w, label_h),
        ARTEMIS_COLOR_ACCENT, label_font, GTextAlignmentCenter);
      s_field_value_layers[si] = artemis_make_text_layer(s_info_layer, GRect(cx, y + val_y_offset, col_w, val_h),
        ARTEMIS_COLOR_VALUES, value_font, GTextAlignmentCenter);
      text_layer_set_overflow_mode(s_field_value_layers[si], GTextOverflowModeTrailingEllipsis);
      ARTEMIS_LOG(APP_LOG_LEVEL_DEBUG, "PAIR %d cx=%d y=%d", si, cx, y);
    }
  }
}
#else
// ─── Slot layout: rect screens ────────────────────────────────────────────────
// Rows fill the area below the chrome. Font size scales with active slot count.
// chrome_bottom: first Y pixel available for data rows.
static void prv_create_slots(void) {
  if (!s_root_layer) return;
  prv_destroy_slots();
  if (s_num_active == 0) return;

  int avail = s_split_y;
  int rh = avail / s_num_active;

  // Label column: fixed 40% of width, value gets the rest
  int lw = s_root_w * 40 / 100;
  int vx = lw + 4, vw = s_root_w - vx - 4;

  GFont label_font = s_font_event;
  GFont value_font = prv_select_font(rh, false);

  for (int i = 0; i < s_num_active; i++) {
    int si = s_active_slots[i];  // index into s_settings.slots[]
    int y = i * rh;
    s_field_label_layers[si] = artemis_make_text_layer(s_info_layer, GRect(4, y, lw - 4, rh),
      ARTEMIS_COLOR_ACCENT, label_font, GTextAlignmentLeft);
    s_field_value_layers[si] = artemis_make_text_layer(s_info_layer, GRect(vx, y, vw, rh),
      ARTEMIS_COLOR_VALUES, value_font, GTextAlignmentRight);
    text_layer_set_overflow_mode(s_field_value_layers[si], GTextOverflowModeTrailingEllipsis);
  }
}
#endif

// ─── Public API ───────────────────────────────────────────────────────────────
void artemis_info_create(Layer *root) {
  for (int i = 0; i < MAX_SLOTS; i++) {
    s_field_label_layers[i] = NULL;
    s_field_value_layers[i] = NULL;
  }

  // Info layer: top-zone container. +1 px so zone-separator at s_split_y is not clipped.
  s_info_layer = layer_create(GRect(0, 0, s_root_w, s_split_y + 1));
  layer_add_child(root, s_info_layer);

  MissionPhase phase = prv_get_mission_phase();
  s_last_phase     = phase;
  s_stats_rendered = false;

  if (phase == MISSION_PHASE_ACTIVE) {
    prv_build_active_slots();
    prv_create_slots();
    prv_create_chrome();
  } else if (phase == MISSION_PHASE_COMPLETE && prv_count_stats() > 0) {
    prv_build_stat_slots();
    prv_create_slots();
    prv_create_chrome();
  } else {
    // PRELAUNCH or COMPLETE without stats: phase overlay text only.
    s_phase_overlay = artemis_event_create(s_info_layer);
  }
}

void artemis_info_destroy(void) {
  artemis_event_destroy(s_phase_overlay);
  s_phase_overlay = NULL;
  if (s_decorations_layer) { layer_destroy(s_decorations_layer); s_decorations_layer = NULL; }
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (s_field_label_layers[i]) { text_layer_destroy(s_field_label_layers[i]); s_field_label_layers[i] = NULL; }
    if (s_field_value_layers[i]) { text_layer_destroy(s_field_value_layers[i]); s_field_value_layers[i] = NULL; }
  }
  if (s_info_layer) { layer_destroy(s_info_layer); s_info_layer = NULL; }
}

void artemis_info_show(void) {
  if (s_info_layer) layer_set_hidden(s_info_layer, false);
}

void artemis_info_hide(void) {
  if (s_info_layer) layer_set_hidden(s_info_layer, true);
}

void artemis_info_refresh(void) {
  if (!s_info_layer) return;
  MissionPhase phase = prv_get_mission_phase();

  // Mid-session ACTIVE → COMPLETE transition: rebuild layer set for the new phase.
  if (s_last_phase == MISSION_PHASE_ACTIVE && phase == MISSION_PHASE_COMPLETE) {
    prv_destroy_slots();
    if (s_decorations_layer) { layer_destroy(s_decorations_layer); s_decorations_layer = NULL; }
    if (prv_count_stats() > 0) {
      prv_build_stat_slots();
      prv_create_slots();
      prv_create_chrome();
    } else {
      s_phase_overlay = artemis_event_create(s_info_layer);
    }
    s_stats_rendered = false;
    s_last_phase = phase;
  }

  if (phase == MISSION_PHASE_ACTIVE) {
    if (s_num_active > 0) prv_render_all_slots();
  } else if (phase == MISSION_PHASE_COMPLETE && prv_count_stats() > 0) {
    if (!s_stats_rendered) {
      prv_render_stats();
      s_stats_rendered = true;
    }
  } else {
    // PRELAUNCH or COMPLETE without stats: lazily create overlay if needed.
    if (!s_phase_overlay)
      s_phase_overlay = artemis_event_create(s_info_layer);
    artemis_event_show(s_phase_overlay, prv_build_phase_text(phase), false);
  }
}

void artemis_info_rebuild_slots(void) {
  MissionPhase phase = prv_get_mission_phase();
  if (phase == MISSION_PHASE_ACTIVE) {
    prv_build_active_slots();
    prv_create_slots();
    return;
  }
  if (phase != MISSION_PHASE_COMPLETE) return;

  bool want_stats  = (prv_count_stats() > 0);
  bool have_layout = (s_phase_overlay == NULL);  // built as stat slots, not overlay

  if (want_stats != have_layout) {
    // Layer-set mismatch: a mission sync just made stats available (or, in
    // principle, took them away) while the COMPLETE-phase layer set built at
    // artemis_info_create()/last transition no longer matches. Tear down and
    // rebuild for the layout prv_count_stats() now calls for — same recipe as
    // the ACTIVE→COMPLETE transition in artemis_info_refresh().
    prv_destroy_slots();
    if (s_decorations_layer) { layer_destroy(s_decorations_layer); s_decorations_layer = NULL; }
    artemis_event_destroy(s_phase_overlay);
    s_phase_overlay = NULL;
    if (want_stats) {
      prv_build_stat_slots();
      prv_create_slots();
      prv_create_chrome();
    } else {
      s_phase_overlay = artemis_event_create(s_info_layer);
    }
    s_stats_rendered = false;
  } else if (want_stats) {
    s_stats_rendered = false;  // force re-render on next refresh (e.g. units changed)
  }
}
