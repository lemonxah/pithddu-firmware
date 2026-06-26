// UI runtime: drives both ST7796 displays, renders telemetry + button pages,
// reads touch and fires HID. C-callable so simhub_main.c can drive it.
#pragma once

#include "simhub_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bring up both displays + touch. Call once after boot.
void ui_init(void);

// Render the active page on each display and service touch. Call ~15 Hz with
// the latest telemetry.
void ui_tick(const simhub_telemetry_t *t);

#ifdef __cplusplus
}
#endif
