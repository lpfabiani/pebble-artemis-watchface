/**
 * @file artemis_main.h
 * @brief Always-visible background scene interface: sky, moon, time, and date.
 *
 * Manages the sky background layer (solid black with Orion star field), the
 * moon bitmap in the bottom zone, and the time and date text layers.
 * These layers are always visible regardless of display state.
 *
 * @author LP Fabiani
 * @date 2026
 */
#pragma once
#include <pebble.h>

// Create sky background and bottom zone (moon, time, date).
// Layers are added as children of root in draw order.
void artemis_main_create(Layer *root);

// Destroy all owned layers and bitmap resources.
void artemis_main_destroy(void);

void artemis_main_show(void);
void artemis_main_hide(void);

// Update time and date text. Call every minute from tick_handler.
void artemis_main_refresh(struct tm *tick_time);
