#include <pebble.h>
#include "artemis_config.h"

static ArtemisSettings s_settings;
static ArtemisData     s_artemis;
// ─── Fonts ───────────────────────────────────────────────────────────────────
static GFont s_font_time  = NULL;  // large: time display
static GFont s_font_date  = NULL;  // small: date and slot labels on compact screens
static GFont s_font_label = NULL;  // medium: slot labels and event overlay

// ─── Layers ───────────────────────────────────────────────────────────────────
static Window    *s_main_window;
static Layer *s_root_layer = NULL;  // retained for dynamic slot recreation
static int s_root_w, s_root_h; // Screen sizes
static int        s_split_y = 0;  // screen_h * 60 / 100; divides top zone from bottom zone

// --- Bottom
static Layer     *s_decorations_layer;
static BitmapLayer *s_moon_bitmap_layer;
static GBitmap   *s_moon_bitmap;
// static Layer     *s_battery_layer;  // removed: battery bar eliminated
// static TextLayer *s_header_layer;   // removed: header "ARTEMIS II" eliminated
static TextLayer    *s_time_layer;
static TextLayer    *s_date_layer;

// --- Top
static Layer     *s_sky_layer;
static Layer        *s_logo_layer;
static GDrawCommandImage *s_logo_pdc;
static GSize             s_logo_draw_size;
static GPoint            s_logo_draw_offset;

static TextLayer    *s_field_label_layers[MAX_SLOTS];
static TextLayer    *s_field_value_layers[MAX_SLOTS];
static TextLayer    *s_event_overlay_layer;  // full-screen special event message

// ─── Active slot mapping ──────────────────────────────────────────────────────
// s_active_slots[i] = index into s_settings.slots[] for the i-th visible slot.
// Slots set to FIELD_NONE are skipped. s_num_active is the count.
static int s_active_slots[MAX_SLOTS];
static int s_num_active = 0;

// static int s_battery_level = 100;  // removed: battery bar eliminated

// ─── Buffers ──────────────────────────────────────────────────────────────────
static char s_time_buf[10];  // "12:59 PM\0" = 9 chars in 12h; "23:59\0" = 6 in 24h
static char s_date_buf[24];
static char s_met_buf[16];
static char s_slot_label_bufs[MAX_SLOTS][20];
static char s_slot_value_bufs[MAX_SLOTS][24];

// ==============================
// ==  FUNCTIONS  ==
// ==============================

// ─── Default settings ─────────────────────────────────────────────────────────
static void prv_default_settings(void) {
  s_settings.version              = SETTINGS_VERSION;
  s_settings.update_interval_min  = DEFAULT_UPDATE_INTERVAL;
  s_settings.use_miles            = DEFAULT_USE_MILES;
  /* ── Color theme fields removed: single Night Sky palette ──
  s_settings.color_theme          = DEFAULT_COLOR_THEME;
  s_settings.color_background     = DEFAULT_COLOR_BACKGROUND;
  s_settings.color_accent         = DEFAULT_COLOR_ACCENT;
  s_settings.color_values         = DEFAULT_COLOR_VALUES;
  s_settings.color_highlights     = DEFAULT_COLOR_HIGHLIGHTS;
  ── */
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

static void prv_apply_window_background(void) {
  window_set_background_color(s_main_window, ARTEMIS_COLOR_SKY);
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
      // Accent color (blue) for ETA countdown
      text_layer_set_text_color(val, ARTEMIS_COLOR_ACCENT);
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

  text_layer_set_text_color(val, ARTEMIS_COLOR_VALUES);
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
  if (clock_is_24h_style()) {
    strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", tick_time);
  }
  else{
    strftime(s_time_buf, sizeof(s_time_buf), "%I:%M %p", tick_time);
  }
  text_layer_set_text(s_time_layer, s_time_buf);
#ifdef PBL_ROUND
  strftime(s_date_buf, sizeof(s_date_buf), "%a %d", tick_time);   // "TUE 26" — fits narrow chord
#else
  strftime(s_date_buf, sizeof(s_date_buf), "%a, %b %d", tick_time);
#endif
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

// ─── Sky background (stars) ───────────────────────────────────────────────────
static void sky_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int w = bounds.size.w, h = bounds.size.h;

  // Solid black background
  graphics_context_set_fill_color(ctx, ARTEMIS_COLOR_SKY);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  /* ── Gradient removed: two-band navy/black didn't look good ──
#ifdef PBL_COLOR
  int mid = h / 2;
  graphics_context_set_fill_color(ctx, ARTEMIS_COLOR_SKY);
  graphics_fill_rect(ctx, GRect(0, 0, w, mid), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, ARTEMIS_COLOR_SKY_HORIZON);
  graphics_fill_rect(ctx, GRect(0, mid, w, h - mid), 0, GCornerNone);
#endif
  ── */

  // Stars — positions normalized to 144×168, scaled to actual screen
  static const GPoint STARS_144[] = {
  // Main Orion stars
  {60, 6},                              // Meissa (head)
  {30,22}, {94,18},                     // Betelgeuse, Bellatrix (shoulders)
  {48,44}, {64,47}, {80,44},            // Belt: Mintaka, Alnilam, Alnitak
  {36,68}, {98,65},                     // Saiph, Rigel (feet)
  // Background fill
  {8,10},{118,8},{5,55},{135,35},
  {20,85},{130,80},{50,90},{105,90},
  {72,30},{25,40},{110,55},{145,65}
  };
  int num_stars = (int)(sizeof(STARS_144) / sizeof(STARS_144[0]));

  graphics_context_set_stroke_color(ctx, ARTEMIS_COLOR_SKY_STARS);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < num_stars; i++) {
    int sx = STARS_144[i].x * w / 144;
    int sy = STARS_144[i].y * h / 168;
    if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
      graphics_draw_pixel(ctx, GPoint(sx, sy));
    }
  }
}

// ─── Logo (PDC vector) ────────────────────────────────────────────────────────
static void prv_scale_pdc_image(GDrawCommandImage *img, GSize target);

static void logo_update_proc(Layer *layer, GContext *ctx) {
  if (!s_logo_pdc) return;
  gdraw_command_image_draw(ctx, s_logo_pdc, s_logo_draw_offset);
}

// Top zone: 0 → s_split_y. No header or chrome above the content area.
static void prv_overlay_geometry(int w, int h, int *out_top, int *out_h) {
  (void)w; (void)h;
  *out_top = 4;
  *out_h   = s_split_y;
}

// Builds (or rebuilds) the logo layer + scaled PDC for the current color
// theme. Creates the layer the first time it is called; subsequent calls
// release the old PDC, load the correct variant, rescale, and reposition
// the existing layer.
static void prv_setup_logo(void) {
  if (!s_root_layer) return;

  int top_zone_y, top_zone_h;
  prv_overlay_geometry(s_root_w, s_root_h, &top_zone_y, &top_zone_h);

  // Release any previously loaded PDC — scaling is destructive in-place.
  if (s_logo_pdc) {
    gdraw_command_image_destroy(s_logo_pdc);
    s_logo_pdc = NULL;
  }

  s_logo_pdc = gdraw_command_image_create_with_resource(LOGO_RESOURCE);

  // Logo is ~1/6 taller than the top zone so the arc at the base of the A
  // spills into the bottom zone and sits naturally over the moon image.
  // Capped by screen width minus 8px side margins.
  int max_logo_side = top_zone_h + top_zone_h / 6;
  if (max_logo_side > s_root_w - 8) max_logo_side = s_root_w - 8;
  if (max_logo_side < 1) max_logo_side = 1;

  if (s_logo_pdc) {
    GSize pdc_original_size = gdraw_command_image_get_bounds_size(s_logo_pdc);
    APP_LOG(APP_LOG_LEVEL_INFO, "LOGO: pdc_original_size.w (%d), pdc_original_size.h (%d)", pdc_original_size.w, pdc_original_size.h);
    if (pdc_original_size.w > 0 && pdc_original_size.h > 0) {
      // Scale to fit max_logo_side on the longer axis, preserving aspect ratio.
      int logo_w = max_logo_side, logo_h = max_logo_side;
      if (pdc_original_size.w >= pdc_original_size.h) {
        // Wider than tall: constrain width, shrink height proportionally.
        logo_h = (max_logo_side * pdc_original_size.h) / pdc_original_size.w;
      } else {
        // Taller than wide: constrain height, shrink width proportionally.
        logo_w = (max_logo_side * pdc_original_size.w) / pdc_original_size.h;
      }
      s_logo_draw_size = GSize(logo_w, logo_h);
    } else {
      s_logo_draw_size = GSize(max_logo_side, max_logo_side);
    }
    prv_scale_pdc_image(s_logo_pdc, s_logo_draw_size);
  } else {
    s_logo_draw_size = GSize(max_logo_side, max_logo_side);
  }

  // Draw offset is always zero: the layer is positioned to match the logo exactly.
  s_logo_draw_offset = GPoint(0, 0);

  // Horizontally centered on screen; top-anchored with 2px gap so the
  // extra height spills downward into the moon zone, not upward.
  int logo_x = (s_root_w - s_logo_draw_size.w) / 2;
  int logo_y = top_zone_y + 2;
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

/* ── Battery bar removed ──────────────────────────────────────────────────────
static void battery_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
#ifdef PBL_ROUND
  int32_t a_start = DEG_TO_TRIGANGLE(-90);
  int32_t a_end   = DEG_TO_TRIGANGLE(-90 + (s_battery_level * 360) / 100);
  // ... radial arc drawing ...
#else
  int bar_w = (bounds.size.w * s_battery_level) / 100;
  // ... rect bar drawing ...
#endif
}

static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  layer_mark_dirty(s_battery_layer);
}
── */

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
  // Separator rows within the top zone, plus the zone-split line at s_split_y
  int first_line_y = 1;  // top zone starts at Y=0
  int num_lines = 1 + (int)(s_num_active / 2);
  int separation_between_lines = (int)(0.9 * s_split_y) / (num_lines);
  for (int i = 0; i < num_lines; i++) {
    prv_draw_horizontal_line(ctx, first_line_y + separation_between_lines * i, layer_radius);
  }
  // Zone separator at s_split_y
  prv_draw_horizontal_line(ctx, s_split_y, layer_radius);
  int num_singles = (s_num_active + 1) % 2 + 1;
  int last_line_y = first_line_y + separation_between_lines * (num_lines - num_singles);
  // Column divider for paired rows
  graphics_draw_line(ctx, GPoint(layer_radius, first_line_y), GPoint(layer_radius, last_line_y));

#else // rect
  int lm = 4, rm = layer_width - 4;
  // Zone separator: top zone / bottom zone dividing line
  graphics_draw_line(ctx, GPoint(lm, s_split_y), GPoint(rm, s_split_y));
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
  /* ── Artemis dynamic font selection removed: use s_font_time/s_font_date/s_font_label directly ──
  if (use_artemis) {
    if (height >= 62) return s_artemis_font_52;
    if (height >= 48) return s_artemis_font_42;
    if (height >= 44) return s_artemis_font_36;
    if (height >= 36) return s_artemis_font_24;
    if (height >= 28) return s_artemis_font_18;
    return s_artemis_font_14;
  } else {
  ── */
  (void)use_artemis;
  {
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

// ─── Chrome: decorations layer only (battery + header removed) ───────────────
static void prv_create_chrome(Layer *root) {
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w, h = bounds.size.h;

  s_decorations_layer = layer_create(GRect(0, 0, w, h));

  /* ── Battery bar removed ────────────────────────────────────────────────────
  #ifdef PBL_ROUND
    s_battery_layer = layer_create(GRect(0, 0, w, h));  // radial arc
  #else
    s_battery_layer = layer_create(GRect(0, 0, w, 3));  // thin rect bar
  #endif
  ── */

  /* ── Header "ARTEMIS II" removed ───────────────────────────────────────────
  #ifdef PBL_ROUND
    s_header_layer = prv_make_layer(root, GRect(cx-60, 6, 120, 14), ...);
  #else
    s_header_layer = prv_make_layer(root, GRect(0, 4, w, 14), ...);
  #endif
  ── */

  /* ── Time + date moved to prv_create_bottom_zone ───────────────────────────
  s_time_layer = prv_make_layer(...);
  s_date_layer = prv_make_layer(...);
  ── */

  (void)w; (void)h;
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

  // Top zone: 0 → s_split_y (no battery or header above slots)
  int chrome_bottom = 0;

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

  // Row heights fit within top zone (0 → s_split_y)
  int separation_between_lines = (int)(0.9 * (s_split_y - chrome_bottom)) / (num_lines);
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

  GFont label_font = s_font_label;
  GFont value_font = prv_select_font(val_h, false, false);

  for (int p = 0; p < num_pairs; p++) {
    for (int side = 0; side < 2; side++) {
      int si = s_active_slots[ai++];
      int cx = (side == 0) ? col_l_x : col_r_x;
      s_field_label_layers[si] = prv_make_layer(s_root_layer, GRect(cx, y, col_w, label_h),
        ARTEMIS_COLOR_ACCENT, label_font, GTextAlignmentCenter);
      s_field_value_layers[si] = prv_make_layer(s_root_layer, GRect(cx, y + val_y_offset, col_w, val_h),
        ARTEMIS_COLOR_VALUES, value_font, GTextAlignmentCenter);
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
      ARTEMIS_COLOR_ACCENT, label_font, GTextAlignmentCenter);
    s_field_value_layers[si] = prv_make_layer(s_root_layer, GRect(sx, y + val_y_offset, sw, val_h),
      ARTEMIS_COLOR_VALUES, value_font, GTextAlignmentCenter);
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

  // Top zone: 0 → s_split_y (no battery or header above slots)
  int chrome_bottom = 0;

  GRect bounds = layer_get_bounds(s_root_layer);
  int w = bounds.size.w, h = bounds.size.h;
  (void)h;

  // Top zone: 0 → s_split_y (no battery or header above slots)
  int avail = s_split_y;
  int rh = avail / s_num_active;

  // Label column: fixed 40% of width, value gets the rest
  int lw = w * 40 / 100;
  int vx = lw + 4, vw = w - vx - 4;

  GFont label_font = s_font_label;
  GFont value_font = prv_select_font(rh, false, false);

  for (int i = 0; i < s_num_active; i++) {
    int si = s_active_slots[i];  // index into s_settings.slots[]
    int y = chrome_bottom + i * rh;
    s_field_label_layers[si] = prv_make_layer(s_root_layer, GRect(4, y, lw - 4, rh),
      ARTEMIS_COLOR_ACCENT, label_font, GTextAlignmentLeft);
    s_field_value_layers[si] = prv_make_layer(s_root_layer, GRect(vx, y, vw, rh),
      ARTEMIS_COLOR_VALUES, value_font, GTextAlignmentRight);
    text_layer_set_overflow_mode(s_field_value_layers[si], GTextOverflowModeTrailingEllipsis);
  }
}
#endif

// ─── Bottom zone: moon bitmap + time + date ───────────────────────────────────
static void prv_create_bottom_zone(Layer *root) {
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w, h = bounds.size.h;
  int bottom_h = h - s_split_y;

  // Moon bitmap — bottom-aligned to screen edge; oversized images clip naturally
#ifdef PBL_BW
  s_moon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MOON_BW);
//#elif defined(PBL_PLATFORM_BASALT)
//  s_moon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MOON_SMALL_COLOR);
#else
  s_moon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MOON_COLOR);
#endif

  if (s_moon_bitmap) {
    // Layer is positioned so the image is horizontally centered on screen;
    // x may be negative when the image is wider than the screen (clips on both sides).
    int img_w = gbitmap_get_bounds(s_moon_bitmap).size.w;
    int layer_x = (w - img_w) / 2;
    s_moon_bitmap_layer = bitmap_layer_create(GRect(layer_x, s_split_y, img_w, bottom_h));
    bitmap_layer_set_bitmap(s_moon_bitmap_layer, s_moon_bitmap);
    bitmap_layer_set_alignment(s_moon_bitmap_layer, GAlignCenter);
    bitmap_layer_set_compositing_mode(s_moon_bitmap_layer, GCompOpAssign);
    layer_add_child(root, bitmap_layer_get_layer(s_moon_bitmap_layer));
  }

  // Time + date block — vertically centered in the bottom zone
  //int time_h  = bottom_h - FONT_DATE_H - 8;
  //int block_h = time_h + 2 + FONT_DATE_H;
  int time_h  = FONT_TIME_H;

  int block_h = FONT_TIME_H + FONT_DATE_H + 6;
  int block_y = s_split_y + (bottom_h - block_h) / 2;
  int time_y = block_y;
  int date_y = block_y + time_h + 2;

  s_time_layer = prv_make_layer(root, GRect(0, time_y, w, FONT_TIME_H),
                  ARTEMIS_COLOR_TIME, s_font_time, GTextAlignmentCenter);

  s_date_layer = prv_make_layer(root, GRect(0, date_y, w, FONT_DATE_H),
                  ARTEMIS_COLOR_DATE, s_font_date, GTextAlignmentCenter);
}

// ─── Full layout creation ─────────────────────────────────────────────────────
static void prv_create_layout(Layer *root) {
  s_root_layer = root;
  GRect bounds = layer_get_bounds(s_root_layer);
  s_root_w = bounds.size.w;
  s_root_h = bounds.size.h;

  // Zone split — must be set before anything else uses it
  s_split_y = s_root_h * 60 / 100;

  // Sky background (z-order: bottom-most)
  s_sky_layer = layer_create(bounds);
  layer_set_update_proc(s_sky_layer, sky_update_proc);
  layer_add_child(root, s_sky_layer);

  prv_create_chrome(root);       // decorations layer (not added to root yet)
  prv_create_slots();            // top zone: slot text layers added to root
  prv_create_bottom_zone(root);  // moon bitmap + time + date added to root
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

  /* ── Color settings removed: single Night Sky palette ──────────────────────
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_THEME))) { ... }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_BACKGROUND))) { ... }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_ACCENT))) { ... }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_VALUES))) { ... }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_HIGHLIGHTS))) { ... }
  ── */

  if (cfg_changed) {
    persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
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

  // prv_create_layout: sets s_split_y, adds sky_layer, slot layers, moon, time, date
  prv_create_layout(root);

  // Decorations (separator line) — added above slot/moon layers
  layer_set_update_proc(s_decorations_layer, decorations_update_proc);
  layer_add_child(root, s_decorations_layer);

  /* ── Battery bar removed ──────────────────────────────────────────────────
  layer_set_update_proc(s_battery_layer, battery_update_proc);
  layer_add_child(root, s_battery_layer);
  ── */

  /* ── Header "ARTEMIS II" removed ─────────────────────────────────────────
  text_layer_set_text(s_header_layer, "ARTEMIS II");
  ── */

  // Special event overlay — covers the top zone, hidden by default

  int overlay_top, overlay_h;
  prv_overlay_geometry(s_root_w, s_root_h, &overlay_top, &overlay_h);
  s_event_overlay_layer = text_layer_create(GRect(4, overlay_top, s_root_w - 8, overlay_h));
  text_layer_set_background_color(s_event_overlay_layer, GColorClear);
  text_layer_set_text_color(s_event_overlay_layer, ARTEMIS_COLOR_ACCENT);
  text_layer_set_font(s_event_overlay_layer, s_font_label);
  text_layer_set_text_alignment(s_event_overlay_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_event_overlay_layer, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(s_event_overlay_layer));
  layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), true);

  // Mission-complete logo (top zone)
  prv_setup_logo();

  // Initial time
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  prv_update_time(t);

  prv_update_display_ui();
  prv_render_all_slots();
  prv_update_display_ui();

  /* ── Battery state removed ────────────────────────────────────────────────
  BatteryChargeState bs = battery_state_service_peek();
  s_battery_level = bs.charge_percent;
  layer_mark_dirty(s_battery_layer);
  ── */
}

static void main_window_unload(Window *window) {
  // Sky + decorations
  if (s_sky_layer)         { layer_destroy(s_sky_layer);         s_sky_layer = NULL; }
  if (s_decorations_layer) { layer_destroy(s_decorations_layer); s_decorations_layer = NULL; }

  // Moon bitmap
  if (s_moon_bitmap_layer) { bitmap_layer_destroy(s_moon_bitmap_layer); s_moon_bitmap_layer = NULL; }
  if (s_moon_bitmap)       { gbitmap_destroy(s_moon_bitmap);            s_moon_bitmap = NULL; }

  // Time + date (bottom zone)
  if (s_time_layer) { text_layer_destroy(s_time_layer); s_time_layer = NULL; }
  if (s_date_layer) { text_layer_destroy(s_date_layer); s_date_layer = NULL; }

  // Slot layers
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (s_field_label_layers[i]) { text_layer_destroy(s_field_label_layers[i]); s_field_label_layers[i] = NULL; }
    if (s_field_value_layers[i]) { text_layer_destroy(s_field_value_layers[i]); s_field_value_layers[i] = NULL; }
  }

  if (s_event_overlay_layer) { text_layer_destroy(s_event_overlay_layer); s_event_overlay_layer = NULL; }
  if (s_logo_layer) { layer_destroy(s_logo_layer);                  s_logo_layer = NULL; }
  if (s_logo_pdc)   { gdraw_command_image_destroy(s_logo_pdc);      s_logo_pdc   = NULL; }

  /* ── Removed ──────────────────────────────────────────────────────────────
  layer_destroy(s_battery_layer);
  text_layer_destroy(s_header_layer);
  ── */
}

// ─── Init / deinit ────────────────────────────────────────────────────────────
static void init(void) {
  prv_load_settings();
  prv_load_artemis();

  // Load only the fonts used on this platform
  s_font_time  = fonts_load_custom_font(resource_get_handle(FONT_TIME));
  s_font_date  = fonts_load_custom_font(resource_get_handle(FONT_DATE));
  s_font_label = fonts_load_custom_font(resource_get_handle(FONT_LABEL));

  /* --- THE HEIGHTS ARE DEFINED, but saving this block as it may be usedful in the future.
  // Measure rendered height of each font once — reused throughout layout
  GRect measure_box = GRect(0, 0, 200, 100);
  s_font_time_h  = graphics_text_layout_get_content_size("0", s_font_time,  measure_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).h;
  s_font_date_h  = graphics_text_layout_get_content_size("0", s_font_date,  measure_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).h;
  s_font_label_h = graphics_text_layout_get_content_size("0", s_font_label, measure_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).h;
  */

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load   = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  // battery_state_service_subscribe(battery_callback);  // removed: battery bar eliminated

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_open(768, 64);

  if (!s_artemis.mission_complete) prv_request_artemis_data();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  // battery_state_service_unsubscribe();  // removed: battery bar eliminated
  window_destroy(s_main_window);

  fonts_unload_custom_font(s_font_time);
  fonts_unload_custom_font(s_font_date);
  fonts_unload_custom_font(s_font_label);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
