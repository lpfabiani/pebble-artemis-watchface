/**
 * @file artemis_clock.h
 * @brief Always-visible background scene interface: sky, moon, time, and date.
 *
 * Manages the sky background layer (solid black with Orion star field) and
 * @c s_time_area_layer — a container for the moon bitmap and time/date text.
 * The container allows the bottom zone to be moved, hidden, or compressed
 * with a single @c layer_set_frame / @c layer_set_hidden call.
 *
 * @author LP Fabiani
 * @date 2026
 */
#pragma once
#include <pebble.h>

// Create sky background and bottom zone (s_time_area_layer: moon, time, date).
// Layers are added as children of root in draw order.
void artemis_clock_create(Layer *root);

// Destroy all owned layers and bitmap resources.
void artemis_clock_destroy(void);

// Show or hide only the bottom zone (moon + time + date).
// Used by main.c when switching to DISPLAY_INFO mode.
void artemis_clock_show(void);
void artemis_clock_hide(void);

// Compress the bottom zone to fit within unobstructed_h pixels (Timeline Peek).
// Reframes s_time_area_layer and re-centres the time+date block inside it.
// Pass s_root_h to restore the normal (full-screen) layout.
void artemis_clock_peek(int unobstructed_h);

// Update time and date text. Call every minute from tick_handler.
void artemis_clock_refresh(struct tm *tick_time);
