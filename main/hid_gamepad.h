// USB HID gamepad — a 32-button "button box" the sim sees as a controller.
//
// Touch widgets call hid_button_pulse() to fire a momentary press; the game,
// with that button bound (TC+, pit request, lights, ...), reacts. The actual
// USB device is a composite CDC + HID enumerated in simhub_main.c.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HID_BUTTON_COUNT 32

// Momentary press of button `btn` (0..31): pressed now, auto-released shortly.
void hid_button_pulse(int btn);

// Hold/release a button explicitly (for toggles or press-and-hold widgets).
void hid_button_set(int btn, bool pressed);

// Service pulse timers and push a report when the button mask changed.
// Call periodically (~every 10 ms).
void hid_gamepad_service(void);

#ifdef __cplusplus
}
#endif
