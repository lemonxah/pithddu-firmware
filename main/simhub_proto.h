// SimHub Custom Serial protocol parser (pure, host-testable) — 5-page frame.
//
// Wire format, one frame per line terminated by '\n' (trailing '\r' tolerated).
// Drives the 5-page HMI: RACE / TIRES / BRAKES / FUEL / INPUTS.
//
//   $g;speed;rpm;maxRpm;shiftRpm;
//     curLap;lastLap;bestLap;pbLap;estLap;delta;
//     pos;field;lap;totalLaps;lapsLeft;
//     water;oil;oilP;boost;tc;abs;bias;
//     fuel;fuelCap;fuelPerLap;fuelLaps;
//     ttFLi;ttFLm;ttFLo;ttFRi;ttFRm;ttFRo;ttRLi;ttRLm;ttRLo;ttRRi;ttRRm;ttRRo;
//     tpFL;tpFR;tpRL;tpRR;twFL;twFR;twRL;twRR;
//     btFL;btFR;btRL;btRR;
//     thr;brk;clu;steer
//
// - leading '$' is a resync sentinel; bytes before it on a line are ignored
// - the first 4 fields are REQUIRED; all later fields are OPTIONAL and default
//   to 0 (tyre wear defaults to 100%), so short frames still parse
// - times are integer milliseconds; delta is SIGNED (negative = faster)
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Core
    char gear;            // 'R', 'N', or '1'..'9'
    int  speed_kmh;
    int  rpm;
    int  max_rpm;
    int  shift_rpm;       // RPM where the strip goes full/cyan (shift point)
    // Timing (ms)
    int  cur_lap_ms;
    int  last_lap_ms;
    int  best_lap_ms;     // session best   -> lap_sb (cyan)
    int  pb_lap_ms;       // personal best  -> lap_pb (purple)
    int  est_lap_ms;      // predicted lap  -> lap_est (amber)
    int  delta_ms;        // signed delta vs best, in 0.1ms units (10000 = 1.0000s);
                          // FMT_DELTA shows 4 decimals, clamped to +/-9.9999. (neg = faster)
    // Race
    int  position;
    int  field_size;      // total entries (OpponentsCount+1)
    int  laps_done;       // current lap number
    int  total_laps;
    int  laps_left;       // race laps remaining
    // Engine / car
    int  water_c;
    int  oil_c;
    int  oil_press_x10;   // oil pressure, bar x10 (35 -> 3.5 bar)
    int  boost_kpa;
    int  tc;
    int  abs;
    int  brake_bias_x10;  // % x10
    // Fuel
    int  fuel_dl;          // fuel x10 (deciliters): 65 -> 6.5 L
    int  fuel_cap_dl;      // tank capacity x10
    int  fuel_per_lap_ml;  // ml per lap
    int  fuel_laps_x10;    // laps of fuel remaining x10
    // Tyres: 3-zone temps per corner (C) — inner / middle / outer
    int  tt_fl_i, tt_fl_m, tt_fl_o;
    int  tt_fr_i, tt_fr_m, tt_fr_o;
    int  tt_rl_i, tt_rl_m, tt_rl_o;
    int  tt_rr_i, tt_rr_m, tt_rr_o;
    // Tyre pressures (kPa) and wear (% remaining)
    int  tp_fl, tp_fr, tp_rl, tp_rr;
    int  tw_fl, tw_fr, tw_rl, tw_rr;
    // Brakes (C)
    int  bt_fl, bt_fr, bt_rl, bt_rr;
    // Inputs
    int  throttle;        // 0..100
    int  brake;           // 0..100
    int  clutch;          // 0..100
    int  steer;           // -100..100 (0 = straight)
    // Aids engagement (live) — drives the side LEDs
    int  tc_active;       // 0 = off, >0 = TC cutting power now (0..100 intensity)
    int  abs_active;      // 0 = off, >0 = ABS modulating now
    // Car control on/off states — drive button-box toggle sync (0 = off, >0 = on)
    int  headlights;     // 0 off, 1 low, 2 high (game-dependent)
    int  wipers;         // 0 off, >0 = on/speed
    int  pit_limiter;    // 0 off, 1 on
    int  ignition;       // 0 off, 1 on
    // World position for the self-learned track map (metres x100; cm precision)
    int  pos_x;
    int  pos_z;
    // Sector times (ms): this/last lap, then personal-best sectors for coloring
    int  s1_ms, s2_ms, s3_ms;
    int  bs1_ms, bs2_ms, bs3_ms;
} simhub_telemetry_t;

// Parse a single NUL-terminated frame line into `out`.
// Returns true on a well-formed frame (first 4 fields valid), false otherwise
// (out is untouched on failure). Missing trailing fields keep their defaults.
bool simhub_parse_line(const char *line, simhub_telemetry_t *out);

#ifdef __cplusplus
}
#endif
