#include "car_data.h"

#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "car_data";
#define NVS_NS  "dash"
#define NVS_KEY "car"

static car_data_t s_car;   // .valid = false until a car is loaded

int car_gear_index(char g)
{
    if (g == 'R') return 0;
    if (g == 'N') return 1;
    if (g >= '1' && g <= '9') return 2 + (g - '1');
    return 1;   // default to neutral
}

const car_data_t *car_data_get(void)
{
    return &s_car;
}

// "#AARRGGBB" (or "#RRGGBB") -> 0xRRGGBB (alpha dropped).
static uint32_t parse_hex(const char *s)
{
    if (!s) return 0;
    if (*s == '#') s++;
    return (uint32_t)strtoul(s, NULL, 16) & 0xFFFFFFu;
}

int car_data_load_json(const char *json, int len)
{
    cJSON *root = (len < 0) ? cJSON_Parse(json) : cJSON_ParseWithLength(json, len);
    if (!root) return -1;

    static car_data_t c;          // off the USB-callback stack
    memset(&c, 0, sizeof(c));

    const cJSON *nm = cJSON_GetObjectItem(root, "carName");
    if (cJSON_IsString(nm) && nm->valuestring)
        strncpy(c.name, nm->valuestring, sizeof(c.name) - 1);

    const cJSON *ln = cJSON_GetObjectItem(root, "ledNumber");
    int raw_n = cJSON_IsNumber(ln) ? ln->valueint : 0;
    if (raw_n < 0) raw_n = 0;
    int n = raw_n > CAR_LED_MAX ? CAR_LED_MAX : raw_n;
    c.led_count = (uint8_t)n;
    // When a car has more LEDs than the physical strip, drop the LOWER (early)
    // LEDs and keep the upper ones — those carry the red/blue shift zone near
    // redline, which is what actually matters. `skip` shifts our window up.
    int skip = raw_n > CAR_LED_MAX ? raw_n - CAR_LED_MAX : 0;

    const cJSON *bi = cJSON_GetObjectItem(root, "redlineBlinkInterval");
    c.blink_ms = cJSON_IsNumber(bi) ? (uint16_t)bi->valueint : 100;
    if (c.blink_ms < 20) c.blink_ms = 100;

    // ledColor[0] is the redline color; [1..raw_n] are the per-LED colors.
    // We keep the upper `n` LEDs, so start at index skip+1.
    const cJSON *lc = cJSON_GetObjectItem(root, "ledColor");
    if (cJSON_IsArray(lc)) {
        for (int i = 0; i < n; i++) {
            const cJSON *col = cJSON_GetArrayItem(lc, skip + i + 1);
            if (cJSON_IsString(col)) c.led_color[i] = parse_hex(col->valuestring);
        }
    }

    // ledRpm is an array whose first element maps each gear to
    // [redline, led1rpm, ..., ledNrpm].
    const cJSON *lr = cJSON_GetObjectItem(root, "ledRpm");
    const cJSON *gears = cJSON_IsArray(lr) ? cJSON_GetArrayItem(lr, 0) : NULL;
    if (cJSON_IsObject(gears)) {
        static const char *keys[CAR_GEARS] =
            { "R", "N", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
        for (int gi = 0; gi < CAR_GEARS; gi++) {
            const cJSON *arr = cJSON_GetObjectItem(gears, keys[gi]);
            if (!cJSON_IsArray(arr)) continue;
            const cJSON *rl = cJSON_GetArrayItem(arr, 0);
            if (cJSON_IsNumber(rl)) c.redline[gi] = (uint16_t)rl->valueint;
            for (int i = 0; i < n; i++) {
                const cJSON *th = cJSON_GetArrayItem(arr, skip + i + 1);
                if (cJSON_IsNumber(th)) c.thresh[gi][i] = (uint16_t)th->valueint;
            }
        }
    }

    c.valid = (n > 0);
    cJSON_Delete(root);
    if (!c.valid) return -2;

    s_car = c;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY, &s_car, sizeof(s_car));
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "car '%s' loaded: %d leds, blink %dms", s_car.name, s_car.led_count, s_car.blink_ms);
    return 0;
}

void car_data_load_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    car_data_t tmp;
    size_t sz = sizeof(tmp);
    if (nvs_get_blob(h, NVS_KEY, &tmp, &sz) == ESP_OK &&
        sz == sizeof(tmp) && tmp.led_count <= CAR_LED_MAX) {
        s_car = tmp;
        ESP_LOGI(TAG, "restored car '%s'", s_car.name);
    }
    nvs_close(h);
}
