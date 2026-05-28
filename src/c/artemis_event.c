/**
 * @file artemis_event.c
 * @brief Special event detection and overlay banner rendering.
 *
 * Owns @c s_event_overlay_layer (independent of @c s_info_layer) and all
 * event detection logic: hardcoded lunar flyby table (highest priority) and
 * dynamic upcoming milestones stored from the API. Vibrates once on each
 * transition to a new event. Text is vertically centred in the top zone
 * using @c graphics_text_layout_get_content_size + @c layer_set_frame.
 *
 * @author LP Fabiani
 * @date 2026
 */
#include "artemis_event.h"
#include "artemis.h"

static TextLayer   *s_event_overlay_layer = NULL;
static const char  *s_active_event_msg    = NULL;

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

const SpecialEvent SPECIAL_EVENTS[] = { SPECIAL_EVENTS_INIT };
#define NUM_SPECIAL_EVENTS ((int)(sizeof(SPECIAL_EVENTS) / sizeof(SPECIAL_EVENTS[0])))

// ─── Event detection ──────────────────────────────────────────────────────────

// Source 1: hardcoded table (highest priority, custom display duration)
// Source 2: upcoming milestones from API (works offline, default duration)
static const char *prv_get_special_event(void) {
  uint32_t now = (uint32_t)time(NULL);

  for (int i = 0; i < NUM_SPECIAL_EVENTS; i++) {
    uint32_t start = SPECIAL_EVENTS[i].epoch;
    uint32_t display_secs = (SPECIAL_EVENTS[i].display_minutes == 0)
      ? EVENT_DISPLAY_S
      : ((uint32_t)SPECIAL_EVENTS[i].display_minutes * 60);
    if (now >= start && now < start + display_secs)
      return SPECIAL_EVENTS[i].message;
  }

  for (int i = 0; i < MAX_UPCOMING; i++) {
    uint32_t start = s_artemis.upcoming[i].epoch;
    if (start > 0 && s_artemis.upcoming[i].name[0] != '\0'
        && now >= start && now < start + EVENT_DISPLAY_S)
      return s_artemis.upcoming[i].name;
  }

  return NULL;
}

// ─── Public API ───────────────────────────────────────────────────────────────
void artemis_event_create(Layer *root) {
  int overlay_top, overlay_h;
  overlay_geometry(&overlay_top, &overlay_h);
  s_event_overlay_layer = text_layer_create(
      GRect(4, overlay_top, s_root_w - 8, overlay_h));
  text_layer_set_background_color(s_event_overlay_layer, GColorClear);
  text_layer_set_text_color(s_event_overlay_layer, ARTEMIS_COLOR_ACCENT);
  text_layer_set_font(s_event_overlay_layer, s_font_event);
  text_layer_set_text_alignment(s_event_overlay_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_event_overlay_layer, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(s_event_overlay_layer));
  layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), true);
}

void artemis_event_destroy(void) {
  if (s_event_overlay_layer) {
    text_layer_destroy(s_event_overlay_layer);
    s_event_overlay_layer = NULL;
  }
}

void artemis_event_show(void) {
  if (s_event_overlay_layer)
    layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), false);
}

void artemis_event_hide(void) {
  if (s_event_overlay_layer)
    layer_set_hidden(text_layer_get_layer(s_event_overlay_layer), true);
}

const char *artemis_event_check(void) {
  const char *event_msg = prv_get_special_event();
  // Vibrate only on transition to a new (non-NULL) event
  if (event_msg != s_active_event_msg && event_msg != NULL
      && s_settings.vibrate_events) {
    static const uint32_t segments[] = { 200, 100, 200, 100, 400 };
    VibePattern pat = { .durations = segments, .num_segments = 5 };
    vibes_enqueue_custom_pattern(pat);
  }
  s_active_event_msg = event_msg;
  return event_msg;
}

// Measure msg, reframe so the text block is vertically centred in the top zone.
void artemis_event_update(const char *msg) {
  if (!s_event_overlay_layer || !msg) return;
  ARTEMIS_LOG(APP_LOG_LEVEL_DEBUG, "EVENT! %s", msg);
  int overlay_top, overlay_h;
  overlay_geometry(&overlay_top, &overlay_h);
  int zone_w = s_root_w - 8;
  GSize text_sz = graphics_text_layout_get_content_size(
      msg, s_font_event,
      GRect(0, 0, zone_w, overlay_h),
      GTextOverflowModeWordWrap, GTextAlignmentCenter);
  int centered_y = overlay_top + (overlay_h - text_sz.h) / 2;
  if (centered_y < overlay_top) centered_y = overlay_top;
  layer_set_frame(text_layer_get_layer(s_event_overlay_layer),
      GRect(4, centered_y, zone_w, text_sz.h));
  text_layer_set_text(s_event_overlay_layer, msg);
}
