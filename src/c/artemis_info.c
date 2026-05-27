/**
 * @file artemis_info.c
 * @brief Top-zone info display: slot rendering, decorations, and event overlay.
 *
 * Owns @c s_info_layer (parent container for field label/value layers and the
 * decorations layer) and @c s_event_overlay_layer (independent, full top-zone
 * text layer for special event banners). @c artemis_info_refresh() implements
 * the display priority logic: special event → mission fields → mission complete.
 * @c artemis_info_rebuild_slots() tears down and recreates slot layers when the
 * configuration changes.
 *
 * @author LP Fabiani
 * @date 2026
 */
#include "artemis_info.h"
#include "artemis.h"
#include "artemis_mission.h"

// ─── Owned globals ────────────────────────────────────────────────────────────
Layer     *s_info_layer         = NULL;
static Layer     *s_decorations_layer  = NULL;
static TextLayer *s_event_overlay_layer = NULL;

static TextLayer *s_field_label_layers[MAX_SLOTS];
static TextLayer *s_field_value_layers[MAX_SLOTS];

int  s_active_slots[MAX_SLOTS];
int  s_num_active = 0;

static char s_met_buf[16];
static char s_slot_label_bufs[MAX_SLOTS][20];
static char s_slot_value_bufs[MAX_SLOTS][24];

// ─── Display state (private) ──────────────────────────────────────────────────
static const char *s_active_event_msg = NULL;

typedef enum {
  DISPLAY_STATE_UNKNOWN = 0,
  DISPLAY_STATE_EVENT,
  DISPLAY_STATE_LOGO,
  DISPLAY_STATE_FIELDS
} DisplayState;

static DisplayState s_current_display_state = DISPLAY_STATE_UNKNOWN;

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
// Must be called after s_info_layer is created and slots are added, so that
// s_decorations_layer is the last child of s_info_layer (drawn on top of fields).
static void prv_create_chrome(void) {
  GRect bounds = layer_get_bounds(s_info_layer);
  int w = bounds.size.w, h = bounds.size.h;

  s_decorations_layer = layer_create(GRect(0, 0, w, h));
  layer_set_update_proc(s_decorations_layer, decorations_update_proc);
  layer_add_child(s_info_layer, s_decorations_layer);

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
      s_field_label_layers[si] = prv_make_layer(s_info_layer, GRect(cx, y, col_w, label_h),
        ARTEMIS_COLOR_ACCENT, label_font, GTextAlignmentCenter);
      s_field_value_layers[si] = prv_make_layer(s_info_layer, GRect(cx, y + val_y_offset, col_w, val_h),
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
    s_field_label_layers[si] = prv_make_layer(s_info_layer, GRect(sx, y, sw, label_h),
      ARTEMIS_COLOR_ACCENT, label_font, GTextAlignmentCenter);
    s_field_value_layers[si] = prv_make_layer(s_info_layer, GRect(sx, y + val_y_offset, sw, val_h),
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
    s_field_label_layers[si] = prv_make_layer(s_info_layer, GRect(4, y, lw - 4, rh),
      ARTEMIS_COLOR_ACCENT, label_font, GTextAlignmentLeft);
    s_field_value_layers[si] = prv_make_layer(s_info_layer, GRect(vx, y, vw, rh),
      ARTEMIS_COLOR_VALUES, value_font, GTextAlignmentRight);
    text_layer_set_overflow_mode(s_field_value_layers[si], GTextOverflowModeTrailingEllipsis);
  }
}
#endif

// ─── Public API ───────────────────────────────────────────────────────────────
void artemis_info_create(Layer *root) {
  // Initialize all slot pointers to NULL
  for (int i = 0; i < MAX_SLOTS; i++) {
    s_field_label_layers[i] = NULL;
    s_field_value_layers[i] = NULL;
  }

  // Info layer: top-zone container for fields + decorations.
  // +1 px so the zone-separator line drawn at y=s_split_y is not clipped.
  s_info_layer = layer_create(GRect(0, 0, s_root_w, s_split_y + 1));
  layer_add_child(root, s_info_layer);

  // Slot layers — added as children of s_info_layer
  prv_create_slots();

  // Decorations added last within s_info_layer so lines draw over field text
  prv_create_chrome();

  // Event overlay — independent child of root (not inside s_info_layer)
  int overlay_top, overlay_h;
  overlay_geometry(s_root_w, s_root_h, &overlay_top, &overlay_h);
  s_event_overlay_layer = text_layer_create(GRect(4, overlay_top, s_root_w - 8, overlay_h));
  text_layer_set_background_color(s_event_overlay_layer, GColorClear);
  text_layer_set_text_color(s_event_overlay_layer, ARTEMIS_COLOR_ACCENT);
  text_layer_set_font(s_event_overlay_layer, s_font_label);
  text_layer_set_text_alignment(s_event_overlay_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_event_overlay_layer, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(s_event_overlay_layer));
  layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), true);
}

void artemis_info_destroy(void) {
  if (s_decorations_layer)   { layer_destroy(s_decorations_layer);                    s_decorations_layer   = NULL; }
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (s_field_label_layers[i]) { text_layer_destroy(s_field_label_layers[i]); s_field_label_layers[i] = NULL; }
    if (s_field_value_layers[i]) { text_layer_destroy(s_field_value_layers[i]); s_field_value_layers[i] = NULL; }
  }
  if (s_info_layer)          { layer_destroy(s_info_layer);                           s_info_layer          = NULL; }
  if (s_event_overlay_layer) { text_layer_destroy(s_event_overlay_layer);             s_event_overlay_layer = NULL; }
}

void artemis_info_show(void) {
  if (s_info_layer) layer_set_hidden(s_info_layer, false);
}

void artemis_info_hide(void) {
  if (s_info_layer) layer_set_hidden(s_info_layer, true);
}

bool artemis_info_refresh(void) {
  if (!s_event_overlay_layer || !s_info_layer) return false;

  const char *event_msg = prv_get_special_event();

  // Vibrate only on transition to a new event
  if (event_msg != s_active_event_msg && event_msg != NULL && s_settings.vibrate_events) {
    static const uint32_t segments[] = { 200, 100, 200, 100, 400 };
    VibePattern pat = { .durations = segments, .num_segments = 5 };
    vibes_enqueue_custom_pattern(pat);
  }
  s_active_event_msg = event_msg;

  if (event_msg) {
    if (DEBUG_ENABLED) APP_LOG(APP_LOG_LEVEL_DEBUG, "→ Showing EVENT OVERLAY");
    layer_set_hidden(s_info_layer, true);
    layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), false);
    text_layer_set_text(s_event_overlay_layer, event_msg);
    return true;
  } else if (!s_artemis.mission_complete) {
    if (DEBUG_ENABLED) APP_LOG(APP_LOG_LEVEL_DEBUG, "→ Showing NORMAL FIELDS");
    layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), true);
    layer_set_hidden(s_info_layer, false);
    prv_calculate_met();
    prv_render_all_slots();
    return true;
  } else {
    if (DEBUG_ENABLED) APP_LOG(APP_LOG_LEVEL_DEBUG, "→ Top zone empty (mission complete)");
    layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), true);
    layer_set_hidden(s_info_layer, true);
    return false;
  }
}

void artemis_info_rebuild_slots(void) {
  prv_create_slots();
}
