/**
 * @file artemis_event.c
 * @brief Reusable event overlay implementation and event detection.
 *
 * Each ArtemisEventOverlay is heap-allocated and manages one TextLayer.
 * artemis_event_show() merges the former update+show pair: centers the text,
 * sets it, makes the layer visible, and optionally vibrates on text change.
 *
 * artemis_event_check() is stateless — queries s_mission.events (synced from
 * the cloud mission file) and s_artemis.upcoming, returns the active message
 * or NULL.
 *
 * @author LP Fabiani
 * @date 2026
 */
#include "artemis_event.h"
#include "artemis.h"

// ─── Event detection ──────────────────────────────────────────────────────────
#define EVENT_DISPLAY_S  (5 * 60)

static const char *prv_get_special_event(void) {
  uint32_t now = (uint32_t)time(NULL);

  for (int i = 0; i < s_mission.num_events; i++) {
    uint32_t start = s_mission.events[i].epoch;
    uint32_t secs  = (s_mission.events[i].display_minutes == 0)
      ? EVENT_DISPLAY_S
      : ((uint32_t)s_mission.events[i].display_minutes * 60);
    if (start > 0 && now >= start && now < start + secs)
      return s_mission.events[i].message;
  }

  for (int i = 0; i < MAX_UPCOMING; i++) {
    uint32_t start = s_artemis.upcoming[i].epoch;
    if (start > 0 && s_artemis.upcoming[i].name[0] != '\0'
        && now >= start && now < start + EVENT_DISPLAY_S)
      return s_artemis.upcoming[i].name;
  }

  return NULL;
}

const char *artemis_event_check(void) {
  return prv_get_special_event();
}

// ─── Instance lifecycle ───────────────────────────────────────────────────────
ArtemisEventOverlay *artemis_event_create(Layer *parent) {
  ArtemisEventOverlay *ev = malloc(sizeof(ArtemisEventOverlay));
  if (!ev) return NULL;
  ev->last_msg = NULL;

  int overlay_top, overlay_h;
  overlay_geometry(&overlay_top, &overlay_h);
  ev->layer = text_layer_create(
      GRect(4, overlay_top, s_root_w - 8, overlay_h));
  text_layer_set_background_color(ev->layer, GColorClear);
  text_layer_set_text_color(ev->layer, ARTEMIS_COLOR_ACCENT);
  text_layer_set_font(ev->layer, s_font_event);
  text_layer_set_text_alignment(ev->layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(ev->layer, GTextOverflowModeWordWrap);
  layer_add_child(parent, text_layer_get_layer(ev->layer));
  layer_set_hidden(text_layer_get_layer(ev->layer), true);
  return ev;
}

void artemis_event_destroy(ArtemisEventOverlay *ev) {
  if (!ev) return;
  if (ev->layer) {
    text_layer_destroy(ev->layer);
    ev->layer = NULL;
  }
  free(ev);
}

// ─── Display ──────────────────────────────────────────────────────────────────
void artemis_event_show(ArtemisEventOverlay *ev, const char *msg, bool vibrate) {
  if (!ev || !msg) return;

  if (vibrate && msg != ev->last_msg) {
    static const uint32_t segments[] = { 200, 100, 200, 100, 400 };
    VibePattern pat = { .durations = segments, .num_segments = 5 };
    vibes_enqueue_custom_pattern(pat);
  }
  ev->last_msg = msg;

  int overlay_top, overlay_h;
  overlay_geometry(&overlay_top, &overlay_h);
  int zone_w = s_root_w - 8;
  GSize text_sz = graphics_text_layout_get_content_size(
      msg, s_font_event,
      GRect(0, 0, zone_w, overlay_h),
      GTextOverflowModeWordWrap, GTextAlignmentCenter);
  int centered_y = overlay_top + (overlay_h - text_sz.h) / 2;
  if (centered_y < overlay_top) centered_y = overlay_top;
  layer_set_frame(text_layer_get_layer(ev->layer),
      GRect(4, centered_y, zone_w, text_sz.h));
  text_layer_set_text(ev->layer, msg);
  layer_set_hidden(text_layer_get_layer(ev->layer), false);
}

void artemis_event_hide(ArtemisEventOverlay *ev) {
  if (ev && ev->layer)
    layer_set_hidden(text_layer_get_layer(ev->layer), true);
}
