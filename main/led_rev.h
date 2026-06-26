// WS2812 rev-counter + TC/ABS LED strip (RMT backend).
//
// One data line (GPIO43) drives a 14-LED chain:
//   index 0..9   rev counter (RPM bar)
//   index 10..11 TC engagement  (left pair)
//   index 12..13 ABS engagement (right pair)
#pragma once

#include <stdint.h>
#include "simhub_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configurable shift-light behavior (lives in a profile so users pick it).
// The bar only lights across the top band: empty below start_pct of the shift
// RPM, filling to full at the shift point, then the whole bar strobes at/above
// flash_pct. Zones are colored bottom->top: n_green, then n_red, then n_blue
// (sum should be <= 12). Colors are 0xRRGGBB.
typedef struct {
    uint8_t  start_pct;    // bar starts lighting here (% of shift RPM), e.g. 85
    uint8_t  flash_pct;    // whole bar strobes at/above here, e.g. 98
    uint8_t  n_green;      // LED counts per zone (bottom -> top)
    uint8_t  n_red;
    uint8_t  n_blue;
    uint32_t col_green;    // 0xRRGGBB
    uint32_t col_red;
    uint32_t col_blue;
    uint32_t col_flash;
} rev_cfg_t;

// Default = a normal progressive rev bar: fills from ~50% of the shift RPM
// (green -> red -> blue zones) and strobes blue at the shift point.
#define REV_CFG_DEFAULT { 50, 98, 6, 3, 3, 0x00FF00, 0xFF0000, 0x0050FF, 0x0050FF }

// Apply a shift-light config (called when a profile loads/changes).
void led_rev_set_config(const rev_cfg_t *cfg);

// Color (0xRRGGBB, 0 = off) of rev-bar LED i across `count` LEDs for the current
// telemetry — shared by the physical strip and the on-screen rev lights.
uint32_t led_rev_segment_rgb(const simhub_telemetry_t *t, int i, int count);

// Bring up the RMT/led_strip device. Safe to call once at boot.
void led_rev_init(void);

// Boot color/count test: solid R, then G, then B (1s each), then a single white
// dot walking down the whole chain. Reveals color order + how many LEDs respond.
void led_rev_selftest(void);

// Set overall strip brightness 0..100 % (persisted to NVS).
void led_rev_set_brightness(int pct);

// Current strip brightness as 0..100 %.
int led_rev_get_brightness(void);

// Recompute and push the strip from the latest telemetry. Call ~20-30 Hz;
// the shift-point flash is timed internally.
void led_rev_update(const simhub_telemetry_t *t);

#ifdef __cplusplus
}
#endif
