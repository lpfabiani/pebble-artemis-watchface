/**
 * @file artemis_event.h
 * @brief Special event detection and overlay banner interface.
 *
 * Owns @c s_event_overlay_layer and the event detection logic
 * (hardcoded lunar flyby table + dynamic API milestones). Separated from
 * @c artemis_info.c so the event overlay can be managed independently of
 * the info slot layer, and so @c main.c can orchestrate both from a clean
 * state machine.
 *
 * @author LP Fabiani
 * @date 2026
 */
#pragma once
#include <pebble.h>

// ─── Special events ───────────────────────────────────────────────────────────
#define SPECIAL_EVENTS_INIT          
/* // PAST EVENTS - ARTEMIS II:
#define SPECIAL_EVENTS_INIT          \
  { 1775501100UL, "MOON OBS.\nBEGINS",     0 }, \
  { 1775515620UL, "BEHIND\nTHE MOON",      0 }, \
  { 1775516520UL, "CLOSEST\nTO MOON",      0 }, \
  { 1775516700UL, "MAX DIST\nFROM EARTH",  0 }, \
  { 1775518020UL, "SIGNAL\nRESTORED",      0 }, \
  { 1775524800UL, "MOON OBS.\nENDS",       0 }, 
*/

// Create the event overlay text layer as a child of root. Hidden by default.
void artemis_event_create(Layer *root);

// Destroy the event overlay layer.
void artemis_event_destroy(void);

void artemis_event_show(void);
void artemis_event_hide(void);

// Check all event sources for an active event. Vibrates on transition to a
// new event. Returns the message string, or NULL if no event is active.
// Call every display update cycle (main.c — artemis_update_display).
const char *artemis_event_check(void);

// Measure msg, reframe the overlay layer so the text is vertically centred
// in the top zone, and set the text. Must be called before artemis_event_show().
void artemis_event_update(const char *msg);
