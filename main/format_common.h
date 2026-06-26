// Shared, dependency-free formatting + rule/palette primitives for the
// data-driven module system. Compiled into BOTH the firmware (main/) and the
// desktop app (pith-dashboard, which adds ../main to its include path). This is
// the single source of truth that guarantees the app preview and the device
// format values and evaluate color rules byte-identically.
//
// Plain C (static inline) so it is valid in both the C firmware and the C++ app.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---- value format types ----
typedef enum {
    FMT_INT = 0,   // integer, value/scale
    FMT_FIXED1,    // 1 decimal,  value/scale
    FMT_FIXED2,    // 2 decimals, value/scale
    FMT_TIME,      // lap time  m:ss.mmm (value = ms)
    FMT_SECTOR,    // sector    s.mmm    (value = ms)
    FMT_DELTA,     // signed seconds, trimmed: +0, -0.3, +1.234 (value = ms)
    FMT_STRING,    // plain integer (no scaling), for counts/codes
    FMT_TYPE_COUNT
} fmt_type_t;

// ---- color-rule comparison ops ----
typedef enum { OP_LT = 0, OP_LE, OP_EQ, OP_GE, OP_GT, OP_COUNT } rule_op_t;

// ---- shared palette tokens (each side maps these to its own color space) ----
typedef enum {
    PAL_BG = 0, PAL_PANEL, PAL_WHITE, PAL_DIM,
    PAL_GREEN, PAL_AMBER, PAL_RED, PAL_CYAN, PAL_BLUE, PAL_PURPLE,
    PAL_COUNT
} pal_token_t;

static const char *const PAL_NAMES[PAL_COUNT] = {
    "bg", "panel", "white", "dim",
    "green", "amber", "red", "cyan", "blue", "purple",
};
static const char *const FMT_NAMES[FMT_TYPE_COUNT] = {
    "int", "fixed1", "fixed2", "time", "sector", "delta", "string",
};
static const char *const OP_NAMES[OP_COUNT] = { "<", "<=", "==", ">=", ">" };

// ---- string -> id parsers (used by both JSON parsers) ----
static inline int fmtc_pal_from_str(const char *s)
{
    if (s) for (int i = 0; i < PAL_COUNT; i++) if (!strcmp(s, PAL_NAMES[i])) return i;
    return PAL_WHITE;
}
static inline int fmtc_fmt_from_str(const char *s)
{
    if (s) for (int i = 0; i < FMT_TYPE_COUNT; i++) if (!strcmp(s, FMT_NAMES[i])) return i;
    return FMT_INT;
}
static inline int fmtc_op_from_str(const char *s)
{
    if (s) for (int i = 0; i < OP_COUNT; i++) if (!strcmp(s, OP_NAMES[i])) return i;
    return OP_GT;
}

// ---- core: format a raw telemetry int into a display string ----
// `scale` divides the raw value (e.g. fuel_dl scale 10 -> litres). `unit` is the
// suffix appended verbatim (e.g. "L", "%", "KM/H"). Pass unit "" to omit (the
// device passes "" for the degree symbol and draws a ring instead).
static inline void fmtc_format(int v, int fmt_t, int scale, const char *unit,
                               char *o, int n)
{
    if (scale <= 0) scale = 1;
    if (!unit) unit = "";
    switch (fmt_t) {
    case FMT_TIME: {
        if (v <= 0) { snprintf(o, n, "--:--.---"); return; }
        snprintf(o, n, "%d:%02d.%03d", v / 60000, (v / 1000) % 60, v % 1000);
        break;
    }
    case FMT_SECTOR: {
        if (v <= 0) { snprintf(o, n, "--.---"); return; }
        snprintf(o, n, "%d.%03d", v / 1000, v % 1000);
        break;
    }
    case FMT_DELTA: {
        // Signed seconds, ALWAYS 4 decimals, clamped to +/-9.9999. `v` is in 0.1 ms
        // units (10000 = 1.0000 s) so the 4th decimal carries real precision.
        if (v >  99999) v =  99999;
        if (v < -99999) v = -99999;
        char sign = v >= 0 ? '+' : '-';
        int a = v < 0 ? -v : v;
        snprintf(o, n, "%c%d.%04d", sign, a / 10000, a % 10000);
        return;  // delta never carries a unit
    }
    case FMT_FIXED1: {
        int whole = v / scale, frac = (abs(v) * 10 / scale) % 10;
        snprintf(o, n, "%d.%d%s", whole, frac, unit);
        return;
    }
    case FMT_FIXED2: {
        int whole = v / scale, frac = (abs(v) * 100 / scale) % 100;
        snprintf(o, n, "%d.%02d%s", whole, frac, unit);
        return;
    }
    case FMT_INT:
    case FMT_STRING:
    default:
        snprintf(o, n, "%d%s", v / scale, unit);
        return;
    }
    // time/sector: append unit if any (rare)
    if (unit[0]) { size_t l = strlen(o); if ((int)l < n - 1) snprintf(o + l, n - (int)l, "%s", unit); }
}

// ---- core: evaluate one color rule comparison against the raw value ----
static inline int fmtc_rule_match(int v, int op, int rule_v)
{
    switch (op) {
    case OP_LT: return v <  rule_v;
    case OP_LE: return v <= rule_v;
    case OP_EQ: return v == rule_v;
    case OP_GE: return v >= rule_v;
    case OP_GT: return v >  rule_v;
    default:    return 0;
    }
}
