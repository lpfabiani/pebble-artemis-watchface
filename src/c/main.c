/**
 * @file main.c
 * @brief Artemis II watchface — entry point and display orchestrator.
 *
 * Defines all shared globals (declared extern in artemis.h), the
 * @c overlay_geometry() helper, and @c artemis_update_display(), which
 * bridges @c artemis_info_refresh() and @c artemis_logo_show/hide().
 * Window lifecycle, tick handler, font loading, and settings persistence
 * are also managed here; all rendering is delegated to the sub-modules.
 *
 * @author LP Fabiani
 * @date 2026
 */
#include <pebble.h>
#include "artemis.h"
#include "artemis_comms.h"
#include "artemis_logo.h"
#include "artemis_main.h"
#include "artemis_info.h"

// ─── Shared globals (declared extern in artemis.h) ────────────────────────────
ArtemisSettings s_settings;
ArtemisData     s_artemis;
Layer          *s_root_layer = NULL;
int             s_root_w = 0, s_root_h = 0;
int             s_split_y = 0;
GFont           s_font_time  = NULL;
GFont           s_font_date  = NULL;
GFont           s_font_label = NULL;

static Window *s_main_window;

// ─── Overlay geometry ─────────────────────────────────────────────────────────
void overlay_geometry(int w, int h, int *out_top, int *out_h) {
  (void)w; (void)h;
  *out_top = 4;
  *out_h   = s_split_y;
}

// ─── Display orchestration ────────────────────────────────────────────────────
void artemis_update_display(void) {
  artemis_logo_show();
/* // FUTURE: Show information.
  bool top_occupied = artemis_info_refresh();
  if (top_occupied) {
    artemis_logo_hide();
  } else {
    artemis_logo_show();
  }
*/
}

// ─── Settings ─────────────────────────────────────────────────────────────────
#define DEFAULT_UPDATE_INTERVAL  30
#define DEFAULT_USE_MILES        false
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
  #define DEFAULT_SLOT_4  FIELD_SPEED
  #define DEFAULT_SLOT_5  FIELD_NEXT_EVENT
#endif

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

// ─── Tick handler ─────────────────────────────────────────────────────────────
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (!s_artemis.mission_complete) {
    int iv = (int)s_settings.update_interval_min;
    if (iv < 1) iv = 30;
    if (tick_time->tm_min % iv == 0) artemis_comms_request_data();
  }
  artemis_main_refresh(tick_time);
  artemis_update_display();
}

// ─── Window load/unload ───────────────────────────────────────────────────────
static void main_window_load(Window *window) {
  prv_apply_window_background();
  Layer *root = window_get_root_layer(window);
  s_root_layer = root;
  GRect bounds = layer_get_bounds(root);
  s_root_w = bounds.size.w;
  s_root_h = bounds.size.h;
  s_split_y = s_root_h * 60 / 100;

  artemis_main_create(root);   // sky + moon + time + date (z-order: bottom)
//  artemis_info_create(root);   // info container + slots + decorations + event overlay
  artemis_logo_create(root);   // mission-complete logo (z-order: top)

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  artemis_main_refresh(t);
  artemis_update_display();
}

static void main_window_unload(Window *window) {
  artemis_logo_destroy();
//  artemis_info_destroy();
  artemis_main_destroy();
}

// ─── Init / deinit ────────────────────────────────────────────────────────────
static void init(void) {
  prv_load_settings();
  prv_load_artemis();

  s_font_time  = fonts_load_custom_font(resource_get_handle(FONT_TIME));
  s_font_date  = fonts_load_custom_font(resource_get_handle(FONT_DATE));
  s_font_label = fonts_load_custom_font(resource_get_handle(FONT_LABEL));

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load   = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  // battery_state_service_subscribe(battery_callback);  // removed: battery bar eliminated

  artemis_comms_init();
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
