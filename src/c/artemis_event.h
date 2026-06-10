/**
 * @file artemis_event.h
 * @brief Reusable event overlay: instance-based centered text banner.
 *
 * ArtemisEventOverlay wraps a TextLayer and handles vertical centering in the
 * top zone. Callers create one instance per display context:
 *   - main.c         → NASA mission events (highest-priority banner)
 *   - artemis_info.c → phase text (pre-launch T-minus, post-mission stats)
 *
 * artemis_event_check() is a free function that queries event data sources
 * (s_mission.events synced from the cloud + API milestones) and is
 * independent of any instance.
 *
 * @author LP Fabiani
 * @date 2026
 */
#pragma once
#include <pebble.h>

// ─── Event overlay instance ───────────────────────────────────────────────────
typedef struct {
  TextLayer  *layer;
  const char *last_msg;  // transition tracking for artemis_event_show vibration
} ArtemisEventOverlay;

// Allocate an overlay as a child of parent. Hidden by default.
ArtemisEventOverlay *artemis_event_create(Layer *parent);

// Destroy the overlay and free the struct.
void artemis_event_destroy(ArtemisEventOverlay *ev);

// Center msg vertically in the top zone, set text, show. Vibrates if msg
// changed since the last call and vibrate is true. No-op if ev or msg is NULL.
void artemis_event_show(ArtemisEventOverlay *ev, const char *msg, bool vibrate);

// Hide the overlay.
void artemis_event_hide(ArtemisEventOverlay *ev);

// ─── Event detection (free function) ─────────────────────────────────────────
// Query s_mission.events (cloud-synced) + upcoming milestones from API.
// Returns the active event message, or NULL. Pure — no side effects.
const char *artemis_event_check(void);
