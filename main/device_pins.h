// Runtime GPIO pin map for the displays, touch and LED strip. Loaded from NVS at
// boot (defaults = the hardware's stock wiring) so the pin layout can be set from
// the PC app (@PINS) without reflashing. Applied at boot — a pin change reboots.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int sclk, mosi, miso, dc;        // shared SPI bus (DC shared by both panels)
    int disp1_cs, disp2_cs;          // per-display chip selects
    int touch1_cs, touch2_cs;        // per-touch chip selects
    int led_din;                     // WS2812/SK6812 data in
    int race_screen;                 // which display index (0/1) shows the race screen; the other shows buttons
    int led_rev;                     // # of LEDs for the rev/shift bar
    int led_tc;                      // # of LEDs for traction control
    int led_abs;                     // # of LEDs for ABS  (strip order: rev, then TC, then ABS)
    int led_rgbw;                    // 1 = SK6812 RGBW (4 bytes), 0 = WS2812 RGB (3 bytes)
} device_pins_t;

void               device_pins_load(void);                 // restore NVS or defaults
const device_pins_t *device_pins_get(void);
int                device_pins_load_json(const char *json, int len);  // @PINS handler; persists

#ifdef __cplusplus
}
#endif
