// Data-driven button/profile model.
//
// Button pages are NOT hardcoded — they come from a "profile" (per game type).
// The firmware ships a baked-in default profile; a companion PC app will later
// edit profiles and push replacements over the wire (see ui_load_profile_blob).
//
// Telemetry pages (HUD, tyres, brakes, fuel, map) stay code-rendered; only the
// interactive button pages are config-driven, since that's what users customize.
#pragma once

#include <stdint.h>
#include "led_rev.h"

#ifdef __cplusplus
extern "C" {
#endif

// What a button does when tapped.
typedef enum {
    UI_ACT_NONE = 0,
    UI_ACT_HID,        // fire HID gamepad button (param = button index 0..31)
    UI_ACT_HID_HOLD,   // press while touched, release on lift (param = button)
    UI_ACT_PAGE,       // switch this display to page id (param = page index)
    UI_ACT_PEEK,       // peek a page for a few seconds (param = page index)
} ui_action_t;

#define UI_LABEL_MAX   15
#define UI_PAGE_BTNS   8     // max buttons per button-page
#define UI_PROFILE_PG  5     // max button-pages per profile
#define UI_GAME_MAX    23

typedef struct {
    int16_t  x, y, w, h;             // rect on the display
    uint16_t color;                  // RGB565 accent
    uint8_t  action;                 // ui_action_t
    uint8_t  param;                   // action argument (HID btn / page id)
    char     label[UI_LABEL_MAX + 1];
    uint8_t  toggle;                 // 1 = latching toggle (lit when on), 0 = momentary push
    uint8_t  field;                  // sync-from-game: field id whose telemetry value drives the
                                     // lit state (0 = none -> latch locally). See field_registry_gen.h.
} ui_button_t;

typedef struct {
    char        name[UI_LABEL_MAX + 1];
    uint8_t     count;
    ui_button_t buttons[UI_PAGE_BTNS];
} ui_button_page_t;

typedef struct {
    char             game[UI_GAME_MAX + 1];   // game this profile targets
    uint8_t          page_count;
    ui_button_page_t pages[UI_PROFILE_PG];
    rev_cfg_t        rev;                      // shift-light behavior
} ui_profile_t;

// Load a persisted profile from NVS into the active slot (call once at boot,
// before the UI starts). Falls back to the baked-in default if none stored.
void ui_config_load(void);

// The active profile (baked-in default until a PC app pushes a new one).
const ui_profile_t *ui_active_profile(void);

// Replace the active profile from a JSON document (PC app -> device over CDC).
// Validates, applies live, and persists to NVS. Returns 0 on success, <0 on
// a parse/validation error. `len` may be -1 for a NUL-terminated string.
int ui_load_profile_json(const char *json, int len);

// Load just the button pages from the PC app's "@BS" payload
// ({"pages":[[{label,kind,action,color,...}]]}). Auto-lays out each page as a
// 3x2 grid on the side screen and assigns sequential HID buttons. Keeps the
// current game + shift-light config. Returns 0 on success.
int ui_load_buttons_json(const char *json, int len);

#ifdef __cplusplus
}
#endif
