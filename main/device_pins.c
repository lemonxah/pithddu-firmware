#include "device_pins.h"
#include "cJSON.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "pins";
#define NVS_NS  "dash"
#define NVS_KEY "pins"

// Stock wiring (Seeed XIAO S3) — used until the app pushes a layout.
static device_pins_t s_pins = {
    .sclk = 7, .mosi = 9, .miso = 8, .dc = 2,
    .disp1_cs = 1, .disp2_cs = 3,
    .touch1_cs = 5, .touch2_cs = 6,
    .led_din = 43,
    .race_screen = 0,
    .led_rev = 12, .led_tc = 2, .led_abs = 2, .led_rgbw = 1,
};

void device_pins_load(void)
{
    nvs_handle_t h;
    size_t sz = sizeof(s_pins);
    device_pins_t tmp;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, NVS_KEY, &tmp, &sz) == ESP_OK && sz == sizeof(tmp)) {
            s_pins = tmp;
            ESP_LOGI(TAG, "restored pin map");
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "pins sclk=%d mosi=%d miso=%d dc=%d cs1=%d cs2=%d t1=%d t2=%d led=%d",
             s_pins.sclk, s_pins.mosi, s_pins.miso, s_pins.dc,
             s_pins.disp1_cs, s_pins.disp2_cs, s_pins.touch1_cs, s_pins.touch2_cs, s_pins.led_din);
}

const device_pins_t *device_pins_get(void) { return &s_pins; }

// Valid GPIO range for the ESP32-S3 (0..48).
static int pin_ok(int p) { return p >= 0 && p <= 48; }

static int get_pin(const cJSON *o, const char *key, int cur)
{
    const cJSON *e = cJSON_GetObjectItem(o, key);
    if (cJSON_IsNumber(e) && pin_ok(e->valueint)) return e->valueint;
    return cur;
}

int device_pins_load_json(const char *json, int len)
{
    cJSON *root = (len < 0) ? cJSON_Parse(json) : cJSON_ParseWithLength(json, len);
    if (!root) return -1;

    device_pins_t p = s_pins;   // start from current, override provided keys
    p.sclk      = get_pin(root, "sclk", p.sclk);
    p.mosi      = get_pin(root, "mosi", p.mosi);
    p.miso      = get_pin(root, "miso", p.miso);
    p.dc        = get_pin(root, "dc", p.dc);
    p.disp1_cs  = get_pin(root, "disp1_cs", p.disp1_cs);
    p.disp2_cs  = get_pin(root, "disp2_cs", p.disp2_cs);
    p.touch1_cs = get_pin(root, "touch1_cs", p.touch1_cs);
    p.touch2_cs = get_pin(root, "touch2_cs", p.touch2_cs);
    p.led_din   = get_pin(root, "led_din", p.led_din);
    const cJSON *rs = cJSON_GetObjectItem(root, "race_screen");
    if (cJSON_IsNumber(rs)) p.race_screen = (rs->valueint == 1) ? 1 : 0;
    const cJSON *lr = cJSON_GetObjectItem(root, "led_rev");
    const cJSON *lt = cJSON_GetObjectItem(root, "led_tc");
    const cJSON *la = cJSON_GetObjectItem(root, "led_abs");
    const cJSON *lw = cJSON_GetObjectItem(root, "led_rgbw");
    if (cJSON_IsNumber(lr) && lr->valueint >= 0 && lr->valueint <= 64) p.led_rev = lr->valueint;
    if (cJSON_IsNumber(lt) && lt->valueint >= 0 && lt->valueint <= 16) p.led_tc = lt->valueint;
    if (cJSON_IsNumber(la) && la->valueint >= 0 && la->valueint <= 16) p.led_abs = la->valueint;
    if (cJSON_IsNumber(lw)) p.led_rgbw = lw->valueint ? 1 : 0;
    cJSON_Delete(root);

    s_pins = p;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY, &s_pins, sizeof(s_pins));
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGW(TAG, "pin map updated — reboot to apply");
    return 0;
}
