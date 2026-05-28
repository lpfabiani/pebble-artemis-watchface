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

// -- Never used
// Mark the logo layer dirty (redraws PDC on next render cycle).
//void artemis_logo_refresh(void);
