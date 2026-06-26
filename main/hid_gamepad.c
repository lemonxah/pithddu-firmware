#include "hid_gamepad.h"

#include <string.h>
#include "esp_timer.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"

#define PULSE_MS 60        // how long a tapped button stays "pressed"

static uint32_t s_mask;            // current button bitmask
static uint32_t s_sent;            // last mask we reported to the host
static int64_t  s_release_us[HID_BUTTON_COUNT];   // 0 = not auto-releasing

void hid_button_pulse(int btn)
{
    if (btn < 0 || btn >= HID_BUTTON_COUNT) return;
    s_mask |= (1u << btn);
    s_release_us[btn] = esp_timer_get_time() + (int64_t)PULSE_MS * 1000;
}

void hid_button_set(int btn, bool pressed)
{
    if (btn < 0 || btn >= HID_BUTTON_COUNT) return;
    if (pressed) s_mask |= (1u << btn);
    else         s_mask &= ~(1u << btn);
    s_release_us[btn] = 0;   // explicit control cancels any pulse timer
}

void hid_gamepad_service(void)
{
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < HID_BUTTON_COUNT; i++) {
        if (s_release_us[i] && now >= s_release_us[i]) {
            s_mask &= ~(1u << i);
            s_release_us[i] = 0;
        }
    }
    if (s_mask == s_sent) return;
    if (!tud_mounted() || !tud_hid_ready()) return;

    if (tud_hid_report(1 /* joystick report id */, &s_mask, sizeof(s_mask))) {
        s_sent = s_mask;
    }
}
