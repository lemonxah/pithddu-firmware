// AUTO-GENERATED from main/field_registry.json by tools/gen_field_registry.py.
// Do not edit by hand.
#pragma once
#include "simhub_proto.h"
#include "format_common.h"
#include <string.h>

typedef struct { const char *name; uint8_t fmt; int16_t scale; const char *label; } field_def_t;

enum {
    FIELD_NONE = 0,
    FIELD_SPEED_KMH,
    FIELD_RPM,
    FIELD_MAX_RPM,
    FIELD_SHIFT_RPM,
    FIELD_CUR_LAP_MS,
    FIELD_LAST_LAP_MS,
    FIELD_BEST_LAP_MS,
    FIELD_PB_LAP_MS,
    FIELD_EST_LAP_MS,
    FIELD_DELTA_MS,
    FIELD_POSITION,
    FIELD_FIELD_SIZE,
    FIELD_LAPS_DONE,
    FIELD_TOTAL_LAPS,
    FIELD_LAPS_LEFT,
    FIELD_WATER_C,
    FIELD_OIL_C,
    FIELD_OIL_PRESS_X10,
    FIELD_BOOST_KPA,
    FIELD_TC,
    FIELD_ABS,
    FIELD_BRAKE_BIAS_X10,
    FIELD_FUEL_DL,
    FIELD_FUEL_CAP_DL,
    FIELD_FUEL_PER_LAP_ML,
    FIELD_FUEL_LAPS_X10,
    FIELD_TT_FL_M,
    FIELD_TT_FR_M,
    FIELD_TT_RL_M,
    FIELD_TT_RR_M,
    FIELD_TP_FL,
    FIELD_TP_FR,
    FIELD_TP_RL,
    FIELD_TP_RR,
    FIELD_TW_FL,
    FIELD_TW_FR,
    FIELD_TW_RL,
    FIELD_TW_RR,
    FIELD_BT_FL,
    FIELD_BT_FR,
    FIELD_BT_RL,
    FIELD_BT_RR,
    FIELD_THROTTLE,
    FIELD_BRAKE,
    FIELD_CLUTCH,
    FIELD_STEER,
    FIELD_TC_ACTIVE,
    FIELD_ABS_ACTIVE,
    FIELD_S1_MS,
    FIELD_S2_MS,
    FIELD_S3_MS,
    FIELD_HEADLIGHTS,
    FIELD_WIPERS,
    FIELD_PIT_LIMITER,
    FIELD_IGNITION,
    FIELD_COUNT
};

static const field_def_t FIELD_REGISTRY[FIELD_COUNT] = {
    { "", FMT_INT, 1, "" },
    { "speed_kmh", FMT_INT, 1, "KM/H" },
    { "rpm", FMT_INT, 1, "RPM" },
    { "max_rpm", FMT_INT, 1, "MAX RPM" },
    { "shift_rpm", FMT_INT, 1, "SHIFT" },
    { "cur_lap_ms", FMT_TIME, 1, "CURRENT" },
    { "last_lap_ms", FMT_TIME, 1, "LAST" },
    { "best_lap_ms", FMT_TIME, 1, "BEST" },
    { "pb_lap_ms", FMT_TIME, 1, "PB" },
    { "est_lap_ms", FMT_TIME, 1, "EST" },
    { "delta_ms", FMT_DELTA, 1, "DELTA" },
    { "position", FMT_INT, 1, "POS" },
    { "field_size", FMT_INT, 1, "FIELD" },
    { "laps_done", FMT_INT, 1, "LAP" },
    { "total_laps", FMT_INT, 1, "LAPS" },
    { "laps_left", FMT_INT, 1, "LEFT" },
    { "water_c", FMT_INT, 1, "H2O" },
    { "oil_c", FMT_INT, 1, "OIL" },
    { "oil_press_x10", FMT_FIXED1, 10, "OIL P" },
    { "boost_kpa", FMT_INT, 1, "BOOST" },
    { "tc", FMT_INT, 1, "TC" },
    { "abs", FMT_INT, 1, "ABS" },
    { "brake_bias_x10", FMT_FIXED1, 10, "BIAS" },
    { "fuel_dl", FMT_FIXED1, 10, "FUEL" },
    { "fuel_cap_dl", FMT_FIXED1, 10, "TANK" },
    { "fuel_per_lap_ml", FMT_FIXED1, 1000, "FUEL/LAP" },
    { "fuel_laps_x10", FMT_FIXED1, 10, "FUEL LAPS" },
    { "tt_fl_m", FMT_INT, 1, "FL" },
    { "tt_fr_m", FMT_INT, 1, "FR" },
    { "tt_rl_m", FMT_INT, 1, "RL" },
    { "tt_rr_m", FMT_INT, 1, "RR" },
    { "tp_fl", FMT_INT, 1, "P FL" },
    { "tp_fr", FMT_INT, 1, "P FR" },
    { "tp_rl", FMT_INT, 1, "P RL" },
    { "tp_rr", FMT_INT, 1, "P RR" },
    { "tw_fl", FMT_INT, 1, "W FL" },
    { "tw_fr", FMT_INT, 1, "W FR" },
    { "tw_rl", FMT_INT, 1, "W RL" },
    { "tw_rr", FMT_INT, 1, "W RR" },
    { "bt_fl", FMT_INT, 1, "B FL" },
    { "bt_fr", FMT_INT, 1, "B FR" },
    { "bt_rl", FMT_INT, 1, "B RL" },
    { "bt_rr", FMT_INT, 1, "B RR" },
    { "throttle", FMT_INT, 1, "THR" },
    { "brake", FMT_INT, 1, "BRK" },
    { "clutch", FMT_INT, 1, "CLU" },
    { "steer", FMT_INT, 1, "STEER" },
    { "tc_active", FMT_INT, 1, "TC ACT" },
    { "abs_active", FMT_INT, 1, "ABS ACT" },
    { "s1_ms", FMT_SECTOR, 1, "S1" },
    { "s2_ms", FMT_SECTOR, 1, "S2" },
    { "s3_ms", FMT_SECTOR, 1, "S3" },
    { "headlights", FMT_INT, 1, "LIGHTS" },
    { "wipers", FMT_INT, 1, "WIPERS" },
    { "pit_limiter", FMT_INT, 1, "PIT LIM" },
    { "ignition", FMT_INT, 1, "IGN" },
};

static inline uint8_t field_id_from_str(const char *s) {
    if (!s) return FIELD_NONE;
    for (int i = 1; i < FIELD_COUNT; i++)
        if (!strcmp(s, FIELD_REGISTRY[i].name)) return (uint8_t)i;
    return FIELD_NONE;
}

// Resolve a field id to its current raw integer value from telemetry.
static inline int field_value(const simhub_telemetry_t *t, uint8_t id) {
    switch (id) {
    case FIELD_SPEED_KMH: return t->speed_kmh;
    case FIELD_RPM: return t->rpm;
    case FIELD_MAX_RPM: return t->max_rpm;
    case FIELD_SHIFT_RPM: return t->shift_rpm;
    case FIELD_CUR_LAP_MS: return t->cur_lap_ms;
    case FIELD_LAST_LAP_MS: return t->last_lap_ms;
    case FIELD_BEST_LAP_MS: return t->best_lap_ms;
    case FIELD_PB_LAP_MS: return t->pb_lap_ms;
    case FIELD_EST_LAP_MS: return t->est_lap_ms;
    case FIELD_DELTA_MS: return t->delta_ms;
    case FIELD_POSITION: return t->position;
    case FIELD_FIELD_SIZE: return t->field_size;
    case FIELD_LAPS_DONE: return t->laps_done;
    case FIELD_TOTAL_LAPS: return t->total_laps;
    case FIELD_LAPS_LEFT: return t->laps_left;
    case FIELD_WATER_C: return t->water_c;
    case FIELD_OIL_C: return t->oil_c;
    case FIELD_OIL_PRESS_X10: return t->oil_press_x10;
    case FIELD_BOOST_KPA: return t->boost_kpa;
    case FIELD_TC: return t->tc;
    case FIELD_ABS: return t->abs;
    case FIELD_BRAKE_BIAS_X10: return t->brake_bias_x10;
    case FIELD_FUEL_DL: return t->fuel_dl;
    case FIELD_FUEL_CAP_DL: return t->fuel_cap_dl;
    case FIELD_FUEL_PER_LAP_ML: return t->fuel_per_lap_ml;
    case FIELD_FUEL_LAPS_X10: return t->fuel_laps_x10;
    case FIELD_TT_FL_M: return t->tt_fl_m;
    case FIELD_TT_FR_M: return t->tt_fr_m;
    case FIELD_TT_RL_M: return t->tt_rl_m;
    case FIELD_TT_RR_M: return t->tt_rr_m;
    case FIELD_TP_FL: return t->tp_fl;
    case FIELD_TP_FR: return t->tp_fr;
    case FIELD_TP_RL: return t->tp_rl;
    case FIELD_TP_RR: return t->tp_rr;
    case FIELD_TW_FL: return t->tw_fl;
    case FIELD_TW_FR: return t->tw_fr;
    case FIELD_TW_RL: return t->tw_rl;
    case FIELD_TW_RR: return t->tw_rr;
    case FIELD_BT_FL: return t->bt_fl;
    case FIELD_BT_FR: return t->bt_fr;
    case FIELD_BT_RL: return t->bt_rl;
    case FIELD_BT_RR: return t->bt_rr;
    case FIELD_THROTTLE: return t->throttle;
    case FIELD_BRAKE: return t->brake;
    case FIELD_CLUTCH: return t->clutch;
    case FIELD_STEER: return t->steer;
    case FIELD_TC_ACTIVE: return t->tc_active;
    case FIELD_ABS_ACTIVE: return t->abs_active;
    case FIELD_S1_MS: return t->s1_ms;
    case FIELD_S2_MS: return t->s2_ms;
    case FIELD_S3_MS: return t->s3_ms;
    case FIELD_HEADLIGHTS: return t->headlights;
    case FIELD_WIPERS: return t->wipers;
    case FIELD_PIT_LIMITER: return t->pit_limiter;
    case FIELD_IGNITION: return t->ignition;
    default: return 0;
    }
}
