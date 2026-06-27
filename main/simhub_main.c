// pithddu — ESP32-S3 SimHub dashboard for dual ST7796 touch
// displays + a WS2812 rev/TC/ABS strip + an on-screen HID button box.
//
// SimHub's Custom Serial frames arrive over USB CDC (composite with an HID
// gamepad). They are parsed into g_telemetry; three tasks consume it:
//   ui_task  — renders both displays (LovyanGFX) and services touch -> HID
//   led_task — drives the rev-counter / TC / ABS LEDs
//   hid_task — pushes HID gamepad reports for tapped buttons

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "soc/rtc_cntl_reg.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"

#include "simhub_proto.h"
#include "ui.hpp"
#include "led_rev.h"
#include "hid_gamepad.h"
#include "ui_config.h"
#include "car_data.h"
#include "race_layout.h"
#include "device_pins.h"
#include "device_serial.h"
#include "field_registry_gen.h"   // field_value + FIELD_COUNT for the @T live-telemetry reply

static const char *TAG = "simhub";

// Composite USB descriptors (usb_descriptors.c).
extern const tusb_desc_device_t usb_device_descriptor;
extern const uint8_t  usb_fs_config_descriptor[];
extern const char    *usb_string_descriptors[];
extern const int      usb_string_descriptor_count;
extern void           usb_descriptors_init(void);  // fills the serial string

// Latest parsed telemetry. Written by the CDC RX callback, read by the tasks.
static volatile simhub_telemetry_t g_telemetry = { .gear = 'N' };

// Parse counters for the @S capacity test (frames OK vs malformed/dropped).
static volatile uint32_t s_frames_ok, s_frames_bad;

// Input simulation toggle (config screen) — sim overrides real telemetry.
static volatile bool s_sim_on = false;

#define RX_BUF_SZ   512           // drain more per read (faster OTA flash writes)
#define LINE_BUF_SZ 8192          // fits a full module-spec layout push (@RS{...}) + JSON profiles
static uint8_t s_rx_buf[RX_BUF_SZ];
// Per-transport line accumulation so CDC telemetry and HID commands don't mix.
typedef struct { char line[LINE_BUF_SZ]; size_t len; } rx_ctx_t;
static rx_ctx_t s_cdc_ctx, s_hid_ctx;

// A command can arrive on the CDC port (itf 0/1) or the HID command channel
// (this pseudo-itf). Replies route back over the same transport.
#define HID_ITF 0x20

// ---- HID command reply channel (report id 2, chunked into 61-byte payloads) ----
static uint8_t       s_hid_tx[2048];
static volatile int  s_hid_tx_len, s_hid_tx_pos;

static void hid_pump(void)
{
    if (s_hid_tx_pos >= s_hid_tx_len) return;
    if (!tud_hid_ready()) return;
    int n = s_hid_tx_len - s_hid_tx_pos;
    if (n > 61) n = 61;
    uint8_t rep[62];
    rep[0] = (uint8_t)n;                       // payload length
    memcpy(rep + 1, s_hid_tx + s_hid_tx_pos, n);
    if (tud_hid_report(2, rep, n + 1)) s_hid_tx_pos += n;
}

// Called from tud_hid_report_complete_cb when an IN report has been delivered.
void hid_tx_complete(void) { hid_pump(); }

static void hid_reply(const char *s)
{
    int len = (int)strlen(s);
    if (s_hid_tx_pos >= s_hid_tx_len) { s_hid_tx_len = 0; s_hid_tx_pos = 0; }
    if (s_hid_tx_len + len < (int)sizeof(s_hid_tx)) {
        memcpy(s_hid_tx + s_hid_tx_len, s, len);
        s_hid_tx_len += len;
    }
    hid_pump();
}

static void cdc_reply(int itf, const char *s)
{
    if (itf == HID_ITF) { hid_reply(s); return; }
    tinyusb_cdcacm_write_queue(itf, (const uint8_t *)s, strlen(s));
    tinyusb_cdcacm_write_flush(itf, 0);
}

// ---- OTA over USB-CDC ----
// PC app sends "@OTA<bytes>\n" then streams the raw .bin; we write it to the
// inactive app slot and reboot into it (with rollback if it won't run).
#define OTA_ACK_CHUNK 2048      // device ACKs ("K") every this-many bytes written
static esp_ota_handle_t       s_ota_handle;
static const esp_partition_t *s_ota_part;
static int            s_ota_remaining;
static int            s_ota_acc;        // bytes written since the last ACK
static bool           s_ota_active;
static int            s_ota_total;      // full image size (for progress %)
static int64_t        s_ota_last_us;    // last OTA byte time -> abandoned-transfer timeout
static int            s_ota_itf = -1;   // transport that owns the in-flight OTA

// Exposed to the UI so the device shows an "updating firmware" screen + progress
// bar while an OTA is in flight.
bool ui_ota_active(void) { return s_ota_active; }
int  ui_ota_pct(void) {
    if (s_ota_total <= 0) return 0;
    int done = s_ota_total - s_ota_remaining;
    return done <= 0 ? 0 : (done >= s_ota_total ? 100 : (int)((int64_t)done * 100 / s_ota_total));
}
static volatile bool  s_ota_reboot;
static volatile bool  s_cfg_reboot;   // pin-layout pushed -> reboot to apply

static bool ota_begin(int itf, int size)
{
    s_ota_part = esp_ota_get_next_update_partition(NULL);
    if (!s_ota_part || size <= 0) return false;
    s_ota_itf = itf;
    if (esp_ota_begin(s_ota_part, size, &s_ota_handle) != ESP_OK) {
        s_ota_part = NULL;
        return false;
    }
    s_ota_remaining = size;
    s_ota_total = size;
    s_ota_acc = 0;
    s_ota_active = true;
    s_ota_last_us = esp_timer_get_time();
    ESP_LOGW(TAG, "OTA begin: %d bytes -> %s", size, s_ota_part->label);
    return true;
}

// Consume up to `len` image bytes; returns the number consumed. Finalizes and
// flags a reboot once the full image has been received.
static int ota_feed(int itf, const uint8_t *data, int len)
{
    s_ota_last_us = esp_timer_get_time();
    int n = len < s_ota_remaining ? len : s_ota_remaining;
    if (esp_ota_write(s_ota_handle, data, n) != ESP_OK) {
        esp_ota_abort(s_ota_handle);
        s_ota_active = false;
        cdc_reply(itf, "OTAERR\n");
        return n;
    }
    s_ota_remaining -= n;
    s_ota_acc += n;
    if (s_ota_remaining <= 0) {
        s_ota_active = false;
        if (esp_ota_end(s_ota_handle) == ESP_OK &&
            esp_ota_set_boot_partition(s_ota_part) == ESP_OK) {
            cdc_reply(itf, "OTADONE\n");
            s_ota_reboot = true;
        } else {
            cdc_reply(itf, "OTAERR\n");
        }
    } else if (s_ota_acc >= OTA_ACK_CHUNK) {
        s_ota_acc -= OTA_ACK_CHUNK;
        cdc_reply(itf, "K\n");        // flow control: "ready for the next chunk"
    }
    return n;
}

// Current car model, as reported by SimHub via "@CM<model>" (DataCorePlugin's
// CarModel). Relayed to the PC app in the "?" status so it can auto-load the
// matching lovely-car-data shift-light profile. Empty until SimHub sends one.
static char s_car_model[48] = "";

static void handle_line(int itf, const char *line)
{
    if (line[0] == '?' && line[1] == '\0') {
        char st[256];
        simhub_telemetry_t cur = g_telemetry;
        // car=<model> is last so the PC parser can read it to end-of-line (spaces).
        snprintf(st, sizeof(st),
                 "esp-simhub | profile=%s g=%c s=%d r=%d/%d fuel=%d.%d delta=%d bright=%d heap=%lu minheap=%lu car=%s\n",
                 ui_active_profile()->game, cur.gear, cur.speed_kmh, cur.rpm,
                 cur.max_rpm, cur.fuel_dl / 10, cur.fuel_dl % 10, cur.delta_ms,
                 led_rev_get_brightness(),
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size(),
                 s_car_model);
        cdc_reply(itf, st);
        return;
    }
    // Profile push from the PC editor: "@P{json...}". Apply + persist.
    // Pin-layout push: "@PINS{json}". Persisted; device reboots to apply.
    // Checked before "@P" (profile) since it shares the prefix.
    if (strncmp(line, "@PINS", 5) == 0) {
        int rc = device_pins_load_json(line + 5, -1);
        cdc_reply(itf, rc == 0 ? "OK\n" : "ERR\n");
        if (rc == 0) s_cfg_reboot = true;
        return;
    }
    if (line[0] == '@' && line[1] == 'P') {
        int rc = ui_load_profile_json(line + 2, -1);
        cdc_reply(itf, rc == 0 ? "OK\n" : "ERR\n");
        return;
    }
    // Button-pages push: "@BS{json}". Checked before "@B" since it shares the
    // prefix (otherwise "@BS" hits @B and atoi("S..") sets brightness to 0).
    if (strncmp(line, "@BS", 3) == 0) {
        int rc = ui_load_buttons_json(line + 3, -1);
        cdc_reply(itf, rc == 0 ? "OK\n" : "ERR\n");
        return;
    }
    // Rev-counter brightness: "@B<0-100>". Persisted.
    if (line[0] == '@' && line[1] == 'B') {
        led_rev_set_brightness(atoi(line + 2));
        cdc_reply(itf, "OK\n");
        return;
    }
    // Capability handshake: "@CAP" -> JSON describing screens + LED layout so the
    // PC app knows how to program this specific device. Checked before "@C".
    if (strncmp(line, "@CAP", 4) == 0) {
        const device_pins_t *p = device_pins_get();
        const ui_profile_t *prof = ui_active_profile();
        int button_pages = prof ? prof->page_count : 0;   // actual configured pages, not a constant
#ifndef PITHDDU_BOARD
#define PITHDDU_BOARD "unknown"
#endif
        char cap[600];
        snprintf(cap, sizeof(cap),
            "{\"name\":\"Pith DDU\",\"fw\":\"0.9.4\",\"board\":\"%s\",\"serial\":\"%s\",\"buttonPages\":%d,"
            "\"screens\":[{\"role\":\"main\",\"w\":480,\"h\":320,\"touch\":true},"
            "{\"role\":\"side\",\"w\":480,\"h\":320,\"touch\":true}],"
            "\"leds\":{\"rev\":12,\"tc\":2,\"abs\":2,\"separate\":true},"
            "\"pins\":{\"sclk\":%d,\"mosi\":%d,\"miso\":%d,\"dc\":%d,"
            "\"disp1_cs\":%d,\"disp2_cs\":%d,\"touch1_cs\":%d,\"touch2_cs\":%d,\"led_din\":%d,"
            "\"race_screen\":%d,\"led_rev\":%d,\"led_tc\":%d,\"led_abs\":%d,\"led_rgbw\":%d}}\n",
            PITHDDU_BOARD, device_serial_get(), button_pages,
            p->sclk, p->mosi, p->miso, p->dc, p->disp1_cs, p->disp2_cs,
            p->touch1_cs, p->touch2_cs, p->led_din, p->race_screen,
            p->led_rev, p->led_tc, p->led_abs, p->led_rgbw);
        cdc_reply(itf, cap);
        return;
    }
    // Live telemetry snapshot for the app's preview: "@T" -> "<gear>;<f1>;<f2>;..."
    // (gear char then every registry field value in id order).
    if (line[0] == '@' && line[1] == 'T' && line[2] == '\0') {
        simhub_telemetry_t cur = g_telemetry;
        char st[480]; int n = 0;
        st[n++] = cur.gear ? cur.gear : 'N';
        for (int id = 1; id < FIELD_COUNT && n < (int)sizeof(st) - 12; id++)
            n += snprintf(st + n, sizeof(st) - n, ";%d", field_value(&cur, id));
        if (n < (int)sizeof(st) - 1) st[n++] = '\n';
        st[n] = '\0';
        cdc_reply(itf, st);
        return;
    }
    // Read back the active race layout: "@RG" -> the last-pushed @RS JSON (so the
    // PC app can show what's currently on the device). Checked before "@RS".
    if (strncmp(line, "@RG", 3) == 0) {
        const char *rj = race_layout_json();
        cdc_reply(itf, (rj && rj[0]) ? rj : "{}");
        cdc_reply(itf, "\n");
        return;
    }
    // Race-screen layout push (layout language): "@RS{json}". Apply + persist.
    if (strncmp(line, "@RS", 3) == 0) {
        int rc = race_layout_load_json(line + 3, -1);
        cdc_reply(itf, rc == 0 ? "OK\n" : "ERR\n");
        return;
    }
    // Shift-light custom config push: "@SL{json}" (same shape as @C car data).
    if (strncmp(line, "@SL", 3) == 0) {
        int rc = car_data_load_json(line + 3, -1);
        cdc_reply(itf, rc == 0 ? "OK\n" : "ERR\n");
        return;
    }
    // Car-model report from SimHub: "@CM<model>" (DataCorePlugin CarModel). Stored
    // and surfaced in "?" so the PC app can auto-apply the matching car profile.
    // Checked before "@C" since it shares the prefix.
    if (strncmp(line, "@CM", 3) == 0) {
        strncpy(s_car_model, line + 3, sizeof(s_car_model) - 1);
        s_car_model[sizeof(s_car_model) - 1] = '\0';
        cdc_reply(itf, "OK\n");
        return;
    }
    // Car data push (lovely-car-data JSON): "@C{json}". Apply + persist.
    if (line[0] == '@' && line[1] == 'C') {
        int rc = car_data_load_json(line + 2, -1);
        cdc_reply(itf, rc == 0 ? "OK\n" : "ERR\n");
        return;
    }
    // OTA firmware push: "@OTA<size>" then <size> raw image bytes follow.
    if (strncmp(line, "@OTA", 4) == 0) {
        cdc_reply(itf, ota_begin(itf, atoi(line + 4)) ? "OTAREADY\n" : "OTAERR\n");
        return;
    }
    // Capacity-test parse stats: "@S" -> "ok=N bad=M", then resets the counters.
    if (line[0] == '@' && line[1] == 'S') {
        char st[48];
        snprintf(st, sizeof(st), "ok=%u bad=%u\n",
                 (unsigned)s_frames_ok, (unsigned)s_frames_bad);
        cdc_reply(itf, st);
        s_frames_ok = 0;
        s_frames_bad = 0;
        return;
    }
    simhub_telemetry_t t;
    if (simhub_parse_line(line, &t)) {
        if (!s_sim_on) g_telemetry = t;   // simulation overrides real telemetry
        s_frames_ok++;
    } else {
        s_frames_bad++;
    }
}

// Shared byte processor for any transport (CDC or HID). OTA bytes are raw and go
// to the slot owner; everything else accumulates into the transport's line buffer.
static void process_rx(int itf, rx_ctx_t *ctx, const uint8_t *buf, size_t len)
{
    size_t i = 0;
    while (i < len) {
        if (s_ota_active && itf == s_ota_itf) {
            i += ota_feed(itf, &buf[i], (int)(len - i));
            continue;
        }
        char c = (char)buf[i++];
        if (c == '\n' || c == '\r') {
            if (ctx->len > 0) { ctx->line[ctx->len] = '\0'; handle_line(itf, ctx->line); ctx->len = 0; }
            continue;
        }
        if (c == '$' && ctx->len > 0) {
            ctx->line[ctx->len] = '\0'; handle_line(itf, ctx->line); ctx->len = 0;
        }
        if (ctx->len < LINE_BUF_SZ - 1) ctx->line[ctx->len++] = c;
        else ctx->len = 0;
    }
}

static void cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    // Drain everything buffered this interrupt — a single 64-byte read leaves
    // the rest in the FIFO and high-rate frames overrun it.
    for (;;) {
        size_t rx_size = 0;
        if (tinyusb_cdcacm_read(itf, s_rx_buf, sizeof(s_rx_buf), &rx_size) != ESP_OK) return;
        if (rx_size == 0) return;
        process_rx(itf, &s_cdc_ctx, s_rx_buf, rx_size);
    }
}

// HID command OUT report payload = [N][N data bytes]; called from
// tud_hid_set_report_cb (report id 2). Feeds the same parser as CDC.
void hid_cmd_rx(const uint8_t *buf, uint16_t len)
{
    if (len < 1) return;
    int n = buf[0];
    if (n > (int)len - 1) n = (int)len - 1;
    if (n > 0) process_rx(HID_ITF, &s_hid_ctx, buf + 1, (size_t)n);
}

// 1200-baud touch -> reboot into ROM download mode (button-free reflash).
static volatile bool s_reboot_to_dl = false;
static void cdc_line_coding_changed(int itf, cdcacm_event_t *event)
{
    if (event->line_coding_changed_data.p_line_coding->bit_rate == 1200) {
        s_reboot_to_dl = true;
    }
}

static void ui_task(void *arg)
{
    ui_init();
    while (1) {
        simhub_telemetry_t cur = g_telemetry;
        ui_tick(&cur);
        vTaskDelay(pdMS_TO_TICKS(33));   // pacing; push time dominates
    }
}

static void led_task(void *arg)
{
    led_rev_init();
    led_rev_selftest();   // R/G/B + walking dot at boot
    while (1) {
        simhub_telemetry_t cur = g_telemetry;
        led_rev_update(&cur);
        vTaskDelay(pdMS_TO_TICKS(33));   // ~30 Hz for smooth rev sweep
    }
}

static void hid_task(void *arg)
{
    while (1) {
        hid_gamepad_service();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Boot self-test: sweep speed / rpm / gear from internally generated values so
// you can confirm the display and LED strip are actually driven, independent of
// any incoming SimHub telemetry. Resets everything to idle when done.
static void run_selftest(void)
{
    static const char gears[] = { 'N', '1', '2', '3', '4', '5', '6', 'R' };
    ESP_LOGI(TAG, "self-test sweep");
    for (int i = 0; i <= 60; i++) {
        int up = (i <= 30) ? i : (60 - i);        // 0 -> 30 -> 0 triangle
        simhub_telemetry_t t = { 0 };
        t.gear      = gears[(i / 4) % 8];
        t.speed_kmh = up * 10;                     // 0 -> 300 -> 0 km/h
        t.rpm       = 1500 + up * 220;             // 1500 -> 8100 -> 1500
        t.max_rpm   = 8800;
        t.shift_rpm = 8100;
        g_telemetry = t;
        vTaskDelay(pdMS_TO_TICKS(18));
    }
    simhub_telemetry_t idle = { .gear = 'N' };
    g_telemetry = idle;
    ESP_LOGI(TAG, "self-test done");
}

// ---- Input simulation (toggled from the config screen) for bench testing ----
// Generates a full sweep of every telemetry field so the whole dash animates
// without SimHub. Exposed to the UI (config-screen toggle).
void sim_set(bool on) { s_sim_on = on; }
bool sim_get(void)    { return s_sim_on; }

static void sim_fill(simhub_telemetry_t *t)
{
    static int phase = 0, gear = 1, rpm = 1500, accel = 1, lap = 1;
    phase++;
    const int shift = 8100;
    if (accel) {
        rpm += 220;
        if (rpm >= shift) { if (gear < 6) { gear++; rpm = 5200; } else accel = 0; }
    } else {
        rpm -= 180;
        if (rpm <= 1500) { gear = 1; rpm = 1500; accel = 1; lap++; }
    }
    int trel = (rpm - 1500) / 60;
    t->gear = (char)('0' + gear);
    t->speed_kmh = gear * 38 + trel;
    t->rpm = rpm;
    t->max_rpm = 8800;
    t->shift_rpm = shift;
    t->cur_lap_ms = (phase * 50) % 95000;
    t->last_lap_ms = 84012;  t->best_lap_ms = 82900;
    t->pb_lap_ms = 82500;    t->est_lap_ms = 83100;
    t->delta_ms = -3000 + (phase % 60) * 100;   // 0.1ms units: -0.300s .. +0.290s
    t->position = 4;  t->field_size = 20;
    t->laps_done = lap;  t->total_laps = 30;  t->laps_left = 30 - (lap % 30);
    t->water_c = 90 + (phase / 20) % 20;
    t->oil_c = 105 + (phase / 25) % 20;
    t->oil_press_x10 = 42 + (phase % 8);
    t->boost_kpa = accel ? 120 : 20;
    t->tc = 4;  t->abs = 2;  t->brake_bias_x10 = 565;
    t->fuel_dl = 700 - (lap % 80) * 8;
    t->fuel_cap_dl = 750;  t->fuel_per_lap_ml = 3200;  t->fuel_laps_x10 = 130;
    int tb = 80 + (phase / 8) % 30;
    t->tt_fl_i = tb;     t->tt_fl_m = tb + 4; t->tt_fl_o = tb + 2;
    t->tt_fr_i = tb + 1; t->tt_fr_m = tb + 5; t->tt_fr_o = tb + 3;
    t->tt_rl_i = tb - 2; t->tt_rl_m = tb + 2; t->tt_rl_o = tb;
    t->tt_rr_i = tb - 1; t->tt_rr_m = tb + 3; t->tt_rr_o = tb + 1;
    t->tp_fl = 165; t->tp_fr = 166; t->tp_rl = 160; t->tp_rr = 161;
    int wear = 95 - (lap % 90);
    t->tw_fl = wear; t->tw_fr = wear - 2; t->tw_rl = wear - 1; t->tw_rr = wear - 3;
    int brk = 250 + (phase / 6) % 450;
    t->bt_fl = brk; t->bt_fr = brk + 10; t->bt_rl = brk - 40; t->bt_rr = brk - 30;
    t->throttle = accel ? 100 : 0;
    t->brake = accel ? 0 : 85;
    t->clutch = 0;
    t->steer = (phase % 100) - 50;
    t->tc_active = (accel && rpm > 7400) ? 1 : 0;
    t->abs_active = (!accel && rpm < 4000) ? 1 : 0;
    t->s1_ms = 28000 + (phase % 40) * 8;
    t->s2_ms = 30900 + (phase % 30) * 7;
    t->s3_ms = 25600 + (phase % 25) * 6;
    t->bs1_ms = 28100; t->bs2_ms = 30900; t->bs3_ms = 25600;
}

static void sim_task(void *arg)
{
    while (1) {
        if (s_sim_on) {
            simhub_telemetry_t t = { 0 };
            sim_fill(&t);
            g_telemetry = t;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "pithddu boot");

    esp_err_t nv = nvs_flash_init();
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ui_config_load();    // restore a PC-pushed profile, if any
    car_data_load_nvs(); // restore a pushed lovely-car-data car, if any
    device_pins_load();  // restore the PC-pushed pin map, else stock wiring
    race_layout_init();  // restore a PC-pushed race layout, else the default

    usb_descriptors_init();  // resolve the per-device serial before enumeration

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.task.size = 8192;     // CDC rx callback parses JSON profiles here
    tusb_cfg.descriptor.device            = &usb_device_descriptor;
    tusb_cfg.descriptor.string            = usb_string_descriptors;
    tusb_cfg.descriptor.string_count      = usb_string_descriptor_count;
    tusb_cfg.descriptor.full_speed_config = usb_fs_config_descriptor;
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    const tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = &cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = &cdc_line_coding_changed,
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));
    ESP_LOGI(TAG, "USB composite ready — CDC (SimHub) + HID gamepad");

    xTaskCreatePinnedToCore(ui_task,  "ui",  8192, NULL, 4, NULL, 1);
    xTaskCreate(led_task, "led", 4096, NULL, 5, NULL);
    xTaskCreate(hid_task, "hid", 3072, NULL, 6, NULL);
    xTaskCreate(sim_task, "sim", 3072, NULL, 3, NULL);

    vTaskDelay(pdMS_TO_TICKS(900));    // let the displays + LED strip come up
    run_selftest();

    // We booted and ran successfully — confirm this image so the rollback timer
    // (if we just OTA'd into it) doesn't revert us on the next reset.
    esp_ota_mark_app_valid_cancel_rollback();

    while (1) {
        // Recover from an abandoned OTA: if a transfer began but no bytes have
        // arrived for a few seconds (PC app crashed / cancelled mid-flash), abort
        // so the device leaves OTA mode and a fresh "@OTA" retry parses cleanly.
        if (s_ota_active && esp_timer_get_time() - s_ota_last_us > 4000000) {
            ESP_LOGW(TAG, "OTA timed out — aborting abandoned transfer");
            esp_ota_abort(s_ota_handle);
            s_ota_active = false;
        }
        if (s_ota_reboot) {
            ESP_LOGW(TAG, "OTA complete — rebooting into the new image");
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
        if (s_cfg_reboot) {
            ESP_LOGW(TAG, "pin layout changed — rebooting to apply");
            vTaskDelay(pdMS_TO_TICKS(300));   // let the OK reply flush first
            esp_restart();
        }
        if (s_reboot_to_dl) {
            ESP_LOGW(TAG, "1200-baud touch — rebooting into download mode");
            vTaskDelay(pdMS_TO_TICKS(100));
            REG_SET_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
