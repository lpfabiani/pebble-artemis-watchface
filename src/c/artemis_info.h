/**
 * @file artemis_info.h
 * @brief Top-zone info slot display interface.
 *
 * Manages @c s_info_layer (container for field label/value layers and the
 * decorations layer). Visibility is controlled by @c main.c via
 * @c artemis_info_show() / @c artemis_info_hide(). Content is updated by
 * @c artemis_info_refresh(), which is called only when the info layer is
 * visible. Event detection and the event overlay are handled separately by
 * @c artemis_event.c.
 *
 * @author LP Fabiani
 * @date 2026
 */
#pragma once
#include <pebble.h>

// Create s_info_layer, slot layers, and decorations as children of root.
void artemis_info_create(Layer *root);

// Destroy all owned layers.
void artemis_info_destroy(void);

// Show or hide the info container (s_info_layer and all field layers).
void artemis_info_show(void);
void artemis_info_hide(void);

// Update slot content (MET, telemetry values). Does not change visibility.
// Call only when the info layer is visible (DISPLAY_INFO mode).
void artemis_info_refresh(void);

// Destroy and recreate slot layers from current s_settings.
// Called by artemis_comms when slot configuration changes.
void artemis_info_rebuild_slots(void);
