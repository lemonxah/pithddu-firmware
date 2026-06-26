#include "ui_config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "field_registry_gen.h"   // field_id_from_str for button sync-from-game

static const char *CFG_TAG = "ui_config";
#define NVS_NS       "dash"
#define NVS_KEY      "profile"
#define NVS_KEY_BTNS "btns_json"   // raw @BS JSON — survives struct layout changes

#include <stdbool.h>
static int apply_buttons_json(const char *json, int len, bool persist);

// RGB565 accents (match the dash palette).
#define C_AMBER  0xF587
#define C_CYAN   0x3E3C
#define C_GREEN  0x370E
#define C_RED    0xF1C5
#define C_WHITE  0xEF9E

// Default profile for LMU / rF2. Two button pages on Display 2:
//  - "FUNC1": driving functions (lights, wipers, limiter, ignition, start, ...)
//  - "FUNC2": adjusters (TC / ABS / brake-bias / engine-map up & down)
// HID button numbers are what you bind in-game. The PC app can replace this.
static ui_profile_t s_profile = {
    .game = "LMU / rF2",
    .rev = REV_CFG_DEFAULT,
    .page_count = 2,
    .pages = {
        {
            .name = "FUNC1",
            .count = 7,
            .buttons = {
                {  12,  12, 148, 125, C_AMBER, UI_ACT_HID,  8, "LIGHTS" },
                { 166,  12, 148, 125, C_CYAN,  UI_ACT_HID,  9, "FLASH" },
                { 320,  12, 148, 125, C_WHITE, UI_ACT_HID, 10, "WIPERS" },
                {  12, 148, 110, 125, C_GREEN, UI_ACT_HID, 11, "LIMITER" },
                { 128, 148, 110, 125, C_WHITE, UI_ACT_HID, 12, "IGNITION" },
                { 244, 148, 110, 125, C_GREEN, UI_ACT_HID, 14, "START" },
                { 360, 148, 108, 125, C_CYAN,  UI_ACT_HID, 13, "HYBRID" },
            },
        },
        {
            .name = "FUNC2",
            .count = 8,
            .buttons = {
                {  12,  12, 110, 125, C_AMBER, UI_ACT_HID, 15, "TC+" },
                { 128,  12, 110, 125, C_CYAN,  UI_ACT_HID, 16, "ABS+" },
                { 244,  12, 110, 125, C_GREEN, UI_ACT_HID, 17, "BIAS+" },
                { 360,  12, 108, 125, C_WHITE, UI_ACT_HID, 18, "MAP+" },
                {  12, 148, 110, 125, C_AMBER, UI_ACT_HID, 19, "TC-" },
                { 128, 148, 110, 125, C_CYAN,  UI_ACT_HID, 20, "ABS-" },
                { 244, 148, 110, 125, C_GREEN, UI_ACT_HID, 21, "BIAS-" },
                { 360, 148, 108, 125, C_WHITE, UI_ACT_HID, 22, "MAP-" },
            },
        },
    },
};

const ui_profile_t *ui_active_profile(void)
{
    return &s_profile;
}

static void cfg_save_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY, &s_profile, sizeof(s_profile));
    nvs_commit(h);
    nvs_close(h);
}

void ui_config_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;   // keep default
    ui_profile_t tmp;
    size_t sz = sizeof(tmp);
    esp_err_t err = nvs_get_blob(h, NVS_KEY, &tmp, &sz);
    nvs_close(h);
    if (err == ESP_OK && sz == sizeof(tmp) && tmp.page_count <= UI_PROFILE_PG) {
        memcpy(&s_profile, &tmp, sizeof(s_profile));
        ESP_LOGI(CFG_TAG, "loaded profile '%s' from NVS", s_profile.game);
    }

    // Replay the stored @BS button JSON over whatever we have. This survives a
    // firmware update that changes ui_profile_t's layout (the binary blob above
    // fails its size check and is dropped, but the text payload re-parses fine).
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t jlen = 0;
        if (nvs_get_str(h, NVS_KEY_BTNS, NULL, &jlen) == ESP_OK && jlen > 1) {
            char *jbuf = (char *)malloc(jlen);
            if (jbuf && nvs_get_str(h, NVS_KEY_BTNS, jbuf, &jlen) == ESP_OK) {
                if (apply_buttons_json(jbuf, -1, false) == 0)
                    ESP_LOGI(CFG_TAG, "replayed stored button pages");
            }
            free(jbuf);
        }
        nvs_close(h);
    }

    led_rev_set_config(&s_profile.rev);   // apply shift-light config (or default)
}

static uint16_t jget(const cJSON *o, const char *k, int def)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? (uint16_t)v->valueint : (uint16_t)def;
}

static uint32_t jgu(const cJSON *o, const char *k, uint32_t def)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? (uint32_t)v->valuedouble : def;
}

// Parse a JSON profile (see README for the schema) into a temp, validate,
// then commit + persist. Never touches s_profile until fully validated.
int ui_load_profile_json(const char *json, int len)
{
    cJSON *root = (len < 0) ? cJSON_Parse(json) : cJSON_ParseWithLength(json, len);
    if (!root) return -1;

    // Static (not on the stack): this runs in the small USB callback task and a
    // ui_profile_t is ~1 KB. Single CDC client, so no reentrancy concern.
    static ui_profile_t p;
    memset(&p, 0, sizeof(p));
    const cJSON *game = cJSON_GetObjectItem(root, "game");
    if (cJSON_IsString(game) && game->valuestring)
        strncpy(p.game, game->valuestring, UI_GAME_MAX);

    const cJSON *pages = cJSON_GetObjectItem(root, "pages");
    int rc = 0;
    if (cJSON_IsArray(pages)) {
        int np = cJSON_GetArraySize(pages);
        if (np > UI_PROFILE_PG) { rc = -2; goto done; }
        p.page_count = (uint8_t)np;
        for (int i = 0; i < np; i++) {
            const cJSON *pg = cJSON_GetArrayItem(pages, i);
            const cJSON *nm = cJSON_GetObjectItem(pg, "name");
            if (cJSON_IsString(nm) && nm->valuestring)
                strncpy(p.pages[i].name, nm->valuestring, UI_LABEL_MAX);
            const cJSON *btns = cJSON_GetObjectItem(pg, "buttons");
            if (!cJSON_IsArray(btns)) continue;
            int nb = cJSON_GetArraySize(btns);
            if (nb > UI_PAGE_BTNS) { rc = -3; goto done; }
            p.pages[i].count = (uint8_t)nb;
            for (int j = 0; j < nb; j++) {
                const cJSON *b = cJSON_GetArrayItem(btns, j);
                ui_button_t *o = &p.pages[i].buttons[j];
                o->x = jget(b, "x", 0);
                o->y = jget(b, "y", 0);
                o->w = jget(b, "w", 60);
                o->h = jget(b, "h", 40);
                o->color = jget(b, "color", 0xFFFF);
                o->action = (uint8_t)jget(b, "action", UI_ACT_HID);
                o->param = (uint8_t)jget(b, "param", 0);
                const cJSON *lb = cJSON_GetObjectItem(b, "label");
                if (cJSON_IsString(lb) && lb->valuestring)
                    strncpy(o->label, lb->valuestring, UI_LABEL_MAX);
            }
        }
    }

    // Shift-light config (optional "rev" object; absent fields keep current).
    p.rev = s_profile.rev;
    const cJSON *rev = cJSON_GetObjectItem(root, "rev");
    if (cJSON_IsObject(rev)) {
        p.rev.start_pct = (uint8_t)jget(rev, "start", p.rev.start_pct);
        p.rev.flash_pct = (uint8_t)jget(rev, "flash", p.rev.flash_pct);
        p.rev.n_green   = (uint8_t)jget(rev, "green", p.rev.n_green);
        p.rev.n_red     = (uint8_t)jget(rev, "red",   p.rev.n_red);
        p.rev.n_blue    = (uint8_t)jget(rev, "blue",  p.rev.n_blue);
        p.rev.col_green = jgu(rev, "cgreen", p.rev.col_green);
        p.rev.col_red   = jgu(rev, "cred",   p.rev.col_red);
        p.rev.col_blue  = jgu(rev, "cblue",  p.rev.col_blue);
        p.rev.col_flash = jgu(rev, "cflash", p.rev.col_flash);
    }

    memcpy(&s_profile, &p, sizeof(s_profile));
    cfg_save_nvs();
    led_rev_set_config(&s_profile.rev);
    ESP_LOGI(CFG_TAG, "applied profile '%s' (%d pages)", s_profile.game, s_profile.page_count);
done:
    cJSON_Delete(root);
    return rc;
}

static uint16_t rgb888_to_565(uint32_t c)
{
    uint8_t r = c >> 16, g = c >> 8, b = c;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Persist the raw @BS JSON so button pages survive a firmware update that
// changes the ui_profile_t struct layout (the binary blob fails its size
// check on load, but this text payload is re-parsed by the new firmware).
static void cfg_save_buttons_json(const char *json, int len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (len < 0) {
        nvs_set_str(h, NVS_KEY_BTNS, json);
    } else {
        char *tmp = (char *)malloc((size_t)len + 1);
        if (tmp) {
            memcpy(tmp, json, (size_t)len);
            tmp[len] = '\0';
            nvs_set_str(h, NVS_KEY_BTNS, tmp);
            free(tmp);
        }
    }
    nvs_commit(h);
    nvs_close(h);
}

int ui_load_buttons_json(const char *json, int len)
{
    return apply_buttons_json(json, len, true);
}

static int apply_buttons_json(const char *json, int len, bool persist)
{
    cJSON *root = (len < 0) ? cJSON_Parse(json) : cJSON_ParseWithLength(json, len);
    if (!root) return -1;
    const cJSON *pages = cJSON_GetObjectItem(root, "pages");
    if (!cJSON_IsArray(pages)) { cJSON_Delete(root); return -1; }

    static ui_profile_t p;
    memcpy(&p, &s_profile, sizeof(p));   // keep game + shift config

    int np = cJSON_GetArraySize(pages);
    if (np > UI_PROFILE_PG) np = UI_PROFILE_PG;
    p.page_count = (uint8_t)np;

    // Auto 3x2 grid for the 480x320 side screen (tab bar ~32px at the bottom).
    const int W2 = 480, H2 = 320, M = 10, G = 8, TAB = 32;
    const int colW = (W2 - 2 * M - 2 * G) / 3;
    const int rowH = (H2 - TAB - 2 * M - G) / 2;
    for (int i = 0; i < np; i++) {
        const cJSON *pg = cJSON_GetArrayItem(pages, i);
        int nb = cJSON_IsArray(pg) ? cJSON_GetArraySize(pg) : 0;
        if (nb > UI_PAGE_BTNS) nb = UI_PAGE_BTNS;
        snprintf(p.pages[i].name, sizeof(p.pages[i].name), "PAGE %d", i + 1);
        p.pages[i].count = (uint8_t)nb;
        for (int j = 0; j < nb; j++) {
            const cJSON *b = cJSON_GetArrayItem(pg, j);
            ui_button_t *o = &p.pages[i].buttons[j];
            int row = j / 3, col = j % 3;
            o->x = M + col * (colW + G);
            o->y = M + row * (rowH + G);
            o->w = colW;
            o->h = rowH;
            o->action = UI_ACT_HID;
            o->param = (uint8_t)(i * 8 + j);     // unique HID button per tile
            const cJSON *kd = cJSON_GetObjectItem(b, "kind");
            o->toggle = (cJSON_IsString(kd) && kd->valuestring && strcmp(kd->valuestring, "toggle") == 0) ? 1 : 0;
            // sync-from-game: bind the toggle's lit state to a telemetry field.
            o->field = 0;
            const cJSON *sy = cJSON_GetObjectItem(b, "sync");
            const cJSON *fld = cJSON_GetObjectItem(b, "field");
            if (cJSON_IsTrue(sy) && cJSON_IsString(fld) && fld->valuestring)
                o->field = field_id_from_str(fld->valuestring);
            const cJSON *lb = cJSON_GetObjectItem(b, "label");
            if (cJSON_IsString(lb) && lb->valuestring)
                strncpy(o->label, lb->valuestring, UI_LABEL_MAX);
            uint32_t rgb = 0x00E5A0;
            const cJSON *cl = cJSON_GetObjectItem(b, "color");
            if (cJSON_IsString(cl) && cl->valuestring)
                rgb = strtoul(cl->valuestring, NULL, 16) & 0xFFFFFF;
            o->color = rgb888_to_565(rgb);
        }
    }
    memcpy(&s_profile, &p, sizeof(s_profile));
    cfg_save_nvs();
    if (persist) cfg_save_buttons_json(json, len);
    ESP_LOGI(CFG_TAG, "applied %d button pages via @BS", p.page_count);
    cJSON_Delete(root);
    return 0;
}
