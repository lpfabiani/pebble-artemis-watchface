/**
 * @file artemis_comms.h
 * @brief Phone–watch AppMessage communication interface.
 *
 * Provides the two-function API for initialising AppMessage and requesting
 * a data refresh from the companion JS app. Actual inbox/outbox handling
 * is private to artemis_comms.c.
 *
 * @author LP Fabiani
 * @date 2026
 */
#pragma once

// Register AppMessage callbacks and open the inbox.
// Call once from init().
void artemis_comms_init(void);

// Send a data-request message to the companion phone app.
void artemis_comms_request_data(void);
