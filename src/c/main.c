/**
 * @file main.c
 * @brief Artemis II watchface — entry point and display orchestrator.
 *
 * Defines all shared globals (declared extern in artemis.h) and implements
 * the three-mode display state machine:
 *
 *   DISPLAY_LOGO  — default; logo visible, bottom zone normal.
 *   DISPLAY_EVENT — special event active; event banner replaces logo.
 *   DISPLAY_INFO  — after shake (if enabled); info slots visible,
 *                   bottom zone hidden; auto-reverts to LOGO after
 *                   s_settings.info_display_s seconds.
 *
 * Events always override logo but never interrupt info (info is "sticky"
 * until the timer fires). Timeline Peek is handled per-mode: logo/event
 * compress the bottom zone via artemis_clock_peek(); info mode hides it.
 *
 * @author LP Fabiani
 * @date 2026
 */
#include <pebble.h>
#include "artemis.h"
#include "artemis_comms.h"
#include "artemis_event.h"
#include "artemis_logo.h"
#include "artemis_clock.h"
#include "artemis_info.h"

// ─── Shared globals (declared extern in artemis.h) ────────────────────────────
ArtemisSettings s_settings;
ArtemisData     s_artemis;
Layer          *s_root_layer = NULL;
int             s_root_w = 0, s_root_h = 0;
int             s_split_y = 0;
GFont           s_font_time  = NULL;
GFont           s_font_date  = NULL;
GFont           s_font_event = NULL;

static Window     *s_main_window  = NULL;
static DisplayMode s_display_mode = DISPLAY_LOGO;
static AppTimer   *s_info_timer   = NULL;

// ─── Overlay geometry ─────────────────────────────────────────────────────────
void overlay_geometry(int *out_top, int *out_h) {
  *out_top = 4;
  *out_h   = s_split_y;
}

// ─── Info timer (auto-revert from DISPLAY_INFO to DISPLAY_LOGO) ───────────────
static void prv_info_timer_callback(void *context) {
  s_info_timer   = NULL;
  s_display_mode = DISPLAY_LOGO;
  artemis_update_display();
}

// ─── Shared info trigger ──────────────────────────────────────────────────────
// Called by both the accel-tap (shake) and the SELECT-click (touch) handlers.
static void prv_show_info(void) {
  if (!s_settings.info_on_shake) return;
  if (s_info_timer) app_timer_cancel(s_info_timer);
  uint32_t ms = (uint32_t)(s_settings.info_display_s > 0
                            ? s_settings.info_display_s : 10) * 1000;
  s_info_timer   = app_timer_register(ms, prv_info_timer_callback, NULL);
  s_display_mode = DISPLAY_INFO;
  artemis_update_display();
}

// ─── Shake handler ────────────────────────────────────────────────────────────
static void prv_tap_handler(AccelAxisType axis, int32_t direction) {
  prv_show_info();
}

// ─── Touch handler (Rebble TouchService — runtime-guarded) ───────────────────
static void prv_touch_handler(const TouchEvent *event, void *context) {
  if (event->type == TouchEvent_Touchdown) prv_show_info();
}

// ─── Shake + touch subscription management ───────────────────────────────────
void artemis_apply_shake_setting(void) {
  accel_tap_service_unsubscribe();
  if (touch_service_is_enabled()) touch_service_unsubscribe();

  if (s_settings.info_on_shake) {
    accel_tap_service_subscribe(prv_tap_handler);
    if (touch_service_is_enabled()) touch_service_subscribe(prv_touch_handler, NULL);
  } else {
    // Revert info mode immediately if it was active when shake was disabled
    if (s_info_timer) { app_timer_cancel(s_info_timer); s_info_timer = NULL; }
    if (s_display_mode == DISPLAY_INFO) {
      s_display_mode = DISPLAY_LOGO;
      artemis_update_display();
    }
  }
}

// ─── Display orchestration ────────────────────────────────────────────────────
void artemis_update_display(void) {
  // Always check for events — vibration fires on transition even in info mode
  const char *event_msg = artemis_event_check();

  // Event overrides logo but not info (info is sticky until timer)
  if (s_display_mode != DISPLAY_INFO) {
    s_display_mode = event_msg ? DISPLAY_EVENT : DISPLAY_LOGO;
  }

  switch (s_display_mode) {
    case DISPLAY_LOGO:
      artemis_logo_show();
      artemis_event_hide();
      artemis_info_hide();
      artemis_clock_show();
      break;

    case DISPLAY_EVENT:
      artemis_event_update(event_msg);
      artemis_event_show();
      artemis_logo_hide();
      artemis_info_hide();
      artemis_clock_show();
      break;

    case DISPLAY_INFO:
      artemis_logo_hide();
      if (s_artemis.mission_complete) {
        artemis_event_update("No Artemis mission\nongoing");
        artemis_event_show();
        artemis_info_hide();
      } else {
        artemis_info_refresh();
        artemis_info_show();
        artemis_event_hide();
      }
      artemis_clock_show();  // hidden only if peek is active (see prv_handle_peek)
      break;
  }
}

// ─── Settings ─────────────────────────────────────────────────────────────────
#define DEFAULT_UPDATE_INTERVAL  30
#define DEFAULT_USE_MILES        false
#define DEFAULT_VIBRATE_EVENTS   true
#define DEFAULT_INFO_ON_SHAKE    true
#define DEFAULT_INFO_DISPLAY_S   10

// Default slot assignments (platform-specific)
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
  s_settings.version             = SETTINGS_VERSION;
  s_settings.update_interval_min = DEFAULT_UPDATE_INTERVAL;
  s_settings.use_miles           = DEFAULT_USE_MILES;
  s_settings.vibrate_events      = DEFAULT_VIBRATE_EVENTS;
  s_settings.info_on_shake       = DEFAULT_INFO_ON_SHAKE;
  s_settings.info_display_s      = DEFAULT_INFO_DISPLAY_S;
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
  memset(&s_artemis, 0, sizeof(s_artemis));
  // -- The following defaults are set by the memset
  // s_artemis.speed_x100        = 0;
  // s_artemis.distance_km       = 0;
  // s_artemis.moon_distance_km  = 0;
  // s_artemis.last_update_epoch  = 0;
  // s_artemis.g_force_x10000    = 0;
  // s_artemis.altitude_km       = 0;
  // s_artemis.periapsis_km      = 0;
  // s_artemis.apoapsis_km       = 0;
  // s_artemis.signal_x100       = 0;
  // s_artemis.downlink_kbps     = 0;
  // for (int i = 0; i < MAX_UPCOMING; i++) {
  //   s_artemis.upcoming[i].name[0] = '\0';
  //   s_artemis.upcoming[i].epoch   = 0;
  // }

  s_artemis.milestone_met_ms  = -1;
  // Assume mission complete until phone confirms otherwise — shows logo at
  // startup instead of placeholder fields while waiting for /api/all response.
  s_artemis.mission_complete   = true;
  strncpy(s_artemis.phase, "...", sizeof(s_artemis.phase) - 1);
  strncpy(s_artemis.milestone_name, "...", sizeof(s_artemis.milestone_name) - 1);
  strncpy(s_artemis.dsn_station, "...", sizeof(s_artemis.dsn_station) - 1);
}

static void prv_load_artemis(void) {
  prv_default_artemis();
  ArtemisData loaded;
  if (persist_read_data(ARTEMIS_KEY, &loaded, sizeof(loaded)) > 0) {
    s_artemis = loaded;
  }
}

// ─── Timeline Peek ────────────────────────────────────────────────────────────
// .change fires each animation frame as peek slides in/out.
// .did_change fires once when the peek settles (or fully dismisses).
//
// Logo/Event: compress bottom zone smoothly via artemis_clock_peek().
// Info: bottom zone is normally visible; hide it only while peek is active
//       so info slots are not obscured by the peek drawer.
static void prv_handle_peek(int unobstructed_h) {
  if (s_display_mode == DISPLAY_INFO) {
    if (unobstructed_h < s_root_h) {
      artemis_clock_hide();
    } else {
      artemis_clock_show();
    }
  } else {
    artemis_clock_peek(unobstructed_h);
  }
}

static void prv_unobstructed_change(AnimationProgress progress, void *context) {
  (void)progress;
  GRect u = layer_get_unobstructed_bounds(s_root_layer);
  prv_handle_peek(u.size.h);
}

static void prv_unobstructed_did_change(void *context) {
  GRect u = layer_get_unobstructed_bounds(s_root_layer);
  prv_handle_peek(u.size.h);
}

// ─── Layer helper ────────────────────────────────────────────────────────────
TextLayer *artemis_make_text_layer(Layer *root, GRect r, GColor col,
                                 GFont font, GTextAlignment align) {
  TextLayer *tl = text_layer_create(r);
  text_layer_set_background_color(tl, GColorClear);
  text_layer_set_text_color(tl, col);
  text_layer_set_font(tl, font);
  text_layer_set_text_alignment(tl, align);
  layer_add_child(root, text_layer_get_layer(tl));
  return tl;
}

// ─── Tick handler ─────────────────────────────────────────────────────────────
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // This is a precaution to save battery because, although we only subscribed to minutes,
  // during the tests sometimes we were awaken every second.
  if (!(units_changed & MINUTE_UNIT)) return;
  // Data refresh only when info slots are visible and schedule matches
  if (s_display_mode == DISPLAY_INFO) {
    int iv = (int)s_settings.update_interval_min;
    if (iv < 1) iv = 30;
    if (tick_time->tm_min % iv == 0) artemis_comms_request_data();
  }
  artemis_clock_refresh(tick_time);
  artemis_update_display();
}

// ─── Window load/unload ───────────────────────────────────────────────────────
static void main_window_load(Window *window) {
  window_set_background_color(window, ARTEMIS_COLOR_SKY);
  Layer *root = window_get_root_layer(window);
  s_root_layer = root;
  GRect bounds = layer_get_bounds(root);
  s_root_w  = bounds.size.w;
  s_root_h  = bounds.size.h;
  s_split_y = s_root_h * 60 / 100;

  artemis_clock_create(root);   // sky + s_time_area_layer (moon, time, date)
  artemis_info_create(root);   // info container + slots + decorations
  artemis_event_create(root);  // event overlay banner
  artemis_logo_create(root);   // mission logo (z-order: topmost)

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  artemis_clock_refresh(t);
  artemis_update_display();
}

static void main_window_unload(Window *window) {
  if (s_info_timer) { app_timer_cancel(s_info_timer); s_info_timer = NULL; }
  artemis_logo_destroy();
  artemis_event_destroy();
  artemis_info_destroy();
  artemis_clock_destroy();
}

// ─── Init / deinit ────────────────────────────────────────────────────────────
static void init(void) {
  prv_load_settings();
  prv_load_artemis();

  s_font_time  = fonts_load_custom_font(resource_get_handle(FONT_TIME));
  s_font_date  = fonts_load_custom_font(resource_get_handle(FONT_DATE));
  s_font_event = fonts_load_custom_font(resource_get_handle(FONT_EVENT));

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load   = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  UnobstructedAreaHandlers peek_handlers = {
    .change     = prv_unobstructed_change,
    .did_change = prv_unobstructed_did_change,
  };
  unobstructed_area_service_subscribe(peek_handlers, NULL);

  artemis_apply_shake_setting();  // subscribe to accel tap if enabled
  artemis_comms_init();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  unobstructed_area_service_unsubscribe();
  accel_tap_service_unsubscribe();
  if (touch_service_is_enabled()) touch_service_unsubscribe();
  window_destroy(s_main_window);
  fonts_unload_custom_font(s_font_time);
  fonts_unload_custom_font(s_font_date);
  fonts_unload_custom_font(s_font_event);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
