/**
 * @file artemis_comms.c
 * @brief Phone–watch AppMessage communication implementation.
 *
 * Handles the AppMessage inbox: parses incoming telemetry fields and Clay
 * configuration keys, persists updated state, and triggers display refresh
 * via @c artemis_update_display() and slot rebuild via
 * @c artemis_info_rebuild_slots(). Also provides @c artemis_comms_request_data()
 * to poll the companion JS app on demand.
 *
 * @author LP Fabiani
 * @date 2026
 */
#include "artemis_comms.h"
#include "artemis.h"
#include "artemis_info.h"

// ─── Key tables (runtime externs — populated once in artemis_comms_init) ─────
static uint32_t s_name_keys[MAX_UPCOMING];
static uint32_t s_epoch_keys[MAX_UPCOMING];
static uint32_t s_slot_keys[MAX_SLOTS];

// ─── AppMessage helpers ───────────────────────────────────────────────────────
// Clay sends select values as cstrings (e.g. "1", "30"). Telemetry arrives as
// integers. These helpers handle both so call sites stay clean.

static int32_t prv_fetch_int(Tuple *t) {
  if (!t) return 0;

  if (t->type == TUPLE_CSTRING)
    return atoi(t->value->cstring);
  // if (t->type == TUPLE_INT)
  // Per definition, all the expected values will be unsigned.
  if (t->length == 1)
    return (uint8_t) t->value->uint8;
  if (t->length == 2)
    return (uint16_t) t->value->uint16;
  if (t->length == 4)
    return (uint32_t) t->value->uint32;
  return 0;
}

// ─── AppMessage ───────────────────────────────────────────────────────────────
static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  bool data_changed = false, cfg_changed = false;
  Tuple *t;

  // ── Phase 1: Parse ───────────────────────────────────────────────────────────

#define FETCH_STR(key, dst) \
  if ((t = dict_find(iter, MESSAGE_KEY_##key)) && t->type == TUPLE_CSTRING) { \
    strncpy(dst, t->value->cstring, sizeof(dst) - 1); \
    dst[sizeof(dst) - 1] = '\0'; data_changed = true; }
#define FETCH_I32(key, dst) \
  if ((t = dict_find(iter, MESSAGE_KEY_##key))) { dst = prv_fetch_int(t); data_changed = true; }

  FETCH_STR(ARTEMIS_PHASE,          s_artemis.phase)
  FETCH_I32(ARTEMIS_SPEED,          s_artemis.speed_x100)
  FETCH_I32(ARTEMIS_DISTANCE,       s_artemis.distance_km)
  FETCH_I32(ARTEMIS_MOON_DIST,      s_artemis.moon_distance_km)
  FETCH_STR(ARTEMIS_MILESTONE_NAME, s_artemis.milestone_name)
  FETCH_I32(ARTEMIS_MILESTONE_MET,  s_artemis.milestone_met_ms)
  FETCH_I32(ARTEMIS_G_FORCE,        s_artemis.g_force_x10000)
  FETCH_I32(ARTEMIS_ALTITUDE,       s_artemis.altitude_km)
  FETCH_I32(ARTEMIS_PERIAPSIS,      s_artemis.periapsis_km)
  FETCH_I32(ARTEMIS_APOAPSIS,       s_artemis.apoapsis_km)
  FETCH_I32(ARTEMIS_SIGNAL,         s_artemis.signal_x100)
  FETCH_STR(ARTEMIS_STATION,        s_artemis.dsn_station)
  FETCH_I32(ARTEMIS_DOWNLINK,       s_artemis.downlink_kbps)

#undef FETCH_STR
#undef FETCH_I32

  for (int i = 0; i < MAX_UPCOMING; i++) {
    if ((t = dict_find(iter, s_name_keys[i]))) {
      strncpy(s_artemis.upcoming[i].name, t->value->cstring,
              sizeof(s_artemis.upcoming[i].name) - 1);
      s_artemis.upcoming[i].name[sizeof(s_artemis.upcoming[i].name) - 1] = '\0';
      data_changed = true;
    }
    if ((t = dict_find(iter, s_epoch_keys[i]))) {
      s_artemis.upcoming[i].epoch = (uint32_t)t->value->int32;
      data_changed = true;
    }
  }

  if ((t = dict_find(iter, MESSAGE_KEY_ARTEMIS_COMPLETE))) {
    s_artemis.mission_complete = (t->value->uint8 == 1); data_changed = true;
  }

  if ((t = dict_find(iter, MESSAGE_KEY_UPDATE_INTERVAL))) {
    s_settings.update_interval_min = prv_fetch_int(t); cfg_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_USE_MILES))) {
    s_settings.use_miles = (t->value->uint8 != 0); cfg_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_VIBRATE_EVENTS))) {
    s_settings.vibrate_events = (t->value->uint8 != 0); cfg_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_INFO_ON_SHAKE))) {
    s_settings.info_on_shake = (prv_fetch_int(t) != 0); cfg_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_INFO_DISPLAY_S))) {
    int v = (int)prv_fetch_int(t);
    s_settings.info_display_s = (uint8_t)((v >= 5 && v <= 60) ? v : 10);
    cfg_changed = true;
  }

  for (int i = 0; i < MAX_SLOTS; i++) {
    if ((t = dict_find(iter, s_slot_keys[i]))) {
      s_settings.slots[i] = (uint8_t)prv_fetch_int(t); cfg_changed = true;
    }
  }

  // ── Phase 2: Commit ──────────────────────────────────────────────────────────

  if (data_changed) {
    s_artemis.last_update_epoch = (uint32_t)time(NULL);
    persist_write_data(ARTEMIS_KEY, &s_artemis, sizeof(s_artemis));
  }
  if (cfg_changed) {
    persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
    artemis_info_rebuild_slots();
    artemis_apply_shake_setting();
  }

  // ── Phase 3: Render ──────────────────────────────────────────────────────────

  if (data_changed || cfg_changed)
    artemis_update_display();
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  ARTEMIS_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

// ─── Public API ───────────────────────────────────────────────────────────────
void artemis_comms_init(void) {
  s_name_keys[0]  = MESSAGE_KEY_ARTEMIS_MS0_NAME;
  s_name_keys[1]  = MESSAGE_KEY_ARTEMIS_MS1_NAME;
  s_name_keys[2]  = MESSAGE_KEY_ARTEMIS_MS2_NAME;
  s_name_keys[3]  = MESSAGE_KEY_ARTEMIS_MS3_NAME;
  s_name_keys[4]  = MESSAGE_KEY_ARTEMIS_MS4_NAME;
  s_epoch_keys[0] = MESSAGE_KEY_ARTEMIS_MS0_EPOCH;
  s_epoch_keys[1] = MESSAGE_KEY_ARTEMIS_MS1_EPOCH;
  s_epoch_keys[2] = MESSAGE_KEY_ARTEMIS_MS2_EPOCH;
  s_epoch_keys[3] = MESSAGE_KEY_ARTEMIS_MS3_EPOCH;
  s_epoch_keys[4] = MESSAGE_KEY_ARTEMIS_MS4_EPOCH;
  s_slot_keys[0]  = MESSAGE_KEY_SLOT_1;
  s_slot_keys[1]  = MESSAGE_KEY_SLOT_2;
  s_slot_keys[2]  = MESSAGE_KEY_SLOT_3;
  s_slot_keys[3]  = MESSAGE_KEY_SLOT_4;
  s_slot_keys[4]  = MESSAGE_KEY_SLOT_5;
  s_slot_keys[5]  = MESSAGE_KEY_SLOT_6;

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_open(768, 64);
}

// ─── Request data from phone ──────────────────────────────────────────────────
void artemis_comms_request_data(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_ARTEMIS, 1);
    app_message_outbox_send();
  }
}

