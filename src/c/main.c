#include <pebble.h>
#include "artemis_config.h"

static ArtemisSettings s_settings;
static ArtemisData     s_artemis;
static GFont          s_artemis_font_14 = NULL;
static GFont          s_artemis_font_18 = NULL;
static GFont          s_artemis_font_24 = NULL;
static GFont          s_artemis_font_36 = NULL;

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
static Layer            *s_logo_layer;
static GDrawCommandImage *s_logo_pdc;
static GSize             s_logo_draw_size;
static GPoint            s_logo_draw_offset;

static int s_battery_level = 100;

// Set true at startup and whenever color theme/colors change. Functions that
// depend on theme-dependent precomputation (logo PDC variant + scaled points,
// chrome recolor) should check this flag, do their work, then clear it.
static bool s_colors_dirty = true;

// ─── Active slot mapping ──────────────────────────────────────────────────────
// s_active_slots[i] = index into s_settings.slots[] for the i-th visible slot.
// Slots set to FIELD_NONE are skipped. s_num_active is the count.
static int s_active_slots[MAX_SLOTS];
static int s_num_active = 0;
static Layer *s_root_layer = NULL;  // retained for dynamic slot recreation

// ─── Buffers ──────────────────────────────────────────────────────────────────
static char s_time_buf[8];
static char s_date_buf[24];
static char s_met_buf[16];
static char s_slot_label_bufs[MAX_SLOTS][20];
static char s_slot_value_bufs[MAX_SLOTS][24];

// ─── Default settings ─────────────────────────────────────────────────────────
static void prv_default_settings(void) {
  s_settings.version              = SETTINGS_VERSION;
  s_settings.update_interval_min  = DEFAULT_UPDATE_INTERVAL;
  s_settings.use_miles            = DEFAULT_USE_MILES;
  s_settings.color_theme          = DEFAULT_COLOR_THEME;
  s_settings.color_background     = DEFAULT_COLOR_BACKGROUND;
  s_settings.color_accent         = DEFAULT_COLOR_ACCENT;
  s_settings.color_values         = DEFAULT_COLOR_VALUES;
  s_settings.color_highlights     = DEFAULT_COLOR_HIGHLIGHTS;
  s_settings.vibrate_events       = DEFAULT_VIBRATE_EVENTS;
  s_settings.slots[0] = DEFAULT_SLOT_0;
  s_settings.slots[1] = DEFAULT_SLOT_1;
  s_settings.slots[2] = DEFAULT_SLOT_2;
  s_settings.slots[3] = DEFAULT_SLOT_3;
  s_settings.slots[4] = DEFAULT_SLOT_4;
  s_settings.slots[5] = DEFAULT_SLOT_5;
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
  // Assume mission complete until phone confirms otherwise — this shows the
  // logo at startup instead of an empty/placeholder field view while waiting
  // for the first /api/all response.
  s_artemis.mission_complete = true;
  s_artemis.last_update_epoch = 0;
  s_artemis.g_force_x10000  = 0;
  s_artemis.altitude_km     = 0;
  s_artemis.periapsis_km    = 0;
  s_artemis.apoapsis_km     = 0;
  s_artemis.signal_x100   = 0;
  s_artemis.downlink_kbps = 0;
  for (int i = 0; i < MAX_UPCOMING; i++) {
    s_artemis.upcoming[i].name[0] = '\0';
    s_artemis.upcoming[i].epoch   = 0;
  }
}

static void prv_load_artemis(void) {
  prv_default_artemis();
  persist_read_data(ARTEMIS_KEY, &s_artemis, sizeof(s_artemis));
}

// ─── Color scheme ─────────────────────────────────────────────────────────────
// Color roles. The render code asks for a role and gets the resolved GColor
// based on the current theme — no separate cached state. Custom theme reads
// from the persisted s_settings.color_* fields; presets are constants.
typedef enum {
  COLOR_ROLE_BACKGROUND = 0,
  COLOR_ROLE_ACCENT,
  COLOR_ROLE_VALUES,
  COLOR_ROLE_HIGHLIGHTS,
  COLOR_ROLE_GRAPH_PENDING,
  COLOR_ROLE_STATUS_COMPLETE,
} ColorRole;

static GColor prv_color(ColorRole role) {
//  switch (role) {
//    case COLOR_ROLE_GRAPH_PENDING:   return GColorDarkGray;
//    case COLOR_ROLE_STATUS_COMPLETE: return GColorLightGray;
//    default: break;
//  }

  switch (s_settings.color_theme) {
    case COLOR_MODE_BW_DARK: // B&W Dark
      switch (role) {
        case COLOR_ROLE_BACKGROUND:      return GColorBlack;
#ifdef PBL_COLOR
        // Some gradients in color screens
        case COLOR_ROLE_GRAPH_PENDING:   return GColorDarkGray;
        case COLOR_ROLE_STATUS_COMPLETE: return GColorLightGray;
#endif
        default:                          return GColorWhite;
      }
    case COLOR_MODE_BW_CLEAR: // B&W Dark
#ifdef PBL_BW
    default:
#endif
      switch (role) { 
        case COLOR_ROLE_BACKGROUND:      return GColorWhite;
#ifdef PBL_COLOR
        // Some gradients in color screens
        case COLOR_ROLE_GRAPH_PENDING:   return GColorLightGray;
        case COLOR_ROLE_STATUS_COMPLETE: return GColorDarkGray;
#endif
        default:                          return GColorBlack;
      }
#ifdef PBL_COLOR
    // More modes for color screens
    case COLOR_MODE_SPACE: // Space (default)
      switch (role) {
        case COLOR_ROLE_BACKGROUND: return GColorBlack;
        case COLOR_ROLE_ACCENT:     return GColorCyan;
        case COLOR_ROLE_VALUES:     return GColorWhite;
        case COLOR_ROLE_HIGHLIGHTS: return GColorYellow;
        default: return GColorWhite;
      }
    case COLOR_MODE_DARK: // Dark
      switch (role) {
        case COLOR_ROLE_BACKGROUND: return GColorFromHEX(0x1A1A2E);
        case COLOR_ROLE_ACCENT:     return GColorFromHEX(0x4A90D9);
        case COLOR_ROLE_VALUES:     return GColorLightGray;
        case COLOR_ROLE_HIGHLIGHTS: return GColorCyan;
        default: return GColorWhite;
      }
    case COLOR_MODE_CLEAR: // Clear
      switch (role) {
        case COLOR_ROLE_BACKGROUND: return GColorWhite;
        case COLOR_ROLE_ACCENT:     return GColorFromHEX(0x003399);
        case COLOR_ROLE_VALUES:     return GColorBlack;
        case COLOR_ROLE_HIGHLIGHTS: return GColorOrange;
        default: return GColorBlack;
      }
    case COLOR_MODE_NASA: // NASA
      switch (role) {
        case COLOR_ROLE_BACKGROUND: return GColorFromHEX(0x0B1F3A);
        case COLOR_ROLE_ACCENT:     return GColorFromHEX(0xFFB300);
        case COLOR_ROLE_VALUES:     return GColorWhite;
        case COLOR_ROLE_HIGHLIGHTS: return GColorOrange;
        default: return GColorWhite;
      }
    case COLOR_MODE_CUSTOM: // Custom
    default:
      switch (role) {
        case COLOR_ROLE_BACKGROUND: return GColorFromHEX(s_settings.color_background);
        case COLOR_ROLE_ACCENT:     return GColorFromHEX(s_settings.color_accent);
        case COLOR_ROLE_VALUES:     return GColorFromHEX(s_settings.color_values);
        case COLOR_ROLE_HIGHLIGHTS: return GColorFromHEX(s_settings.color_highlights);
        default: return GColorWhite;
      }
#endif
  }
}

// Returns the raw 0xRRGGBB hex of the resolved background color, used by
// prv_setup_logo to pick the dark/clear variant via perceived_luminance.
static uint32_t prv_color_hex(ColorRole role) {
#ifdef PBL_COLOR
  GColor c = prv_color(role);
  // Reverse GColorFromHEX: extract 8-8-8 RGB from the 8-bit ARGB color.
  uint8_t r = ((c.argb >> 4) & 0x3) * 85;
  uint8_t g = ((c.argb >> 2) & 0x3) * 85;
  uint8_t b = ( c.argb       & 0x3) * 85;
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
#else
  return (role == COLOR_ROLE_BACKGROUND) ? 0x000000 : 0xFFFFFF;
#endif
}

static void prv_apply_window_background(void) {
  window_set_background_color(s_main_window, prv_color(COLOR_ROLE_BACKGROUND));
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

// ─── Special event lookup ─────────────────────────────────────────────────────
// Source 1: hardcoded table of key lunar flyby moments (highest priority)
// Source 2: upcoming milestones stored from the API (works offline)
// Hardcoded events use custom display_minutes (0 = default); upcoming use default
static const char *prv_get_special_event(void) {
  uint32_t now = (uint32_t)time(NULL);

  for (int i = 0; i < NUM_SPECIAL_EVENTS; i++) {
    uint32_t start = SPECIAL_EVENTS[i].epoch;
    uint32_t display_secs = (SPECIAL_EVENTS[i].display_minutes == 0)
      ? EVENT_DISPLAY_S
      : (SPECIAL_EVENTS[i].display_minutes * 60);
    if (now >= start && now < start + display_secs) {
      return SPECIAL_EVENTS[i].message;
    }
  }

  for (int i = 0; i < MAX_UPCOMING; i++) {
    uint32_t start = s_artemis.upcoming[i].epoch;
    if (start > 0 && s_artemis.upcoming[i].name[0] != '\0'
        && now >= start && now < start + EVENT_DISPLAY_S) {
      return s_artemis.upcoming[i].name;
    }
  }

  return NULL;
}

// ─── Slot rendering ───────────────────────────────────────────────────────────

// Renders one slot by its settings-array index (si).
// All FIELD_NONE slots are never created, so si is always valid here.
static void prv_render_slot(int si) {
  FieldType ft = (FieldType)s_settings.slots[si];
  TextLayer *lbl = s_field_label_layers[si];
  TextLayer *val = s_field_value_layers[si];
  if (!lbl || !val) return;
  if ((uint8_t)s_settings.slots[si] >= (uint8_t)FIELD_COUNT) ft = FIELD_NONE;

  if (ft == FIELD_NONE) {
    text_layer_set_text(lbl, "");
    text_layer_set_text(val, "");
    return;
  }

  strncpy(s_slot_label_bufs[si], FIELD_LABELS[ft], sizeof(s_slot_label_bufs[si]) - 1);
  text_layer_set_text(lbl, s_slot_label_bufs[si]);

  char *vbuf = s_slot_value_bufs[si];
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
      strncpy(s_slot_label_bufs[si], s_artemis.milestone_name, sizeof(s_slot_label_bufs[si]) - 1);
      text_layer_set_text(lbl, s_slot_label_bufs[si]);
      prv_format_milestone_eta(vbuf, 24);
      // Highlight color for ETA
      text_layer_set_text_color(val, prv_color(COLOR_ROLE_HIGHLIGHTS));
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

  text_layer_set_text_color(val, prv_color(COLOR_ROLE_VALUES));
  text_layer_set_text(val, vbuf);
}

static void prv_render_all_slots(void) {
  for (int i = 0; i < s_num_active; i++)
      prv_render_slot(s_active_slots[i]);
}

// Show or hide the special event overlay over the field area
static const char *s_active_event_msg = NULL;  // tracks currently shown event

// ─── Unified display logic ────────────────────────────────────────────────────
// Display states (for tracking, to avoid redundant layer hide/show calls)
typedef enum {
  DISPLAY_STATE_UNKNOWN = 0,
  DISPLAY_STATE_EVENT,
  DISPLAY_STATE_LOGO,
  DISPLAY_STATE_FIELDS
} DisplayState;

static DisplayState s_current_display_state = DISPLAY_STATE_UNKNOWN;

// Priority: Special Events > Mission Complete > Normal Fields
static void prv_update_display_ui(void) {
  if (!s_event_overlay_layer || !s_logo_layer) return;

  const char *event_msg = prv_get_special_event();
  bool event_active = (event_msg != NULL);

  // Determine target state
  DisplayState target_state = event_active ? DISPLAY_STATE_EVENT
                            : s_artemis.mission_complete ? DISPLAY_STATE_LOGO
                            : DISPLAY_STATE_FIELDS;

  // Vibrate only on transition to a new event (independent of redraw skip)
  if (event_msg != s_active_event_msg && event_msg != NULL && s_settings.vibrate_events) {
    static const uint32_t segments[] = { 200, 100, 200, 100, 400 };
    VibePattern pat = { .durations = segments, .num_segments = 5 };
    vibes_enqueue_custom_pattern(pat);
  }
  s_active_event_msg = event_msg;

  // Always update the event text if we're showing the event (text can change
  // even if state stays the same), and skip layer hide/show if state unchanged.
  if (target_state == s_current_display_state) {
    if (target_state == DISPLAY_STATE_EVENT && event_msg) {
      text_layer_set_text(s_event_overlay_layer, event_msg);
    }
    return;
  }

  if (DEBUG_ENABLED) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "=== DISPLAY UI UPDATE === state %d → %d",
            s_current_display_state, target_state);
  }

  s_current_display_state = target_state;

  switch (target_state) {
    case DISPLAY_STATE_EVENT:
      if (DEBUG_ENABLED) APP_LOG(APP_LOG_LEVEL_DEBUG, "→ Showing EVENT OVERLAY");
      layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), false);
      layer_set_hidden(s_logo_layer, true);
      layer_set_hidden(s_decorations_layer, true);
      for (int i = 0; i < s_num_active; i++) {
        int si = s_active_slots[i];
        if (s_field_label_layers[si])
          layer_set_hidden(text_layer_get_layer(s_field_label_layers[si]), true);
        if (s_field_value_layers[si])
          layer_set_hidden(text_layer_get_layer(s_field_value_layers[si]), true);
      }
      text_layer_set_text(s_event_overlay_layer, event_msg);
      break;

    case DISPLAY_STATE_LOGO:
      if (DEBUG_ENABLED) APP_LOG(APP_LOG_LEVEL_DEBUG, "→ Showing MISSION COMPLETE LOGO");
      layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), true);
      layer_set_hidden(s_logo_layer, false);
      layer_set_hidden(s_decorations_layer, true);
      for (int i = 0; i < s_num_active; i++) {
        int si = s_active_slots[i];
        if (s_field_label_layers[si])
          layer_set_hidden(text_layer_get_layer(s_field_label_layers[si]), true);
        if (s_field_value_layers[si])
          layer_set_hidden(text_layer_get_layer(s_field_value_layers[si]), true);
      }
      break;

    case DISPLAY_STATE_FIELDS:
    default:
      if (DEBUG_ENABLED) APP_LOG(APP_LOG_LEVEL_DEBUG, "→ Showing NORMAL FIELDS");
      layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), true);
      layer_set_hidden(s_logo_layer, true);
      layer_set_hidden(s_decorations_layer, false);
      for (int i = 0; i < s_num_active; i++) {
        int si = s_active_slots[i];
        if (s_field_label_layers[si])
          layer_set_hidden(text_layer_get_layer(s_field_label_layers[si]), false);
        if (s_field_value_layers[si])
          layer_set_hidden(text_layer_get_layer(s_field_value_layers[si]), false);
      }
      break;
  }
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
  prv_update_display_ui();
  prv_render_all_slots();

  if (!s_artemis.mission_complete) {
    int iv = (int)s_settings.update_interval_min;
    if (iv < 1) iv = 30;
    if (tick_time->tm_min % iv == 0) prv_request_artemis_data();
  }
}

// ─── Logo (PDC vector) ────────────────────────────────────────────────────────
static void prv_scale_pdc_image(GDrawCommandImage *img, GSize target);

static void logo_update_proc(Layer *layer, GContext *ctx) {
  if (!s_logo_pdc) return;
  gdraw_command_image_draw(ctx, s_logo_pdc, s_logo_draw_offset);
}

// Computes overlay top/height for the current platform (must match the
// values used by prv_create_slots and the event overlay layer).
static void prv_overlay_geometry(int w, int h, int *out_top, int *out_h) {
#ifdef PBL_ROUND
  int time_h_ov = (w >= 240) ? 52 : 36;
  int top = 22 + time_h_ov + 2 + 18 + 4;
#else
  int time_h_ov = (w >= 180) ? 52 : 42;
  int date_h_ov = (w >= 180) ? 18 : 14;
  int top = 19 + time_h_ov + 2 + date_h_ov + 2;
#endif
  *out_top = top;
  *out_h   = h - top - 4;
}

static uint8_t perceived_luminance_gcolor(GColor color) {
    uint8_t r = ((color.argb >> 4) & 0x3) * 85;
    uint8_t g = ((color.argb >> 2) & 0x3) * 85;
    uint8_t b = ((color.argb >> 0) & 0x3) * 85;

    APP_LOG(APP_LOG_LEVEL_ERROR, "GColor: %d - color.argb: %d - Luminance: %d", color, color.argb, 77 * r + 150 * g + 29 * b);

    return (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
}


static uint8_t perceived_luminance(uint32_t color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >>  8) & 0xFF;
    uint8_t b = (color >>  0) & 0xFF;

    APP_LOG(APP_LOG_LEVEL_ERROR, "Color: %d - Luminance: %d", color, 77 * r + 150 * g + 29 * b);

    // Coefficients scaled to sum to 256 for integer-only arithmetic
    // 0.299 ≈ 77/256,  0.587 ≈ 150/256,  0.114 ≈ 29/256
    return (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
}

// Builds (or rebuilds) the logo layer + scaled PDC for the current color
// theme. Creates the layer the first time it is called; subsequent calls
// release the old PDC, load the correct variant, rescale, and reposition
// the existing layer.
static void prv_setup_logo(void) {
  if (!s_root_layer) return;

  GRect bounds = layer_get_bounds(s_root_layer);
  int w = bounds.size.w, h = bounds.size.h;
  int overlay_top, overlay_h;
  prv_overlay_geometry(w, h, &overlay_top, &overlay_h);

  // Release any previously loaded PDC — we re-load to start from unscaled
  // points (scaling is destructive in-place).
  if (s_logo_pdc) {
    gdraw_command_image_destroy(s_logo_pdc);
    s_logo_pdc = NULL;
  }

APP_LOG(APP_LOG_LEVEL_ERROR, "1");

#ifdef PBL_COLOR
  uint32_t logo_resource = (perceived_luminance_gcolor(prv_color(COLOR_ROLE_BACKGROUND)) < 128)
                          ? RESOURCE_ID_IMAGE_ARTEMIS_LOGO_DARK
                          : RESOURCE_ID_IMAGE_ARTEMIS_LOGO_CLEAR;
APP_LOG(APP_LOG_LEVEL_ERROR, "2c");
#else
  uint32_t logo_resource = (perceived_luminance_gcolor(prv_color(COLOR_ROLE_BACKGROUND)) < 128)
                          ? RESOURCE_ID_IMAGE_ARTEMIS_LOGO_WHITE
                          : RESOURCE_ID_IMAGE_ARTEMIS_LOGO_BLACK;
APP_LOG(APP_LOG_LEVEL_ERROR, "2bn");

#endif
  s_logo_pdc = gdraw_command_image_create_with_resource(logo_resource);
APP_LOG(APP_LOG_LEVEL_ERROR, "3");

  int target_size = overlay_h - 4;
  if (target_size > w - 8) target_size = w - 8;
  if (target_size < 1) target_size = 1;

  if (s_logo_pdc) {
    GSize orig = gdraw_command_image_get_bounds_size(s_logo_pdc);
    if (orig.w > 0 && orig.h > 0) {
      int tw = target_size, th = target_size;
      if (orig.w >= orig.h) {
        th = (target_size * orig.h) / orig.w;
      } else {
        tw = (target_size * orig.w) / orig.h;
      }
      s_logo_draw_size = GSize(tw, th);
    } else {
      s_logo_draw_size = GSize(target_size, target_size);
    }
    prv_scale_pdc_image(s_logo_pdc, s_logo_draw_size);
  } else {
    s_logo_draw_size = GSize(target_size, target_size);
  }

  s_logo_draw_offset = GPoint(0, 0);

  int logo_x = (w - s_logo_draw_size.w) / 2;
  int logo_y = overlay_top + (overlay_h - s_logo_draw_size.h) / 2;
  GRect frame = GRect(logo_x, logo_y, s_logo_draw_size.w, s_logo_draw_size.h);

  if (!s_logo_layer) {
    s_logo_layer = layer_create(frame);
    layer_set_update_proc(s_logo_layer, logo_update_proc);
    layer_add_child(s_root_layer, s_logo_layer);
    layer_set_hidden(s_logo_layer, true);
  } else {
    layer_set_frame(s_logo_layer, frame);
    layer_mark_dirty(s_logo_layer);
  }
}

// Scale all points and radii in a PDC image in-place to fit target size.
// The PDC's stored bounds_size may not reflect the actual coordinate range of
// its points (some converters output points in SVG units while bounds is something
// else). Compute the actual point bounding box first, then scale + translate so
// the bbox maps into target.w × target.h, preserving aspect ratio and centering.
static void prv_scale_pdc_image(GDrawCommandImage *img, GSize target) {
  if (!img) return;

  GDrawCommandList *list = gdraw_command_image_get_command_list(img);
  if (!list) return;

  uint32_t num = gdraw_command_list_get_num_commands(list);

  int32_t min_x = INT32_MAX, min_y = INT32_MAX;
  int32_t max_x = INT32_MIN, max_y = INT32_MIN;
  bool any_points = false;

  for (uint32_t i = 0; i < num; i++) {
    GDrawCommand *cmd = gdraw_command_list_get_command(list, i);
    if (!cmd) continue;
    uint16_t np = gdraw_command_get_num_points(cmd);
    for (uint16_t j = 0; j < np; j++) {
      GPoint p = gdraw_command_get_point(cmd, j);
      if (p.x < min_x) min_x = p.x;
      if (p.y < min_y) min_y = p.y;
      if (p.x > max_x) max_x = p.x;
      if (p.y > max_y) max_y = p.y;
      any_points = true;
    }
  }

  if (!any_points) return;

  int32_t src_w = max_x - min_x;
  int32_t src_h = max_y - min_y;
  if (src_w <= 0) src_w = 1;
  if (src_h <= 0) src_h = 1;

  int32_t sx_x1000 = (int32_t)target.w * 1000 / src_w;
  int32_t sy_x1000 = (int32_t)target.h * 1000 / src_h;
  int32_t s_x1000 = (sx_x1000 < sy_x1000) ? sx_x1000 : sy_x1000;

  int32_t scaled_w = src_w * s_x1000 / 1000;
  int32_t scaled_h = src_h * s_x1000 / 1000;
  int32_t off_x = (target.w - scaled_w) / 2;
  int32_t off_y = (target.h - scaled_h) / 2;

  for (uint32_t i = 0; i < num; i++) {
    GDrawCommand *cmd = gdraw_command_list_get_command(list, i);
    if (!cmd) continue;
    uint16_t np = gdraw_command_get_num_points(cmd);
    for (uint16_t j = 0; j < np; j++) {
      GPoint p = gdraw_command_get_point(cmd, j);
      int32_t nx = ((int32_t)p.x - min_x) * s_x1000 / 1000 + off_x;
      int32_t ny = ((int32_t)p.y - min_y) * s_x1000 / 1000 + off_y;
      p.x = (int16_t)nx;
      p.y = (int16_t)ny;
      gdraw_command_set_point(cmd, j, p);
    }
    uint16_t r = gdraw_command_get_radius(cmd);
    if (r > 0) {
      gdraw_command_set_radius(cmd, (uint16_t)((uint32_t)r * s_x1000 / 1000));
    }
  }

  gdraw_command_image_set_bounds_size(img, target);
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
                     (s_battery_level <= 40) ? GColorChromeYellow : prv_color(COLOR_ROLE_HIGHLIGHTS);
  graphics_context_set_fill_color(ctx, arc_color);
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
#endif
  graphics_fill_radial(ctx, bounds, GOvalScaleModeFitCircle, inset, a_start, a_end);
  graphics_context_set_fill_color(ctx, prv_color(COLOR_ROLE_GRAPH_PENDING));
  graphics_fill_radial(ctx, bounds, GOvalScaleModeFitCircle, inset, a_end, DEG_TO_TRIGANGLE(270));
#else
  int bar_w = (bounds.size.w * s_battery_level) / 100;
  graphics_context_set_fill_color(ctx, prv_color(COLOR_ROLE_GRAPH_PENDING));
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#ifdef PBL_COLOR
  GColor bar_color = (s_battery_level <= 20) ? GColorRed :
                     (s_battery_level <= 40) ? GColorChromeYellow : prv_color(COLOR_ROLE_HIGHLIGHTS);
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
  graphics_context_set_stroke_color(ctx, prv_color(COLOR_ROLE_ACCENT));
  graphics_context_set_stroke_width(ctx, 1);

#ifdef PBL_ROUND
  int r = layer_get_bounds(layer).size.w / 2;
  // Separators: after date, between sections, before milestone
#ifdef PBL_PLATFORM_CHALK
  int first_line_y = 80;
#else // gabbro
  int first_line_y = 100;
#endif
  int num_lines = 1 + (int) (s_num_active / 2);
  int separation_between_lines = (int) (0.9 * (r*2 - first_line_y)) / (num_lines);
  for (int i = 0; i < num_lines; i++) {
    prv_draw_horizontal_line(ctx, first_line_y + separation_between_lines * i, r);  // after date
    APP_LOG(APP_LOG_LEVEL_DEBUG, "HORIZONTAL LINE: %d", (int)first_line_y + separation_between_lines * i);
  }
  int num_singles = (s_num_active + 1) % 2 + 1;
  int last_line_y = first_line_y + separation_between_lines * (num_lines - num_singles);
  // Column dividers for pairs
  graphics_draw_line(ctx, GPoint(r, first_line_y), GPoint(r, last_line_y));
  APP_LOG(APP_LOG_LEVEL_DEBUG, "VERTICAL LINE: %d - %d", first_line_y, last_line_y);

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
// ─── Layer helpers ────────────────────────────────────────────────────────────
static TextLayer *prv_make_layer(Layer *root, GRect r, GColor col,
                                 GFont font, GTextAlignment align) {
  TextLayer *tl = text_layer_create(r);
  text_layer_set_background_color(tl, GColorClear);
  text_layer_set_text_color(tl, col);
  text_layer_set_font(tl, font);
  text_layer_set_text_alignment(tl, align);
  layer_add_child(root, text_layer_get_layer(tl));
  return tl;
}

// ─── Font selection ───────────────────────────────────────────────────────────
// Selects font based on available height and font family preference
// Parameters:
//   height: available vertical space in pixels
//   bold: currently unused, reserved for future use
//   use_artemis: true for custom Artemis font, false for system font
static GFont prv_select_font(int height, bool bold, bool use_artemis) {
  if (use_artemis) {
    if (height >= 44) return s_artemis_font_36;
    if (height >= 36) return s_artemis_font_24;
    if (height >= 28) return s_artemis_font_18;
    return s_artemis_font_14;
  } else {
    // System fonts
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
}

static void prv_build_active_slots(void) {
  s_num_active = 0;
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (s_settings.slots[i] != FIELD_NONE) {
      s_active_slots[s_num_active++] = i;
    }
  }
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

// ─── Chrome: battery, header, time, date, decorations ────────────────────────
// Called once at window load. Uses screen dimensions dynamically.
static void prv_create_chrome(Layer *root) {
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w, h = bounds.size.h;

#ifdef PBL_ROUND
  // Round: battery arc fills full circle layer
  s_battery_layer = layer_create(GRect(0, 0, w, h));
  int cx = w / 2;
  s_header_layer = prv_make_layer(root, GRect(cx - 60, 6, 120, 14),
                    prv_color(COLOR_ROLE_ACCENT), prv_select_font(14, false, true), GTextAlignmentCenter);
  int time_h = (w >= 240) ? 52 : 36;
  s_time_layer = prv_make_layer(root, GRect(cx - (w/2 - 28), 22, w - 56, time_h),
                  prv_color(COLOR_ROLE_VALUES), prv_select_font(time_h, true, true), GTextAlignmentCenter);
  int date_h = (w >= 240) ? 26 : 18;
  int date_y = 22 + time_h + 2;
  s_date_layer = prv_make_layer(root, GRect(cx - 70, date_y, 140, 18),
                  GColorLightGray, prv_select_font(date_h, false, true), GTextAlignmentCenter);
#else
  // Rect: thin battery bar across top
  s_battery_layer = layer_create(GRect(0, 0, w, 3));
  s_header_layer  = prv_make_layer(root, GRect(0, 4, w, 14),
                     prv_color(COLOR_ROLE_ACCENT), prv_select_font(14, false, true), GTextAlignmentCenter);
  int time_h = (w >= 180) ? 52 : 42;
  GFont time_font = prv_select_font(time_h, true, true);
  s_time_layer = prv_make_layer(root, GRect(0, 19, w, time_h),
                  prv_color(COLOR_ROLE_VALUES), time_font, GTextAlignmentCenter);
  int date_h = (w >= 180) ? 18 : 14;
  GFont date_font = prv_select_font(date_h, false, true);
  s_date_layer = prv_make_layer(root, GRect(0, 19 + time_h + 2, w, date_h),
                  GColorLightGray, date_font, GTextAlignmentCenter);
#endif

  s_decorations_layer = layer_create(GRect(0, 0, w, h));
}

// ─── Slot creation ────────────────────────────────────────────────────
// Rebuilds slot layers from scratch based on current s_settings.slots[].
// Safe to call multiple times (destroys existing layers first).
#ifdef PBL_ROUND
// ─── Slot layout: round screens ──────────────────────────────────────────────
// Active slots fill pairs (2 columns) then singles (1 column, safe-zone width).
// chrome_bottom: first Y pixel available for data rows.
static void prv_create_slots (void) {
  if (!s_root_layer) return;
  prv_destroy_slots();
  prv_build_active_slots();
  // Slots have been rebuilt — force display state refresh next update
  s_current_display_state = DISPLAY_STATE_UNKNOWN;

  if (s_num_active == 0) return;

  GRect bounds = layer_get_bounds(s_root_layer);
  int w = bounds.size.w, h = bounds.size.h;
  int r = w / 2;

  // Chrome bottom: date_y + date_h + 2
  // date_y = 22 + time_h + 2; time_h = (w>=240)?52:36
  int time_h = (w >= 240) ? 52 : 36;
  int date_h = 18;
  //int chrome_bottom = 22 + time_h + 2 + date_h + 4;
#ifdef PBL_PLATFORM_CHALK
  int chrome_bottom = 80;
#else // gabbro
  int chrome_bottom = 100;
#endif

  APP_LOG(APP_LOG_LEVEL_INFO, "chrome_bottom %d", chrome_bottom);

  // Split active slots into pairs and singles:
  // Fill pairs first (2 per row), then remainder goes to singles.
  // DISTRIBUTION:
  //   TOTAL || SINGLE ROWS    || PAIR ROWS
  //    0    ||       0        ||     0
  //    1    ||       1        ||     0
  //    2    ||       2        ||     0
  //    3    ||       1        ||     1
  //    4    ||       2        ||     1
  //    5    ||       1        ||     2
  //    6    ||       2        ||     2
  int num_lines = 1 + (int) (s_num_active / 2);
  int num_singles = (s_num_active + 1) % 2 + 1;
  int num_pairs   = (s_num_active - num_singles) / 2;

  // Row heights: label(13) + value(13) = 26px per row
  int separation_between_lines = (int) (0.9 * (w - chrome_bottom)) / (num_lines);
  int label_h = (separation_between_lines / 2) - 2;
  int val_h = label_h;

  //for (int i = 0; i < num_lines; i++) {
  //  first_line_y + separation_between_lines * i, r;  // after date

  // Dynamic value offset for vertical centering: overlap by half the gap
  int val_y_offset = label_h + + 2;

  int col_l_x = 10, col_w = (w / 2) - 14;
  int col_r_x = w / 2 + 4;

  int y = chrome_bottom + 1;
  int ai = 0;  // index into s_active_slots[]

  GFont label_font = prv_select_font(label_h, true, true);
  GFont value_font = prv_select_font(val_h, false, false);

  for (int p = 0; p < num_pairs; p++) {
    for (int side = 0; side < 2; side++) {
      int si = s_active_slots[ai++];
      int cx = (side == 0) ? col_l_x : col_r_x;
      s_field_label_layers[si] = prv_make_layer(s_root_layer, GRect(cx, y, col_w, label_h),
        prv_color(COLOR_ROLE_ACCENT), label_font, GTextAlignmentCenter);
      s_field_value_layers[si] = prv_make_layer(s_root_layer, GRect(cx, y + val_y_offset, col_w, val_h),
        prv_color(COLOR_ROLE_VALUES), value_font, GTextAlignmentCenter);
      text_layer_set_overflow_mode(s_field_value_layers[si], GTextOverflowModeTrailingEllipsis);
      APP_LOG(APP_LOG_LEVEL_DEBUG, "BOX %d : %d x %d ++ %d x %d", si, cx, y, cx, y + val_y_offset);
    }
    y += separation_between_lines;
  }

  for (int s = 0; s < num_singles; s++) {
    int si = s_active_slots[ai++];
    // Safe chord width at this Y position
    int dy = y + label_h - r;
    int32_t hw = prv_isqrt(r * r - dy * dy) - 12;
    if (hw < 20) hw = 20;
    int sx = r - hw, sw = hw * 2;
    s_field_label_layers[si] = prv_make_layer(s_root_layer, GRect(sx, y, sw, label_h),
      prv_color(COLOR_ROLE_ACCENT), label_font, GTextAlignmentCenter);
    s_field_value_layers[si] = prv_make_layer(s_root_layer, GRect(sx, y + val_y_offset, sw, val_h),
      prv_color(COLOR_ROLE_VALUES), value_font, GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_field_value_layers[si], GTextOverflowModeTrailingEllipsis);
    y += separation_between_lines;
  }

  (void)h;
}
#else
// ─── Slot layout: rect screens ────────────────────────────────────────────────
// Rows fill the area below the chrome. Font size scales with active slot count.
// chrome_bottom: first Y pixel available for data rows.
static void prv_create_slots(void) {
  if (!s_root_layer) return;
  prv_destroy_slots();
  prv_build_active_slots();
  // Slots have been rebuilt — force display state refresh next update
  s_current_display_state = DISPLAY_STATE_UNKNOWN;

  if (s_num_active == 0) return;

  GRect bounds = layer_get_bounds(s_root_layer);
  int w = bounds.size.w, h = bounds.size.h;

  // Chrome bottom: 19 + time_h + 2 + date_h + 2
  int time_h = (w >= 180) ? 52 : 42;
  int date_h = (w >= 180) ? 18 : 14;
  int chrome_bottom = 19 + time_h + 2 + date_h + 2;

  int avail = h - chrome_bottom;
  int rh = avail / s_num_active;

  // Label column: fixed 40% of width, value gets the rest
  int lw = w * 40 / 100;
  int vx = lw + 4, vw = w - vx - 4;

  GFont label_font = prv_select_font(rh, true, true);
  GFont value_font = prv_select_font(rh, false, false);

  for (int i = 0; i < s_num_active; i++) {
    int si = s_active_slots[i];  // index into s_settings.slots[]
    int y = chrome_bottom + i * rh;
    s_field_label_layers[si] = prv_make_layer(s_root_layer, GRect(4, y, lw - 4, rh),
      prv_color(COLOR_ROLE_ACCENT), label_font, GTextAlignmentLeft);
    s_field_value_layers[si] = prv_make_layer(s_root_layer, GRect(vx, y, vw, rh),
      prv_color(COLOR_ROLE_VALUES), value_font, GTextAlignmentRight);
    text_layer_set_overflow_mode(s_field_value_layers[si], GTextOverflowModeTrailingEllipsis);
  }
}
#endif

// ─── Full layout creation (chrome + slots) ────────────────────────────────────
static void prv_create_layout(Layer *root) {
  s_root_layer = root;
  prv_create_chrome(root);
  prv_create_slots();
}

// ─── AppMessage helpers ───────────────────────────────────────────────────────
// Clay sends select values as cstrings (e.g. "1", "30"). Telemetry arrives as
// integers. These helpers handle both so call sites stay clean.

static int32_t prv_fetch_int(Tuple *t) {
  if (!t) return 0;

  if (t->type == TUPLE_CSTRING) 
    return atoi(t->value->cstring);
  // if (t->type == TUPLE_INT)
  if (t->length == 1)
    return (uint8_t) t->value->uint8;
  if (t->length == 2)
    return (uint16_t) t->value->uint16;
  if (t->length == 4)
    return (uint32_t) t->value->int32;
  return 0;
}

static uint32_t prv_fetch_color(Tuple *t) {
  if (!t) return 0;
  // Color pickers send "0xRRGGBB" — strip the "0x" prefix before parsing
  if (t->type == TUPLE_CSTRING)
    return (uint32_t)strtol(t->value->cstring, NULL, 16);
  return (uint32_t)t->value->int32;
}

// ─── AppMessage ───────────────────────────────────────────────────────────────
static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  bool data_changed = false, cfg_changed = false;
  Tuple *t;

  // ── Telemetry data ──────────────────────────────────────────────────────────
#define FETCH_STR(key, dst) \
  if ((t = dict_find(iter, MESSAGE_KEY_##key))) { \
    strncpy(dst, t->value->cstring, sizeof(dst) - 1); \
    dst[sizeof(dst) - 1] = '\0'; data_changed = true; }
#define FETCH_I32(key, dst) \
  if ((t = dict_find(iter, MESSAGE_KEY_##key))) { dst = prv_fetch_int(t); data_changed = true; }

  FETCH_STR(ARTEMIS_PHASE,          s_artemis.phase)
  FETCH_I32(ARTEMIS_SPEED,          s_artemis.speed_x100)
  FETCH_I32(ARTEMIS_DISTANCE,       s_artemis.distance_km)
  FETCH_I32(ARTEMIS_MOON_DIST,      s_artemis.moon_distance_km)
  FETCH_STR(ARTEMIS_MILESTONE_NAME, s_artemis.milestone_name)
  FETCH_I32(ARTEMIS_MILESTONE_MET,  s_artemis.milestone_met_ms)
  FETCH_I32(ARTEMIS_G_FORCE,        s_artemis.g_force_x10000)
  FETCH_I32(ARTEMIS_ALTITUDE,       s_artemis.altitude_km)
  FETCH_I32(ARTEMIS_PERIAPSIS,      s_artemis.periapsis_km)
  FETCH_I32(ARTEMIS_APOAPSIS,       s_artemis.apoapsis_km)
  FETCH_I32(ARTEMIS_SIGNAL,         s_artemis.signal_x100)
  FETCH_STR(ARTEMIS_STATION,  s_artemis.dsn_station)
  FETCH_I32(ARTEMIS_DOWNLINK, s_artemis.downlink_kbps)

  // Upcoming milestones (5 slots, each a name + UTC epoch)
  // Note: MESSAGE_KEY_* are extern variables, not constants — cannot use in static initializers
  { uint32_t name_keys[MAX_UPCOMING];
    name_keys[0] = MESSAGE_KEY_ARTEMIS_MS0_NAME;
    name_keys[1] = MESSAGE_KEY_ARTEMIS_MS1_NAME;
    name_keys[2] = MESSAGE_KEY_ARTEMIS_MS2_NAME;
    name_keys[3] = MESSAGE_KEY_ARTEMIS_MS3_NAME;
    name_keys[4] = MESSAGE_KEY_ARTEMIS_MS4_NAME;
    uint32_t epoch_keys[MAX_UPCOMING];
    epoch_keys[0] = MESSAGE_KEY_ARTEMIS_MS0_EPOCH;
    epoch_keys[1] = MESSAGE_KEY_ARTEMIS_MS1_EPOCH;
    epoch_keys[2] = MESSAGE_KEY_ARTEMIS_MS2_EPOCH;
    epoch_keys[3] = MESSAGE_KEY_ARTEMIS_MS3_EPOCH;
    epoch_keys[4] = MESSAGE_KEY_ARTEMIS_MS4_EPOCH;
    for (int i = 0; i < MAX_UPCOMING; i++) {
      if ((t = dict_find(iter, name_keys[i]))) {
        strncpy(s_artemis.upcoming[i].name, t->value->cstring,
                sizeof(s_artemis.upcoming[i].name) - 1);
        s_artemis.upcoming[i].name[sizeof(s_artemis.upcoming[i].name) - 1] = '\0';
        data_changed = true;
      }
      if ((t = dict_find(iter, epoch_keys[i]))) {
        s_artemis.upcoming[i].epoch = (uint32_t)t->value->int32;
        data_changed = true;
      }
    }
  }

  if ((t = dict_find(iter, MESSAGE_KEY_ARTEMIS_COMPLETE))) {
    s_artemis.mission_complete = (t->value->uint8 == 1); data_changed = true;
    prv_update_display_ui();
  }

#undef FETCH_STR
#undef FETCH_I32

  if (data_changed) {
    s_artemis.last_update_epoch = (uint32_t)time(NULL);
    persist_write_data(ARTEMIS_KEY, &s_artemis, sizeof(s_artemis));
    prv_render_all_slots();
  }

  // ── Clay settings ───────────────────────────────────────────────────────────
  if ((t = dict_find(iter, MESSAGE_KEY_UPDATE_INTERVAL))) {
    s_settings.update_interval_min = prv_fetch_int(t); cfg_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_USE_MILES))) {
    s_settings.use_miles = (t->value->uint8 != 0); cfg_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_VIBRATE_EVENTS))) {
    s_settings.vibrate_events = (t->value->uint8 != 0); cfg_changed = true;
  }

  // Slot selects
  uint32_t slot_keys[MAX_SLOTS] = {
    MESSAGE_KEY_SLOT_1, MESSAGE_KEY_SLOT_2, MESSAGE_KEY_SLOT_3,
    MESSAGE_KEY_SLOT_4, MESSAGE_KEY_SLOT_5, MESSAGE_KEY_SLOT_6
  };
  for (int i = 0; i < MAX_SLOTS; i++) {
    if ((t = dict_find(iter, slot_keys[i]))) {
      s_settings.slots[i] = (uint8_t)prv_fetch_int(t); cfg_changed = true;
    }
  }

  // Color settings — flag any change so theme-dependent precomputation
  // (logo variant, chrome recolor) reruns.
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_THEME))) {
    s_settings.color_theme = (uint8_t)prv_fetch_int(t); cfg_changed = true; s_colors_dirty = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_BACKGROUND))) {
    s_settings.color_background = prv_fetch_color(t); cfg_changed = true; s_colors_dirty = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_ACCENT))) {
    s_settings.color_accent = prv_fetch_color(t); cfg_changed = true; s_colors_dirty = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_VALUES))) {
    s_settings.color_values = prv_fetch_color(t); cfg_changed = true; s_colors_dirty = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_HIGHLIGHTS))) {
    s_settings.color_highlights = prv_fetch_color(t); cfg_changed = true; s_colors_dirty = true;
  }

  if (cfg_changed) {
    persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
    if (s_colors_dirty) {
      prv_apply_window_background();
      prv_setup_logo();
      // Recolor chrome layers (slot layers will pick up colors via create below)
      text_layer_set_text_color(s_header_layer, prv_color(COLOR_ROLE_ACCENT));
      text_layer_set_text_color(s_time_layer, prv_color(COLOR_ROLE_VALUES));
      text_layer_set_text_color(s_event_overlay_layer, prv_color(COLOR_ROLE_HIGHLIGHTS));
      layer_mark_dirty(s_decorations_layer);
      s_colors_dirty = false;
    }
    // Rebuild slot layers (handles FIELD_NONE skipping + font scaling)
    prv_create_slots();
    prv_render_all_slots();
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

// ─── Window load/unload ───────────────────────────────────────────────────────
static void main_window_load(Window *window) {
  prv_apply_window_background();
  Layer *root = window_get_root_layer(window);

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

  // Special event overlay — covers the field area, hidden by default.
  // Position matches the chrome_bottom calculation used by prv_create_slots
  // and prv_setup_logo (single source of truth: prv_overlay_geometry).
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w, h = bounds.size.h;
  int overlay_top, overlay_h;
  prv_overlay_geometry(w, h, &overlay_top, &overlay_h);
  s_event_overlay_layer = text_layer_create(
    GRect(4, overlay_top, w - 8, overlay_h));

  GFont overlay_font = prv_select_font(28, false, true);
  text_layer_set_background_color(s_event_overlay_layer, GColorClear);
  text_layer_set_text_color(s_event_overlay_layer, prv_color(COLOR_ROLE_HIGHLIGHTS));
  text_layer_set_font(s_event_overlay_layer, overlay_font);
  text_layer_set_text_alignment(s_event_overlay_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_event_overlay_layer, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(s_event_overlay_layer));
  layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), true);

  // Mission-complete logo — built lazily based on color theme.
  // s_colors_dirty starts true, so the first prv_update_display_ui call will
  // trigger prv_setup_logo before showing the logo.
  prv_setup_logo();
  s_colors_dirty = false;

  // Initial time
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  prv_update_time(t);

  prv_update_display_ui();
  prv_render_all_slots();
  prv_update_display_ui();

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
  if (s_logo_layer) { layer_destroy(s_logo_layer); s_logo_layer = NULL; }
  if (s_logo_pdc)   { gdraw_command_image_destroy(s_logo_pdc); s_logo_pdc = NULL; }
}

// ─── Init / deinit ────────────────────────────────────────────────────────────
static void init(void) {
  prv_load_settings();
  prv_load_artemis();

  // Load custom Artemis fonts
  s_artemis_font_14 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARTEMIS_14));
  s_artemis_font_18 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARTEMIS_18));
  s_artemis_font_24 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARTEMIS_24));
  s_artemis_font_36 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARTEMIS_36));

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

  // Unload custom fonts
  fonts_unload_custom_font(s_artemis_font_14);
  fonts_unload_custom_font(s_artemis_font_18);
  fonts_unload_custom_font(s_artemis_font_24);
  fonts_unload_custom_font(s_artemis_font_36);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
