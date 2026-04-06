#include <pebble.h>

// ─── Persistent storage ───────────────────────────────────────────────────────
#define SETTINGS_KEY  1
#define ARTEMIS_KEY   2
#define SETTINGS_VERSION 2  // bump when struct changes to force reset

// ─── Mission constants ────────────────────────────────────────────────────────
#define LAUNCH_EPOCH       ((time_t)1775082900)  // Apr 1 2026 22:35 UTC
#define MISSION_END_HOURS  229

// ─── Special events ───────────────────────────────────────────────────────────
// Each event is shown for EVENT_DISPLAY_S seconds after its epoch, then dismissed.
// All times are UTC unix epochs. EDT = UTC-4.
// Source: https://www.nasa.gov/missions/nasa-answers-your-most-pressing-artemis-ii-questions
#define EVENT_DISPLAY_S  (5 * 60)  // each event banner shows for 5 minutes

typedef struct {
  uint32_t    epoch;    // UTC unix timestamp when event becomes active
  const char *message;  // text to display (short, fits in large font)
} SpecialEvent;

static const SpecialEvent s_special_events[] = {
  { 1775501100UL, "MOON OBS.\nBEGINS"       },  // Apr 6 18:45 UTC (2:45 PM EDT)
  { 1775515620UL, "BEHIND\nTHE MOON"        },  // Apr 6 22:47 UTC (6:47 PM EDT)
  { 1775516520UL, "CLOSEST\nTO MOON"        },  // Apr 6 23:02 UTC (7:02 PM EDT)
  { 1775516700UL, "MAX DIST\nFROM EARTH"    },  // Apr 6 23:05 UTC (7:05 PM EDT)
  { 1775518020UL, "SIGNAL\nRESTORED"        },  // Apr 6 23:27 UTC (7:27 PM EDT)
  { 1775524800UL, "MOON OBS.\nENDS"         },  // Apr 7 01:20 UTC (9:20 PM EDT)
};
#define NUM_SPECIAL_EVENTS ((int)(sizeof(s_special_events) / sizeof(s_special_events[0])))

// Returns the active special event message, or NULL if none active
static const char *prv_get_special_event(void) {
  uint32_t now = (uint32_t)time(NULL);
  for (int i = 0; i < NUM_SPECIAL_EVENTS; i++) {
    uint32_t start = s_special_events[i].epoch;
    uint32_t end   = start + EVENT_DISPLAY_S;
    if (now >= start && now < end) {
      return s_special_events[i].message;
    }
  }
  return NULL;
}

// ─── Field types ──────────────────────────────────────────────────────────────
typedef enum {
  FIELD_NONE         = 0,
  FIELD_MET          = 1,
  FIELD_SPEED        = 2,
  FIELD_EARTH_DIST   = 3,
  FIELD_MOON_DIST    = 4,
  FIELD_PHASE        = 5,
  FIELD_NEXT_EVENT   = 6,
  FIELD_G_FORCE      = 7,
  FIELD_ALTITUDE     = 8,
  FIELD_PERIAPSIS    = 9,
  FIELD_APOAPSIS     = 10,
  FIELD_SIGNAL       = 11,
  FIELD_STATION      = 12,
  FIELD_DOWNLINK     = 13,
  FIELD_COUNT        = 14
} FieldType;

// ─── Platform slot count ──────────────────────────────────────────────────────
#define MAX_SLOTS 6
#if defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_CHALK)
  #define NUM_SLOTS 5
#else
  #define NUM_SLOTS 6
#endif

// ─── Color scheme ─────────────────────────────────────────────────────────────
typedef struct {
  GColor background;
  GColor accent;       // title, labels, separators
  GColor values;       // time, field values
  GColor highlights;   // ETA, battery, live indicator
  GColor graph_pending;
  GColor status_complete;
} ColorScheme;

static ColorScheme s_colors;

// ─── Settings ────────────────────────────────────────────────────────────────
typedef struct {
  uint8_t  version;
  int32_t  update_interval_min;
  bool     use_miles;
  uint8_t  slots[MAX_SLOTS];
  uint8_t  color_theme;
  uint32_t color_background;
  uint32_t color_accent;
  uint32_t color_values;
  uint32_t color_highlights;
  bool     vibrate_events;
} ArtemisSettings;

static ArtemisSettings s_settings;

// ─── Artemis data ─────────────────────────────────────────────────────────────
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
} ArtemisData;

static ArtemisData s_artemis;

// ─── Layers ───────────────────────────────────────────────────────────────────
static Window    *s_main_window;
static Layer     *s_decorations_layer;
static Layer     *s_battery_layer;
static TextLayer *s_header_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_field_label_layers[MAX_SLOTS];
static TextLayer *s_field_value_layers[MAX_SLOTS];
static TextLayer *s_event_overlay_layer;  // full-screen special event message

static int s_battery_level = 100;

// ─── Buffers ──────────────────────────────────────────────────────────────────
static char s_time_buf[8];
static char s_date_buf[24];
static char s_met_buf[16];
static char s_slot_label_bufs[MAX_SLOTS][20];
static char s_slot_value_bufs[MAX_SLOTS][24];

// ─── Default settings ─────────────────────────────────────────────────────────
static void prv_default_settings(void) {
  s_settings.version              = SETTINGS_VERSION;
  s_settings.update_interval_min  = 30;
  s_settings.use_miles            = false;
  s_settings.color_theme          = 0;
  s_settings.color_background     = 0x000000;
  s_settings.color_accent         = 0x55FFFF;
  s_settings.color_values         = 0xFFFFFF;
  s_settings.color_highlights     = 0xFFFF00;
  s_settings.vibrate_events       = true;
#if defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_CHALK)
  s_settings.slots[0] = FIELD_MET;
  s_settings.slots[1] = FIELD_EARTH_DIST;
  s_settings.slots[2] = FIELD_MOON_DIST;
  s_settings.slots[3] = FIELD_PHASE;
  s_settings.slots[4] = FIELD_NEXT_EVENT;
  s_settings.slots[5] = FIELD_NONE;
#else
  s_settings.slots[0] = FIELD_MET;
  s_settings.slots[1] = FIELD_SPEED;
  s_settings.slots[2] = FIELD_EARTH_DIST;
  s_settings.slots[3] = FIELD_MOON_DIST;
  s_settings.slots[4] = FIELD_PHASE;
  s_settings.slots[5] = FIELD_NEXT_EVENT;
#endif
}

static void prv_load_settings(void) {
  prv_default_settings();
  ArtemisSettings loaded;
  if (persist_read_data(SETTINGS_KEY, &loaded, sizeof(loaded)) > 0
      && loaded.version == SETTINGS_VERSION) {
    s_settings = loaded;
  }
}

static void prv_default_artemis(void) {
  strncpy(s_artemis.phase, "...", sizeof(s_artemis.phase) - 1);
  strncpy(s_artemis.milestone_name, "...", sizeof(s_artemis.milestone_name) - 1);
  strncpy(s_artemis.dsn_station, "...", sizeof(s_artemis.dsn_station) - 1);
  s_artemis.speed_x100       = 0;
  s_artemis.distance_km      = 0;
  s_artemis.moon_distance_km = 0;
  s_artemis.milestone_met_ms = -1;
  s_artemis.mission_complete = false;
  s_artemis.last_update_epoch = 0;
  s_artemis.g_force_x10000  = 0;
  s_artemis.altitude_km     = 0;
  s_artemis.periapsis_km    = 0;
  s_artemis.apoapsis_km     = 0;
  s_artemis.signal_x100     = 0;
  s_artemis.downlink_kbps   = 0;
}

static void prv_load_artemis(void) {
  prv_default_artemis();
  persist_read_data(ARTEMIS_KEY, &s_artemis, sizeof(s_artemis));
}

// ─── Color scheme ─────────────────────────────────────────────────────────────
static void prv_apply_theme(uint8_t theme, uint32_t bg, uint32_t ac, uint32_t val, uint32_t hi) {
#ifdef PBL_COLOR
  switch (theme) {
    case 0: // Space (default)
      s_colors.background = GColorBlack;
      s_colors.accent     = GColorCeleste;
      s_colors.values     = GColorWhite;
      s_colors.highlights = GColorYellow;
      break;
    case 1: // Dark
      s_colors.background = GColorFromHEX(0x1A1A2E);
      s_colors.accent     = GColorFromHEX(0x4A90D9);
      s_colors.values     = GColorLightGray;
      s_colors.highlights = GColorCyan;
      break;
    case 2: // Clear
      s_colors.background = GColorWhite;
      s_colors.accent     = GColorFromHEX(0x003399);
      s_colors.values     = GColorBlack;
      s_colors.highlights = GColorOrange;
      break;
    case 3: // B&W
      s_colors.background = GColorBlack;
      s_colors.accent     = GColorWhite;
      s_colors.values     = GColorWhite;
      s_colors.highlights = GColorWhite;
      break;
    case 4: // NASA
      s_colors.background = GColorFromHEX(0x0B1F3A);
      s_colors.accent     = GColorFromHEX(0xFFB300);
      s_colors.values     = GColorWhite;
      s_colors.highlights = GColorOrange;
      break;
    case 5: // Custom
    default:
      s_colors.background = GColorFromHEX(bg);
      s_colors.accent     = GColorFromHEX(ac);
      s_colors.values     = GColorFromHEX(val);
      s_colors.highlights = GColorFromHEX(hi);
      break;
  }
  s_colors.graph_pending   = GColorDarkGray;
  s_colors.status_complete = GColorLightGray;
#else
  s_colors.background      = GColorBlack;
  s_colors.accent          = GColorWhite;
  s_colors.values          = GColorWhite;
  s_colors.highlights      = GColorWhite;
  s_colors.graph_pending   = GColorDarkGray;
  s_colors.status_complete = GColorLightGray;
#endif
}

static void prv_build_color_scheme(void) {
  prv_apply_theme(s_settings.color_theme,
                  s_settings.color_background,
                  s_settings.color_accent,
                  s_settings.color_values,
                  s_settings.color_highlights);
  window_set_background_color(s_main_window, s_colors.background);
}

// ─── Format helpers ───────────────────────────────────────────────────────────
static void prv_format_commas(int32_t value, char *buf, size_t size) {
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%d", (int)value);
  int len = strlen(tmp);
  int out = 0, first = len - ((len - 1) / 3) * 3;
  for (int i = 0; i < len && out < (int)size - 1; i++) {
    if (i > 0 && (i - first) % 3 == 0 && out < (int)size - 2) buf[out++] = ',';
    buf[out++] = tmp[i];
  }
  buf[out] = '\0';
}

static int32_t prv_isqrt(int32_t n) {
  if (n <= 0) return 0;
  int32_t x = n, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + n / x) / 2; }
  return x;
}

static const char *prv_unit(const char *km_unit, const char *mi_unit) {
  return s_settings.use_miles ? mi_unit : km_unit;
}

static int32_t prv_to_miles(int32_t km) {
  return (km * 621) / 1000;
}

// ─── MET ─────────────────────────────────────────────────────────────────────
static void prv_format_met(char *buf, size_t size) {
  time_t now = time(NULL);
  int32_t sec = (int32_t)(now - LAUNCH_EPOCH);
  if (sec < 0) sec = 0;
  int32_t tm = sec / 60, m = tm % 60, th = tm / 60, h = th % 24, d = th / 24;
  snprintf(buf, size, "%dd %dh %dm", (int)d, (int)h, (int)m);
}

static void prv_calculate_met(void) {
  prv_format_met(s_met_buf, sizeof(s_met_buf));
}

// ─── Milestone countdown ──────────────────────────────────────────────────────
static void prv_format_milestone_eta(char *buf, size_t size) {
  if (s_artemis.milestone_met_ms < 0 || s_artemis.last_update_epoch == 0) {
    snprintf(buf, size, "--"); return;
  }
  time_t now = time(NULL);
  int32_t sec = (int32_t)(now - LAUNCH_EPOCH);
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
static const char *s_field_labels[FIELD_COUNT] = {
  "", "MET", "SPEED", "EARTH", "MOON", "PHASE", "NEXT EVENT",
  "G-FORCE", "ALTITUDE", "PERIAPSIS", "APOAPSIS", "SIGNAL", "STATION", "DOWNLINK"
};

static void prv_render_slot(int i) {
  FieldType ft = (FieldType)s_settings.slots[i];
  TextLayer *lbl = s_field_label_layers[i];
  TextLayer *val = s_field_value_layers[i];
  if (!lbl || !val) return;

  if (ft == FIELD_NONE) {
    text_layer_set_text(lbl, "");
    text_layer_set_text(val, "");
    return;
  }

  strncpy(s_slot_label_bufs[i], s_field_labels[ft], sizeof(s_slot_label_bufs[i]) - 1);
  text_layer_set_text(lbl, s_slot_label_bufs[i]);

  char *vbuf = s_slot_value_bufs[i];
  char num[20];

  switch (ft) {
    case FIELD_MET:
      prv_format_met(vbuf, 24);
      break;

    case FIELD_SPEED: {
      int32_t sx = s_artemis.speed_x100;
      if (s_settings.use_miles) {
        int32_t mx = (sx * 621) / 1000;
        snprintf(vbuf, 24, "%d.%02d mi/s", (int)(mx/100), (int)(mx%100));
      } else {
        snprintf(vbuf, 24, "%d.%02d km/s", (int)(sx/100), (int)(sx%100));
      }
      break;
    }

    case FIELD_EARTH_DIST: {
      int32_t d = s_settings.use_miles ? prv_to_miles(s_artemis.distance_km) : s_artemis.distance_km;
      prv_format_commas(d, num, sizeof(num));
      snprintf(vbuf, 24, "%s %s", num, prv_unit("km", "mi"));
      break;
    }

    case FIELD_MOON_DIST: {
      int32_t d = s_settings.use_miles ? prv_to_miles(s_artemis.moon_distance_km) : s_artemis.moon_distance_km;
      prv_format_commas(d, num, sizeof(num));
      snprintf(vbuf, 24, "%s %s", num, prv_unit("km", "mi"));
      break;
    }

    case FIELD_PHASE:
      strncpy(vbuf, s_artemis.phase, 23);
      vbuf[23] = '\0';
      break;

    case FIELD_NEXT_EVENT:
      // Label = milestone name, value = countdown
      strncpy(s_slot_label_bufs[i], s_artemis.milestone_name, sizeof(s_slot_label_bufs[i]) - 1);
      text_layer_set_text(lbl, s_slot_label_bufs[i]);
      prv_format_milestone_eta(vbuf, 24);
      // Highlight color for ETA
      text_layer_set_text_color(val, s_colors.highlights);
      text_layer_set_text(val, vbuf);
      return;  // early return — color already set

    case FIELD_G_FORCE: {
      int32_t g = s_artemis.g_force_x10000;
      snprintf(vbuf, 24, "0.%04d g", (int)(g < 0 ? -g : g));
      break;
    }

    case FIELD_ALTITUDE: {
      int32_t d = s_settings.use_miles ? prv_to_miles(s_artemis.altitude_km) : s_artemis.altitude_km;
      prv_format_commas(d, num, sizeof(num));
      snprintf(vbuf, 24, "%s %s", num, prv_unit("km", "mi"));
      break;
    }

    case FIELD_PERIAPSIS: {
      int32_t d = s_settings.use_miles ? prv_to_miles(s_artemis.periapsis_km) : s_artemis.periapsis_km;
      snprintf(vbuf, 24, "%d %s", (int)d, prv_unit("km", "mi"));
      break;
    }

    case FIELD_APOAPSIS: {
      int32_t d = s_settings.use_miles ? prv_to_miles(s_artemis.apoapsis_km) : s_artemis.apoapsis_km;
      prv_format_commas(d, num, sizeof(num));
      snprintf(vbuf, 24, "%s %s", num, prv_unit("km", "mi"));
      break;
    }

    case FIELD_SIGNAL: {
      int32_t s = s_artemis.signal_x100;
      snprintf(vbuf, 24, "%d.%02d s", (int)(s/100), (int)(s%100));
      break;
    }

    case FIELD_STATION:
      strncpy(vbuf, s_artemis.dsn_station, 23);
      vbuf[23] = '\0';
      break;

    case FIELD_DOWNLINK: {
      int32_t kbps = s_artemis.downlink_kbps;
      if (kbps >= 1000) snprintf(vbuf, 24, "%d.%d Mbps", (int)(kbps/1000), (int)((kbps%1000)/100));
      else              snprintf(vbuf, 24, "%d kbps", (int)kbps);
      break;
    }

    default:
      snprintf(vbuf, 24, "--");
      break;
  }

  text_layer_set_text_color(val, s_colors.values);
  text_layer_set_text(val, vbuf);
}

static void prv_render_all_slots(void) {
  for (int i = 0; i < NUM_SLOTS; i++) prv_render_slot(i);
}

// Show or hide the special event overlay over the field area
static const char *s_active_event_msg = NULL;  // tracks currently shown event

static void prv_update_event_overlay(void) {
  if (!s_event_overlay_layer) return;
  const char *msg = prv_get_special_event();
  bool showing = (msg != NULL);

  // Vibrate only on transition to a new event
  if (msg != s_active_event_msg && msg != NULL && s_settings.vibrate_events) {
    static const uint32_t segments[] = { 200, 100, 200, 100, 400 };
    VibePattern pat = { .durations = segments, .num_segments = 5 };
    vibes_enqueue_custom_pattern(pat);
  }
  s_active_event_msg = msg;

  layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), !showing);
  layer_set_hidden(s_decorations_layer, showing);
  for (int i = 0; i < NUM_SLOTS; i++) {
    if (s_field_label_layers[i])
      layer_set_hidden(text_layer_get_layer(s_field_label_layers[i]), showing);
    if (s_field_value_layers[i])
      layer_set_hidden(text_layer_get_layer(s_field_value_layers[i]), showing);
  }

  if (showing) text_layer_set_text(s_event_overlay_layer, msg);
}

// ─── Request data from phone ──────────────────────────────────────────────────
static void prv_request_artemis_data(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_ARTEMIS, 1);
    app_message_outbox_send();
  }
}

// ─── Time & date ──────────────────────────────────────────────────────────────
static void prv_update_time(struct tm *tick_time) {
  strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buf);
  strftime(s_date_buf, sizeof(s_date_buf), "%a, %b %d", tick_time);
  text_layer_set_text(s_date_layer, s_date_buf);
}

// ─── Tick handler ─────────────────────────────────────────────────────────────
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time(tick_time);
  prv_calculate_met();
  prv_update_event_overlay();
  prv_render_all_slots();

  if (!s_artemis.mission_complete) {
    int iv = (int)s_settings.update_interval_min;
    if (iv < 1) iv = 30;
    if (tick_time->tm_min % iv == 0) prv_request_artemis_data();
  }
}

// ─── Battery ──────────────────────────────────────────────────────────────────
static void battery_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
#ifdef PBL_ROUND
  int32_t a_start = DEG_TO_TRIGANGLE(-90);
  int32_t a_end   = DEG_TO_TRIGANGLE(-90 + (s_battery_level * 360) / 100);
#ifdef PBL_PLATFORM_GABBRO
  int inset = 8;
#else
  int inset = 5;
#endif
#ifdef PBL_COLOR
  GColor arc_color = (s_battery_level <= 20) ? GColorRed :
                     (s_battery_level <= 40) ? GColorChromeYellow : s_colors.highlights;
  graphics_context_set_fill_color(ctx, arc_color);
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
#endif
  graphics_fill_radial(ctx, bounds, GOvalScaleModeFitCircle, inset, a_start, a_end);
  graphics_context_set_fill_color(ctx, s_colors.graph_pending);
  graphics_fill_radial(ctx, bounds, GOvalScaleModeFitCircle, inset, a_end, DEG_TO_TRIGANGLE(270));
#else
  int bar_w = (bounds.size.w * s_battery_level) / 100;
  graphics_context_set_fill_color(ctx, s_colors.graph_pending);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#ifdef PBL_COLOR
  GColor bar_color = (s_battery_level <= 20) ? GColorRed :
                     (s_battery_level <= 40) ? GColorChromeYellow : s_colors.highlights;
  graphics_context_set_fill_color(ctx, bar_color);
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
#endif
  graphics_fill_rect(ctx, GRect(0, 0, bar_w, bounds.size.h), 0, GCornerNone);
#endif
}

static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  layer_mark_dirty(s_battery_layer);
}

// ─── Decorations ──────────────────────────────────────────────────────────────
#ifdef PBL_ROUND
static void prv_draw_safe_line(GContext *ctx, int y, int r) {
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
  graphics_context_set_stroke_color(ctx, s_colors.accent);
  graphics_context_set_stroke_width(ctx, 1);

#ifdef PBL_ROUND
  int r = layer_get_bounds(layer).size.w / 2;
  // Separators: after date, between sections, before milestone
#ifdef PBL_PLATFORM_CHALK
  prv_draw_safe_line(ctx, 74, r);   // after date
  prv_draw_safe_line(ctx, 101, r);  // between slot 0-1 and slot 2-3
  prv_draw_safe_line(ctx, 128, r);  // between slot 2-3 and slot 4
  prv_draw_safe_line(ctx, 156, r);  // after slot 4
  // Column dividers for pairs
  graphics_draw_line(ctx, GPoint(90, 75),  GPoint(90, 100));
  graphics_draw_line(ctx, GPoint(90, 102), GPoint(90, 127));
#else // gabbro
  prv_draw_safe_line(ctx, 100, r);  // after date
  prv_draw_safe_line(ctx, 136, r);  // between slot 0-1 and slot 2-3
  prv_draw_safe_line(ctx, 172, r);  // between slot 2-3 and slot 4
  prv_draw_safe_line(ctx, 210, r);  // between slot 4 and slot 5
  // Column dividers for pairs
  graphics_draw_line(ctx, GPoint(130, 101), GPoint(130, 135));
  graphics_draw_line(ctx, GPoint(130, 137), GPoint(130, 171));
#endif

#else // rect
  int lm, rm;
#ifdef PBL_PLATFORM_EMERY
  lm = 8; rm = 192;
#else
  lm = 4; rm = 140;
#endif
  // Separator after date block
#ifdef PBL_PLATFORM_EMERY
  graphics_draw_line(ctx, GPoint(lm, 94), GPoint(rm, 94));
#else
  graphics_draw_line(ctx, GPoint(lm, 80), GPoint(rm, 80));
#endif
#endif // PBL_ROUND
#endif // PBL_COLOR
}

// ─── Layer creation helper ────────────────────────────────────────────────────
static TextLayer *prv_make_layer(Layer *root, GRect r, GColor col,
                                 const char *font, GTextAlignment align) {
  TextLayer *tl = text_layer_create(r);
  text_layer_set_background_color(tl, GColorClear);
  text_layer_set_text_color(tl, col);
  text_layer_set_font(tl, fonts_get_system_font(font));
  text_layer_set_text_alignment(tl, align);
  layer_add_child(root, text_layer_get_layer(tl));
  return tl;
}

// ─── Layout: Emery (200×228) ──────────────────────────────────────────────────
#ifdef PBL_PLATFORM_EMERY
static void prv_create_layout(Layer *root) {
  // Chrome: battery(3) + header(14) + gap(2) + time(52) + date(18) + sep(2) = 91px
  s_battery_layer = layer_create(GRect(0, 0, 200, 3));
  s_header_layer  = prv_make_layer(root, GRect(0, 4, 200, 14),
                     s_colors.accent, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
  s_time_layer    = prv_make_layer(root, GRect(0, 19, 200, 52),
                     s_colors.values, FONT_KEY_LECO_42_NUMBERS, GTextAlignmentCenter);
  s_date_layer    = prv_make_layer(root, GRect(0, 65, 200, 18),
                     GColorLightGray, FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);

  s_decorations_layer = layer_create(GRect(0, 0, 200, 228));

  // 6 data rows, Y=96 onward, row height 22px  → 6×22=132, 96+132=228 ✓
  int base = 96, rh = 22;
  for (int i = 0; i < NUM_SLOTS; i++) {
    int y = base + i * rh;
    s_field_label_layers[i] = prv_make_layer(root, GRect(4, y, 56, rh),
      s_colors.accent, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentLeft);
    s_field_value_layers[i] = prv_make_layer(root, GRect(60, y, 136, rh),
      s_colors.values, FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentRight);
    text_layer_set_overflow_mode(s_field_value_layers[i], GTextOverflowModeTrailingEllipsis);
  }
}
#endif

// ─── Layout: Aplite/Basalt (144×168) ─────────────────────────────────────────
#if defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_BASALT)
static void prv_create_layout(Layer *root) {
  // Chrome: battery(3) + header(14) + gap(1) + time(42) + date(16) + sep(4) = 80px
  s_battery_layer = layer_create(GRect(0, 0, 144, 3));
  s_header_layer  = prv_make_layer(root, GRect(0, 4, 144, 14),
                     s_colors.accent, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
  s_time_layer    = prv_make_layer(root, GRect(0, 19, 144, 42),
                     s_colors.values, FONT_KEY_LECO_38_BOLD_NUMBERS, GTextAlignmentCenter);
  s_date_layer    = prv_make_layer(root, GRect(0, 59, 144, 14),
                     GColorLightGray, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);

  s_decorations_layer = layer_create(GRect(0, 0, 144, 168));

  // 5 data rows, Y=82, row height 17px → 5×17=85, 82+85=167 ✓ (1px margin)
  int base = 82, rh = 17;
  for (int i = 0; i < NUM_SLOTS; i++) {
    int y = base + i * rh;
    s_field_label_layers[i] = prv_make_layer(root, GRect(4, y, 52, rh),
      s_colors.accent, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentLeft);
    s_field_value_layers[i] = prv_make_layer(root, GRect(56, y, 84, rh),
      s_colors.values, FONT_KEY_GOTHIC_14, GTextAlignmentRight);
    text_layer_set_overflow_mode(s_field_value_layers[i], GTextOverflowModeTrailingEllipsis);
  }
  // unused slot 5
  s_field_label_layers[5] = NULL;
  s_field_value_layers[5] = NULL;
}
#endif

// ─── Layout: Chalk (180×180 round) ───────────────────────────────────────────
#ifdef PBL_PLATFORM_CHALK
// Layout: pair(0,1) | pair(2,3) | single(4)
// Chrome ends at Y=82; section heights: 30+30+26=86; 82+86=168 ≤ 180 ✓
// Safe col widths per safe-zone math: left col GRect(10,y,80,h), right GRect(90,y,80,h)
static void prv_create_layout(Layer *root) {
  s_battery_layer = layer_create(GRect(0, 0, 180, 180));
  s_header_layer  = prv_make_layer(root, GRect(40, 6, 100, 14),
                     s_colors.accent, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
  s_time_layer    = prv_make_layer(root, GRect(14, 20, 152, 36),
                     s_colors.values, FONT_KEY_LECO_32_BOLD_NUMBERS, GTextAlignmentCenter);
  s_date_layer    = prv_make_layer(root, GRect(20, 55, 140, 18),
                     GColorLightGray, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);

  s_decorations_layer = layer_create(GRect(0, 0, 180, 180));

  // Chrome ends at Y=75. Available: 180-75=105px
  // Pair 0+1: Y=76, label h=13, value h=13 → 26px, ends Y=102
  // Pair 2+3: Y=103, 26px, ends Y=129
  // Single 4: Y=131, label h=13, value h=13 → ends Y=157
  // Total: 75+26+26+26=153 ≤ 180 ✓
  int pair_ys[2]   = { 74, 100 };
  int single_y     = 126;
  int col_l_x = 10, col_r_x = 92, col_w = 78;

  for (int i = 0; i < NUM_SLOTS; i++) {
    GRect lr, vr;
    if (i < 4) {
      int pair = i / 2;
      int side = i % 2;
      int cx   = (side == 0) ? col_l_x : col_r_x;
      int y    = pair_ys[pair];
      lr = GRect(cx, y,      col_w, 13);
      vr = GRect(cx, y + 13, col_w, 13);
    } else {
      lr = GRect(10, single_y,      160, 13);
      vr = GRect(10, single_y + 13, 160, 13);
    }
    s_field_label_layers[i] = prv_make_layer(root, lr,
      s_colors.accent, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
    s_field_value_layers[i] = prv_make_layer(root, vr,
      s_colors.values, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_field_value_layers[i], GTextOverflowModeTrailingEllipsis);
  }
  s_field_label_layers[5] = NULL;
  s_field_value_layers[5] = NULL;
}
#endif

// ─── Layout: Gabbro (260×260 round) ──────────────────────────────────────────
#ifdef PBL_PLATFORM_GABBRO
// Layout: pair(0,1) | pair(2,3) | single(4) | single(5)
// Chrome: battery arc + header(14) + gap + time(52) + date(18) + sep = ~104px
// Data: 2 pairs×32 + 2 singles×24 = 112px; 104+112=216 ≤ 260 ✓
static void prv_create_layout(Layer *root) {
  s_battery_layer = layer_create(GRect(0, 0, 260, 260));
  s_header_layer  = prv_make_layer(root, GRect(70, 8, 120, 14),
                     s_colors.accent, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
  s_time_layer    = prv_make_layer(root, GRect(28, 24, 204, 52),
                     s_colors.values, FONT_KEY_LECO_42_NUMBERS, GTextAlignmentCenter);
  s_date_layer    = prv_make_layer(root, GRect(14, 65, 232, 18),
                     GColorLightGray, FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);

  s_decorations_layer = layer_create(GRect(0, 0, 260, 260));

  // Pairs: Y=104 and Y=138; each = label(14)+value(18) = 32px tall
  // Singles: Y=172 and Y=196; each = label(12)+value(16) = 28px
  int pair_ys[2]  = { 102, 138 };
  int single_ys[2]= { 174, 212 };
  int col_l_x = 14, col_r_x = 134, col_w = 112;

  for (int i = 0; i < NUM_SLOTS; i++) {
    GRect lr, vr;
    if (i < 4) {
      int pair = i / 2, side = i % 2;
      int cx = (side == 0) ? col_l_x : col_r_x;
      int y  = pair_ys[pair];
      lr = GRect(cx, y,      col_w, 14);
      vr = GRect(cx, y + 14, col_w, 18);
    } else {
      int sy = single_ys[i - 4];
      lr = GRect(20, sy,      220, 14);
      vr = GRect(20, sy + 12, 220, 20);
    }
    s_field_label_layers[i] = prv_make_layer(root, lr,
      s_colors.accent, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
    s_field_value_layers[i] = prv_make_layer(root, vr,
      s_colors.values, FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_field_value_layers[i], GTextOverflowModeTrailingEllipsis);
  }
}
#endif

// ─── AppMessage ───────────────────────────────────────────────────────────────
static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "INBOX: received");
  bool data_changed = false, cfg_changed = false;

#define FETCH_STR(key, dst) { \
  Tuple *t = dict_find(iter, MESSAGE_KEY_##key); \
  if (t) { strncpy(dst, t->value->cstring, sizeof(dst)-1); dst[sizeof(dst)-1]='\0'; data_changed=true; } }
#define FETCH_I32(key, dst) { \
  Tuple *t = dict_find(iter, MESSAGE_KEY_##key); \
  if (t) { dst = t->value->int32; data_changed=true; } }
#define FETCH_U8(key, dst) { \
  Tuple *t = dict_find(iter, MESSAGE_KEY_##key); \
  if (t) { dst = (t->value->uint8 != 0); data_changed=true; } }
#define FETCH_I32C(key, dst) { \
  Tuple *t = dict_find(iter, MESSAGE_KEY_##key); \
  if (t) { dst = t->value->int32; cfg_changed=true; } }

  FETCH_STR(ARTEMIS_PHASE, s_artemis.phase)
  FETCH_I32(ARTEMIS_SPEED, s_artemis.speed_x100)
  FETCH_I32(ARTEMIS_DISTANCE, s_artemis.distance_km)
  FETCH_I32(ARTEMIS_MOON_DIST, s_artemis.moon_distance_km)
  FETCH_STR(ARTEMIS_MILESTONE_NAME, s_artemis.milestone_name)
  FETCH_I32(ARTEMIS_MILESTONE_MET, s_artemis.milestone_met_ms)
  FETCH_I32(ARTEMIS_G_FORCE, s_artemis.g_force_x10000)
  FETCH_I32(ARTEMIS_ALTITUDE, s_artemis.altitude_km)
  FETCH_I32(ARTEMIS_PERIAPSIS, s_artemis.periapsis_km)
  FETCH_I32(ARTEMIS_APOAPSIS, s_artemis.apoapsis_km)
  FETCH_I32(ARTEMIS_SIGNAL, s_artemis.signal_x100)
  FETCH_STR(ARTEMIS_STATION, s_artemis.dsn_station)
  FETCH_I32(ARTEMIS_DOWNLINK, s_artemis.downlink_kbps)
  { Tuple *t = dict_find(iter, MESSAGE_KEY_ARTEMIS_COMPLETE);
    if (t) { s_artemis.mission_complete = (t->value->uint8 == 1); data_changed = true; } }

  if (data_changed) {
    s_artemis.last_update_epoch = (uint32_t)time(NULL);
    persist_write_data(ARTEMIS_KEY, &s_artemis, sizeof(s_artemis));
    prv_render_all_slots();
  }

  APP_LOG(APP_LOG_LEVEL_DEBUG, "INBOX: clay section");
  // Clay settings
  { Tuple *t = dict_find(iter, MESSAGE_KEY_UPDATE_INTERVAL);
    if (t) { s_settings.update_interval_min = t->value->int32; cfg_changed = true; } }
  { Tuple *t = dict_find(iter, MESSAGE_KEY_USE_MILES);
    if (t) { s_settings.use_miles = (t->value->uint8 != 0); cfg_changed = true; } }
  { Tuple *t = dict_find(iter, MESSAGE_KEY_VIBRATE_EVENTS);
    if (t) { s_settings.vibrate_events = (t->value->uint8 != 0); cfg_changed = true; } }

  // Slot config
  uint32_t slot_keys[6] = {
    MESSAGE_KEY_SLOT_1, MESSAGE_KEY_SLOT_2, MESSAGE_KEY_SLOT_3,
    MESSAGE_KEY_SLOT_4, MESSAGE_KEY_SLOT_5, MESSAGE_KEY_SLOT_6
  };
  for (int i = 0; i < MAX_SLOTS; i++) {
    Tuple *t = dict_find(iter, slot_keys[i]);
    if (t) { s_settings.slots[i] = (uint8_t)t->value->int32; cfg_changed = true; }
  }

  // Color settings — temporarily ignored for crash isolation
  // TODO: re-enable once crash is diagnosed
  { Tuple *t = dict_find(iter, MESSAGE_KEY_COLOR_THEME);
    if (t) { (void)t; /* ignored */ } }
  { Tuple *t = dict_find(iter, MESSAGE_KEY_COLOR_BACKGROUND);
    if (t) { APP_LOG(APP_LOG_LEVEL_DEBUG, "CFG: color_bg type=%d", t->type); } }
  { Tuple *t = dict_find(iter, MESSAGE_KEY_COLOR_ACCENT);
    if (t) { APP_LOG(APP_LOG_LEVEL_DEBUG, "CFG: color_ac type=%d", t->type); } }
  { Tuple *t = dict_find(iter, MESSAGE_KEY_COLOR_VALUES);
    if (t) { (void)t; /* ignored */ } }
  { Tuple *t = dict_find(iter, MESSAGE_KEY_COLOR_HIGHLIGHTS);
    if (t) { (void)t; /* ignored */ } }

  if (cfg_changed) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "CFG: persist");
    persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
    APP_LOG(APP_LOG_LEVEL_DEBUG, "CFG: build_color theme=%d bg=%06lx", s_settings.color_theme, (unsigned long)s_settings.color_background);
    prv_build_color_scheme();
    APP_LOG(APP_LOG_LEVEL_DEBUG, "CFG: recolor header");
    text_layer_set_text_color(s_header_layer, s_colors.accent);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "CFG: recolor time");
    text_layer_set_text_color(s_time_layer, s_colors.values);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "CFG: recolor slots");
    for (int i = 0; i < NUM_SLOTS; i++) {
      if (s_field_label_layers[i]) text_layer_set_text_color(s_field_label_layers[i], s_colors.accent);
      if (s_field_value_layers[i]) text_layer_set_text_color(s_field_value_layers[i], s_colors.values);
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "CFG: mark dirty");
    layer_mark_dirty(s_decorations_layer);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "CFG: render slots");
    prv_render_all_slots();
    APP_LOG(APP_LOG_LEVEL_DEBUG, "CFG: done");
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

// ─── Window load/unload ───────────────────────────────────────────────────────
static void main_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);

  prv_build_color_scheme();

  // Initialize all slot pointers to NULL before layout
  for (int i = 0; i < MAX_SLOTS; i++) {
    s_field_label_layers[i] = NULL;
    s_field_value_layers[i] = NULL;
  }

  prv_create_layout(root);

  layer_set_update_proc(s_battery_layer, battery_update_proc);
  layer_add_child(root, s_battery_layer);
  layer_set_update_proc(s_decorations_layer, decorations_update_proc);
  layer_add_child(root, s_decorations_layer);

  text_layer_set_text(s_header_layer, "ARTEMIS II");

  // Special event overlay — covers the field area, hidden by default
  // Positioned to fill screen below the date row, centered
  GRect bounds = layer_get_bounds(root);
  int overlay_top, overlay_h;
#if defined(PBL_PLATFORM_EMERY)
  overlay_top = 94; overlay_h = 134;
#elif defined(PBL_PLATFORM_GABBRO)
  overlay_top = 100; overlay_h = 160;
#elif defined(PBL_PLATFORM_CHALK)
  overlay_top = 75; overlay_h = 82;
#else  // aplite / basalt
  overlay_top = 80; overlay_h = 88;
#endif
  (void)bounds;
  s_event_overlay_layer = text_layer_create(
    GRect(4, overlay_top, bounds.size.w - 8, overlay_h));
  text_layer_set_background_color(s_event_overlay_layer, GColorClear);
  text_layer_set_text_color(s_event_overlay_layer, s_colors.highlights);
  text_layer_set_font(s_event_overlay_layer,
    fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_event_overlay_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_event_overlay_layer, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(s_event_overlay_layer));
  layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), true);

  // Initial time
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  prv_update_time(t);

  prv_update_event_overlay();
  prv_render_all_slots();

  BatteryChargeState bs = battery_state_service_peek();
  s_battery_level = bs.charge_percent;
  layer_mark_dirty(s_battery_layer);
}

static void main_window_unload(Window *window) {
  layer_destroy(s_battery_layer);
  layer_destroy(s_decorations_layer);
  text_layer_destroy(s_header_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (s_field_label_layers[i]) text_layer_destroy(s_field_label_layers[i]);
    if (s_field_value_layers[i]) text_layer_destroy(s_field_value_layers[i]);
  }
  if (s_event_overlay_layer) text_layer_destroy(s_event_overlay_layer);
}

// ─── Init / deinit ────────────────────────────────────────────────────────────
static void init(void) {
  prv_load_settings();
  prv_load_artemis();

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load   = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_callback);

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_open(768, 64);

  if (!s_artemis.mission_complete) prv_request_artemis_data();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
