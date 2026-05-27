/**
 * @file artemis_info.h
 * @brief Top-zone info display interface.
 *
 * Manages the @c s_info_layer container (field labels and values, decorations)
 * and the independent @c s_event_overlay_layer. The key entry point is
 * @c artemis_info_refresh(), which evaluates display priority (special event →
 * mission fields → mission complete) and returns @c true when the top zone is
 * occupied, allowing main.c to decide logo visibility.
 *
 * @author LP Fabiani
 * @date 2026
 */
#pragma once
#include <pebble.h>

// Create s_info_layer, slot layers, decorations, and event overlay.
// s_info_layer and event overlay are added as children of root.
void artemis_info_create(Layer *root);

// Destroy all owned layers.
void artemis_info_destroy(void);

// Show or hide the entire info container (s_info_layer).
// Does not affect the event overlay (managed internally by artemis_info_refresh).
void artemis_info_show(void);
void artemis_info_hide(void);

// Evaluate display priority and update the top zone:
//   - Special event active  → show event overlay, hide fields, return true
//   - Mission not complete  → hide event overlay, show fields, return true
//   - Mission complete      → hide everything,                 return false
// Caller uses the return value to decide whether to show the logo.
bool artemis_info_refresh(void);

// Destroy and recreate slot layers from current s_settings.
// Called by artemis_comms when slot configuration changes.
void artemis_info_rebuild_slots(void);
