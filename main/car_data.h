// Per-car shift-light data sourced from the lovely-car-data project
// (github.com/Lovely-Sim-Racing/lovely-car-data). The PC app downloads a car's
// JSON and pushes it with `@C{json}`; the rev counter then drives its LEDs from
// per-gear, per-LED RPM thresholds + per-LED colors — accurate for the car you
// are actually driving. Falls back to the generic rev config when no car loaded.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAR_LED_MAX  12          // physical rev LEDs on the strip
#define CAR_GEARS    11          // R, N, 1..9

typedef struct {
    bool     valid;
    uint8_t  led_count;                       // ledNumber (clamped to CAR_LED_MAX)
    uint16_t blink_ms;                        // redlineBlinkInterval
    uint32_t led_color[CAR_LED_MAX];          // per-LED 0xRRGGBB
    uint16_t redline[CAR_GEARS];              // per-gear shift/redline rpm
    uint16_t thresh[CAR_GEARS][CAR_LED_MAX];  // per-gear per-LED on-threshold rpm
    char     name[24];
} car_data_t;

// Gear char ('R','N','1'..'9') -> gear index 0..10.
int car_gear_index(char g);

const car_data_t *car_data_get(void);

// Parse a lovely-car-data JSON document, apply + persist to NVS. 0 = ok, <0 err.
// `len` may be -1 for a NUL-terminated string.
int car_data_load_json(const char *json, int len);

// Restore a stored car from NVS at boot (if any).
void car_data_load_nvs(void);

#ifdef __cplusplus
}
#endif
