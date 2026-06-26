#include "simhub_proto.h"

#include <stddef.h>
#include <ctype.h>

// Parse a non-negative integer at *pp, advancing past the digits.
// Returns -1 if no digit is present.
static int parse_uint(const char **pp)
{
    const char *p = *pp;
    if (!isdigit((unsigned char)*p)) {
        return -1;
    }
    int v = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        p++;
    }
    *pp = p;
    return v;
}

// Optional signed int: if a (possibly signed) number is present at *pp, store
// it in *out and advance; if the field is empty, leave *out untouched.
static void parse_int_opt(const char **pp, int *out)
{
    const char *q = *pp;
    int sign = 1;
    if (*q == '-' || *q == '+') {
        if (*q == '-') sign = -1;
        q++;
    }
    if (!isdigit((unsigned char)*q)) {
        return;  // empty field -> keep default, do not consume the sign
    }
    int v = 0;
    while (isdigit((unsigned char)*q)) {
        v = v * 10 + (*q - '0');
        q++;
    }
    *out = sign * v;
    *pp = q;
}

static bool expect_sep(const char **pp)
{
    if (**pp != ';') return false;
    (*pp)++;
    return true;
}

// If the next char is ';', consume it and parse an optional signed int into
// *field. Returns false once there are no more separators.
static bool opt_field(const char **pp, int *field)
{
    if (**pp != ';') return false;
    (*pp)++;
    parse_int_opt(pp, field);
    return true;
}

bool simhub_parse_line(const char *line, simhub_telemetry_t *out)
{
    if (line == NULL || out == NULL) {
        return false;
    }

    // Resync: skip to the sentinel.
    const char *p = line;
    while (*p && *p != '$') p++;
    if (*p != '$') return false;
    p++;  // consume '$'

    simhub_telemetry_t t = (simhub_telemetry_t){0};
    // Default tyre wear to "full" so an absent field doesn't read as worn out.
    t.tw_fl = t.tw_fr = t.tw_rl = t.tw_rr = 100;

    // ---- Required core fields ----
    // Gear may arrive as a letter (N/R/1..9) or numeric (0=neutral, -1=reverse,
    // 1..9 = gears), depending on the SimHub property. Accept both.
    char g = *p;
    if (g == '-') {                 // "-1" reverse
        t.gear = 'R';
        p++;
        while (isdigit((unsigned char)*p)) p++;
    } else if (g == '0') {          // numeric neutral
        t.gear = 'N';
        p++;
    } else if (g == 'R' || g == 'N') {
        t.gear = g;
        p++;
    } else if (g >= '1' && g <= '9') {
        t.gear = g;
        p++;
    } else {
        return false;
    }

    if (!expect_sep(&p)) return false;
    if ((t.speed_kmh = parse_uint(&p)) < 0) return false;
    if (!expect_sep(&p)) return false;
    if ((t.rpm = parse_uint(&p)) < 0) return false;
    if (!expect_sep(&p)) return false;
    if ((t.max_rpm = parse_uint(&p)) < 0) return false;

    // ---- Optional extended fields, in fixed order ----
    // Stops at the first absent separator; remaining fields keep defaults.
    do {
        if (!opt_field(&p, &t.shift_rpm))      break;
        if (!opt_field(&p, &t.cur_lap_ms))     break;
        if (!opt_field(&p, &t.last_lap_ms))    break;
        if (!opt_field(&p, &t.best_lap_ms))    break;
        if (!opt_field(&p, &t.pb_lap_ms))      break;
        if (!opt_field(&p, &t.est_lap_ms))     break;
        if (!opt_field(&p, &t.delta_ms))       break;
        if (!opt_field(&p, &t.position))       break;
        if (!opt_field(&p, &t.field_size))     break;
        if (!opt_field(&p, &t.laps_done))      break;
        if (!opt_field(&p, &t.total_laps))     break;
        if (!opt_field(&p, &t.laps_left))      break;
        if (!opt_field(&p, &t.water_c))        break;
        if (!opt_field(&p, &t.oil_c))          break;
        if (!opt_field(&p, &t.oil_press_x10))  break;
        if (!opt_field(&p, &t.boost_kpa))      break;
        if (!opt_field(&p, &t.tc))             break;
        if (!opt_field(&p, &t.abs))            break;
        if (!opt_field(&p, &t.brake_bias_x10)) break;
        if (!opt_field(&p, &t.fuel_dl))        break;
        if (!opt_field(&p, &t.fuel_cap_dl))    break;
        if (!opt_field(&p, &t.fuel_per_lap_ml))break;
        if (!opt_field(&p, &t.fuel_laps_x10))  break;
        if (!opt_field(&p, &t.tt_fl_i))        break;
        if (!opt_field(&p, &t.tt_fl_m))        break;
        if (!opt_field(&p, &t.tt_fl_o))        break;
        if (!opt_field(&p, &t.tt_fr_i))        break;
        if (!opt_field(&p, &t.tt_fr_m))        break;
        if (!opt_field(&p, &t.tt_fr_o))        break;
        if (!opt_field(&p, &t.tt_rl_i))        break;
        if (!opt_field(&p, &t.tt_rl_m))        break;
        if (!opt_field(&p, &t.tt_rl_o))        break;
        if (!opt_field(&p, &t.tt_rr_i))        break;
        if (!opt_field(&p, &t.tt_rr_m))        break;
        if (!opt_field(&p, &t.tt_rr_o))        break;
        if (!opt_field(&p, &t.tp_fl))          break;
        if (!opt_field(&p, &t.tp_fr))          break;
        if (!opt_field(&p, &t.tp_rl))          break;
        if (!opt_field(&p, &t.tp_rr))          break;
        if (!opt_field(&p, &t.tw_fl))          break;
        if (!opt_field(&p, &t.tw_fr))          break;
        if (!opt_field(&p, &t.tw_rl))          break;
        if (!opt_field(&p, &t.tw_rr))          break;
        if (!opt_field(&p, &t.bt_fl))          break;
        if (!opt_field(&p, &t.bt_fr))          break;
        if (!opt_field(&p, &t.bt_rl))          break;
        if (!opt_field(&p, &t.bt_rr))          break;
        if (!opt_field(&p, &t.throttle))       break;
        if (!opt_field(&p, &t.brake))          break;
        if (!opt_field(&p, &t.clutch))         break;
        if (!opt_field(&p, &t.steer))          break;
        if (!opt_field(&p, &t.tc_active))      break;
        if (!opt_field(&p, &t.abs_active))     break;
        if (!opt_field(&p, &t.headlights))     break;
        if (!opt_field(&p, &t.wipers))         break;
        if (!opt_field(&p, &t.pit_limiter))    break;
        if (!opt_field(&p, &t.ignition))       break;
        if (!opt_field(&p, &t.pos_x))          break;
        if (!opt_field(&p, &t.pos_z))          break;
        if (!opt_field(&p, &t.s1_ms))          break;
        if (!opt_field(&p, &t.s2_ms))          break;
        if (!opt_field(&p, &t.s3_ms))          break;
        if (!opt_field(&p, &t.bs1_ms))         break;
        if (!opt_field(&p, &t.bs2_ms))         break;
        if (!opt_field(&p, &t.bs3_ms))         break;
    } while (0);

    // Trailing characters must only be separators / terminators / whitespace.
    while (*p == ';' || *p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') p++;
    if (*p != '\0') return false;

    *out = t;
    return true;
}
