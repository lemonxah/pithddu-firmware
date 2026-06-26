#include "device_serial.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs.h"

#define NVS_NS   "dash"
#define NVS_KEY  "serial"

static const char *TAG = "serial";

// "PITH-" + 12 hex digits + nul = 18; round up for headroom.
static char s_serial[24];

// Fill s_serial from the factory eFuse MAC: "PITH-XXXXXXXXXXXX". The base MAC is
// globally unique per chip (Espressif OUI), so this is a universally unique id.
static void derive_from_mac(void)
{
    uint8_t mac[6] = { 0 };
    esp_efuse_mac_get_default(mac);
    snprintf(s_serial, sizeof(s_serial), "PITH-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char *device_serial_get(void)
{
    if (s_serial[0]) return s_serial;   // already resolved this boot

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        // No NVS — still return a valid (non-persisted) serial.
        derive_from_mac();
        ESP_LOGW(TAG, "NVS unavailable; using volatile serial %s", s_serial);
        return s_serial;
    }

    size_t len = sizeof(s_serial);
    if (nvs_get_str(h, NVS_KEY, s_serial, &len) == ESP_OK && s_serial[0]) {
        nvs_close(h);
        return s_serial;                // restored a previously generated serial
    }

    // First boot: generate, persist, reuse forever after.
    derive_from_mac();
    if (nvs_set_str(h, NVS_KEY, s_serial) == ESP_OK) nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "generated device serial %s", s_serial);
    return s_serial;
}
