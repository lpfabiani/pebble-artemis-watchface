/**
 * @file artemis_logo.h
 * @brief Mission-complete Artemis 'A' logo interface.
 *
 * Owns the PDC vector logo layer shown in the top zone when the mission
 * is complete and no special event is active. The logo is scaled to fill
 * the top zone and overflow slightly into the moon zone so the base arc
 * of the 'A' sits naturally over the moon image.
 *
 * @author LP Fabiani
 * @date 2026
 */
#pragma once
#include <pebble.h>

// Create and add the logo layer to root. Layer is hidden by default.
void artemis_logo_create(Layer *root);

// Destroy the logo layer and release the PDC resource.
void artemis_logo_destroy(void);

void artemis_logo_show(void);
void artemis_logo_hide(void);

// Recompute the logo's size so it doesn't overlap the moon/time/date zone as
// Timeline Peek moves it up. Pass the current unobstructed height (same value
// passed to artemis_clock_peek()). Hides the logo if the available space
// becomes too small to render legibly; restores it once space returns.
// Safe to call every animation frame — it no-ops if the logo isn't currently
// shown for reasons unrelated to peek.
void artemis_logo_peek(int unobstructed_h);
