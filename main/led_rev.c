#include "led_rev.h"

#include "led_strip.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "car_data.h"
#include "device_pins.h"

// Strip layout — set at boot from device_pins (rev, then TC, then ABS). Plain
// ints (not #defines) so they're configurable from the PC app.
static int LED_COUNT = 16, REV_FIRST = 0, REV_COUNT = 12;
static int TC_FIRST = 12, TC_COUNT = 2, ABS_FIRST = 14, ABS_COUNT = 2;
static bool s_rgbw = true;     // SK6812 RGBW vs WS2812 RGB

#define DEFAULT_BRIGHT 110      // default brightness scale 0..255 (~43%)
#define FLASH_MS       100      // shift-light flash half-period

static const char *TAG = "led_rev";
static led_strip_handle_t s_strip;
static bool s_ok;
static int  s_bright = DEFAULT_BRIGHT;   // runtime brightness 0..255
static rev_cfg_t s_cfg = REV_CFG_DEFAULT;

typedef struct { uint8_t r, g, b; } rgb_t;

static const rgb_t C_OFF = { 0, 0, 0 };
static const rgb_t C_TC  = { 255,  80,   0 };   // TC amber
static const rgb_t C_ABS = {   0, 150, 255 };   // ABS cyan

static rgb_t hex2rgb(uint32_t c)
{
    return (rgb_t){ (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
}

void led_rev_set_config(const rev_cfg_t *cfg)
{
    if (cfg) s_cfg = *cfg;
}

static void set_px(int i, rgb_t c)
{
    int r = (c.r * s_bright) / 255, g = (c.g * s_bright) / 255, b = (c.b * s_bright) / 255;
    if (s_rgbw) led_strip_set_pixel_rgbw(s_strip, i, r, g, b, 0);  // 4-channel (white 0)
    else        led_strip_set_pixel(s_strip, i, r, g, b);          // 3-channel WS2812
}

void led_rev_set_brightness(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_bright = pct * 255 / 100;
    nvs_handle_t h;
    if (nvs_open("dash", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "bright", pct);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "brightness %d%%", pct);
}

int led_rev_get_brightness(void)
{
    return s_bright * 100 / 255;
}

void led_rev_init(void)
{
    // Strip layout + type from the runtime config (rev | TC | ABS, contiguous).
    const device_pins_t *p = device_pins_get();
    REV_COUNT = p->led_rev > 0 ? p->led_rev : 12;
    TC_COUNT  = p->led_tc  < 0 ? 0 : p->led_tc;
    ABS_COUNT = p->led_abs < 0 ? 0 : p->led_abs;
    REV_FIRST = 0;
    TC_FIRST  = REV_COUNT;
    ABS_FIRST = REV_COUNT + TC_COUNT;
    LED_COUNT = REV_COUNT + TC_COUNT + ABS_COUNT;
    if (LED_COUNT < 1) LED_COUNT = 1;
    s_rgbw = p->led_rgbw != 0;

    led_strip_config_t strip_config = {
        .strip_gpio_num = p->led_din,
        .max_leds = LED_COUNT,
        .led_pixel_format = s_rgbw ? LED_PIXEL_FORMAT_GRBW : LED_PIXEL_FORMAT_GRB,
        .led_model = s_rgbw ? LED_MODEL_SK6812 : LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        return;
    }
    led_strip_clear(s_strip);
    led_strip_refresh(s_strip);
    s_ok = true;

    // Restore saved brightness, if any.
    int32_t pct = -1;
    nvs_handle_t h;
    if (nvs_open("dash", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_i32(h, "bright", &pct) == ESP_OK && pct >= 0 && pct <= 100)
            s_bright = pct * 255 / 100;
        nvs_close(h);
    }
    ESP_LOGI(TAG, "rev strip on gpio%d, %d leds, bright=%d", device_pins_get()->led_din, LED_COUNT, s_bright);
}

void led_rev_selftest(void)
{
    if (!s_ok) return;
    ESP_LOGI(TAG, "led self-test (R/G/B + walking dot)");
    const rgb_t rgb[3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };
    for (int c = 0; c < 3; c++) {
        for (int i = 0; i < LED_COUNT; i++) set_px(i, rgb[c]);
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(220));
    }
    // single white dot walking the whole chain — count how many actually light
    for (int i = 0; i < LED_COUNT; i++) {
        led_strip_clear(s_strip);
        set_px(i, (rgb_t){ 255, 255, 255 });
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(45));
    }
    led_strip_clear(s_strip);
    led_strip_refresh(s_strip);
}

// Effective shift RPM: reported value, else ~93% of max.
static int shift_rpm_of(const simhub_telemetry_t *t)
{
    if (t->shift_rpm > 4200) return t->shift_rpm;
    if (t->max_rpm > 4200)   return (t->max_rpm * 93) / 100;
    return 0;
}

// Color (0xRRGGBB, 0 = off) of rev-bar LED i mapped across `count` LEDs, for the
// current telemetry — honoring the active shift config (or a loaded car) and the
// flash strobe. SHARED by the physical strip AND the on-screen rev lights so they
// always match. No brightness applied (the strip scales it in set_px).
uint32_t led_rev_segment_rgb(const simhub_telemetry_t *t, int i, int count)
{
    if (count <= 0) return 0;

    // Car-data path: per-gear, per-LED RPM thresholds + per-LED colors.
    const car_data_t *cd = car_data_get();
    if (cd->valid) {
        int gi = car_gear_index(t->gear);
        int rl = cd->redline[gi];
        bool over = (rl > 0 && t->rpm >= rl);
        int bm = cd->blink_ms ? cd->blink_ms : 100;
        bool flash_on = ((esp_timer_get_time() / 1000) / bm) & 1;
        int off = (count - cd->led_count) / 2;     // center fewer LEDs
        if (off < 0) off = 0;
        int ci = i - off;
        if (ci >= 0 && ci < cd->led_count) {
            if (over) return flash_on ? cd->led_color[ci] : 0;
            if (cd->thresh[gi][ci] > 0 && t->rpm >= cd->thresh[gi][ci]) return cd->led_color[ci];
        }
        return 0;
    }

    // Shift-config bar: fills from start_pct of the shift RPM, strobes at flash_pct.
    int shift = shift_rpm_of(t);
    int startrpm = shift * s_cfg.start_pct / 100;
    int span = shift - startrpm;
    int lit = 0;
    if (span > 0) {
        lit = (t->rpm - startrpm) * count / span;
        if (lit < 0) lit = 0;
        if (lit > count) lit = count;
    }
    int pct = (shift > 0) ? t->rpm * 100 / shift : 0;
    if (pct >= s_cfg.flash_pct)
        return (((esp_timer_get_time() / 1000) / FLASH_MS) & 1) ? s_cfg.col_flash : 0;
    if (i >= lit) return 0;
    if (i < s_cfg.n_green) return s_cfg.col_green;
    if (i < s_cfg.n_green + s_cfg.n_red) return s_cfg.col_red;
    return s_cfg.col_blue;
}

void led_rev_update(const simhub_telemetry_t *t)
{
    if (!s_ok) return;
    for (int i = 0; i < REV_COUNT; i++)
        set_px(REV_FIRST + i, hex2rgb(led_rev_segment_rgb(t, i, REV_COUNT)));
    for (int i = 0; i < TC_COUNT; i++)
        set_px(TC_FIRST + i, t->tc_active > 0 ? C_TC : C_OFF);
    for (int i = 0; i < ABS_COUNT; i++)
        set_px(ABS_FIRST + i, t->abs_active > 0 ? C_ABS : C_OFF);
    led_strip_refresh(s_strip);
}
