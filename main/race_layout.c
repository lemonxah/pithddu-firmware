#include "race_layout.h"
#include "field_registry_gen.h"   // field_id_from_str + FIELD_REGISTRY defaults

#include <string.h>
#include "cJSON.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "race_layout";
#define NVS_NS   "dash"
#define NVS_KEY  "race2"          // new key: the v1 module-spec blob (old "race" ignored)
#define NVS_KEY_JSON "racejson"   // raw @RS JSON, so the PC app can read the layout back

static race_layout_t s_layout;
// Parse scratch kept off the 8KB USB task stack (a full layout is ~1.5KB).
static race_layout_t s_parse;
// Last-pushed @RS JSON, kept verbatim so the app can read the active layout back
// (@RG) and render it — avoids serialising the compact POD back to JSON.
static char s_json[6144];

// kind string -> RK_* (must match the app's kind strings).
static const struct { const char *s; uint8_t k; } KINDMAP[] = {
    { "stat", RK_STAT }, { "gear", RK_GEAR }, { "gearSpeed", RK_GEARSPEED },
    { "rpmStrip", RK_RPMSTRIP }, { "tyreGrid", RK_TYREGRID }, { "tcDual", RK_TCDUAL },
    { "sectors", RK_SECTORS }, { "lapPair", RK_LAPPAIR }, { "bar", RK_BAR },
    { "map", RK_MAP }, { "flag", RK_FLAG }, { "position", RK_POSITION },
};

static uint8_t kind_from_str(const char *s)
{
    if (s) for (unsigned i = 0; i < sizeof(KINDMAP) / sizeof(KINDMAP[0]); i++)
        if (!strcmp(s, KINDMAP[i].s)) return KINDMAP[i].k;
    return RK_NONE;
}

static void copy_str(char *dst, int cap, const char *src)
{
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

// Parse one module spec object into `m`. Returns 1 on a usable module.
static int parse_module(const cJSON *o, race_mod_spec_t *m)
{
    memset(m, 0, sizeof(*m));
    m->kind  = kind_from_str(cJSON_GetStringValue(cJSON_GetObjectItem(o, "k")));
    if (m->kind == RK_NONE) return 0;

    m->field = field_id_from_str(cJSON_GetStringValue(cJSON_GetObjectItem(o, "f")));

    // Defaults inherited from the field registry, overridable per-module.
    const field_def_t *fd = &FIELD_REGISTRY[m->field];
    m->fmt_t = fd->fmt;
    m->scale = fd->scale;
    copy_str(m->label, RACE_LABEL_LEN, fd->label);
    m->base  = PAL_WHITE;

    const cJSON *l = cJSON_GetObjectItem(o, "l");
    if (cJSON_IsString(l)) copy_str(m->label, RACE_LABEL_LEN, l->valuestring);

    const cJSON *fmt = cJSON_GetObjectItem(o, "fmt");
    if (cJSON_IsObject(fmt)) {
        const cJSON *t  = cJSON_GetObjectItem(fmt, "t");
        const cJSON *u  = cJSON_GetObjectItem(fmt, "u");
        const cJSON *sc = cJSON_GetObjectItem(fmt, "sc");
        if (cJSON_IsString(t)) m->fmt_t = (uint8_t)fmtc_fmt_from_str(t->valuestring);
        if (cJSON_IsString(u)) copy_str(m->unit, RACE_UNIT_LEN, u->valuestring);
        if (cJSON_IsNumber(sc) && sc->valueint > 0) m->scale = (int16_t)sc->valueint;
    }

    const cJSON *b = cJSON_GetObjectItem(o, "b");
    if (cJSON_IsString(b)) m->base = (uint8_t)fmtc_pal_from_str(b->valuestring);

    const cJSON *sz = cJSON_GetObjectItem(o, "sz");
    if (cJSON_IsNumber(sz) && sz->valueint > 0 && sz->valueint <= 100)
        m->size_pct = (uint8_t)sz->valueint;

    const cJSON *r = cJSON_GetObjectItem(o, "r");
    if (cJSON_IsArray(r)) {
        int rn = cJSON_GetArraySize(r), k = 0;
        for (int i = 0; i < rn && k < RACE_MAX_RULES; i++) {
            const cJSON *e = cJSON_GetArrayItem(r, i);
            const cJSON *op = cJSON_GetObjectItem(e, "op");
            const cJSON *v  = cJSON_GetObjectItem(e, "v");
            const cJSON *c  = cJSON_GetObjectItem(e, "c");
            if (!cJSON_IsString(op) || !cJSON_IsString(c)) continue;
            m->rules[k].op    = (uint8_t)fmtc_op_from_str(op->valuestring);
            m->rules[k].color = (uint8_t)fmtc_pal_from_str(c->valuestring);
            m->rules[k].v     = cJSON_IsNumber(v) ? v->valueint : 0;
            k++;
        }
        m->rule_n = (uint8_t)k;
    }

    const cJSON *z = cJSON_GetObjectItem(o, "z");
    const cJSON *ord = cJSON_GetObjectItem(o, "o");
    m->zone  = cJSON_IsNumber(z) ? (uint8_t)z->valueint : 0;
    if (m->zone >= RZ_ZONES) m->zone = 0;
    m->order = cJSON_IsNumber(ord) ? (uint8_t)ord->valueint : 0;
    return 1;
}

// Default layout (built-in). Run through the same parser so default == parser.
static const char *DEFAULT_JSON =
    "{\"v\":1,\"mods\":["
      "{\"k\":\"rpmStrip\",\"z\":0,\"o\":0},"
      "{\"k\":\"stat\",\"f\":\"delta_ms\",\"b\":\"amber\",\"z\":1,\"o\":0,"
        "\"r\":[{\"op\":\"<\",\"v\":0,\"c\":\"green\"},{\"op\":\">\",\"v\":0,\"c\":\"red\"}]},"
      "{\"k\":\"position\",\"z\":1,\"o\":1},"
      "{\"k\":\"gearSpeed\",\"z\":2,\"o\":0},"
      "{\"k\":\"stat\",\"f\":\"fuel_dl\",\"fmt\":{\"u\":\"L\"},\"z\":3,\"o\":0},"
      "{\"k\":\"tyreGrid\",\"f\":\"tt_fl_m\",\"fmt\":{\"u\":\"\\u00b0\"},\"z\":3,\"o\":1},"
      "{\"k\":\"lapPair\",\"z\":4,\"o\":0}"
    "]}";

static int parse_into(race_layout_t *dst, const char *json, int len)
{
    cJSON *root = (len < 0) ? cJSON_Parse(json) : cJSON_ParseWithLength(json, len);
    if (!root) return -1;
    const cJSON *v = cJSON_GetObjectItem(root, "v");
    if (!cJSON_IsNumber(v) || v->valueint != RACE_SCHEMA_VERSION) { cJSON_Delete(root); return -1; }
    const cJSON *mods = cJSON_GetObjectItem(root, "mods");
    if (!cJSON_IsArray(mods)) { cJSON_Delete(root); return -1; }

    memset(dst, 0, sizeof(*dst));
    dst->version = RACE_SCHEMA_VERSION;
    int n = cJSON_GetArraySize(mods), k = 0;
    for (int i = 0; i < n && k < RACE_MAX_MODS; i++) {
        if (parse_module(cJSON_GetArrayItem(mods, i), &dst->mods[k])) k++;
    }
    dst->count = (uint8_t)k;
    dst->valid = 1;
    cJSON_Delete(root);
    return 0;
}

static void load_default(void)
{
    if (parse_into(&s_layout, DEFAULT_JSON, -1) != 0) {
        memset(&s_layout, 0, sizeof(s_layout));   // empty but valid as a last resort
        s_layout.version = RACE_SCHEMA_VERSION;
        s_layout.valid = 1;
    }
}

void race_layout_init(void)
{
    nvs_handle_t h;
    size_t sz = sizeof(s_layout);
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, NVS_KEY, &s_layout, &sz) == ESP_OK &&
            sz == sizeof(s_layout) && s_layout.valid &&
            s_layout.version == RACE_SCHEMA_VERSION) {
            nvs_close(h);
            ESP_LOGI(TAG, "restored race layout (%d modules)", s_layout.count);
            return;
        }
        nvs_close(h);
    }
    load_default();
    ESP_LOGI(TAG, "default race layout (%d modules)", s_layout.count);

    // Restore the raw JSON for read-back (separate key; absence is fine).
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t jl = sizeof(s_json);
        if (nvs_get_str(h, NVS_KEY_JSON, s_json, &jl) != ESP_OK) s_json[0] = '\0';
        nvs_close(h);
    }
}

const race_layout_t *race_layout_get(void) { return &s_layout; }
const char *race_layout_json(void) { return s_json; }

int race_layout_load_json(const char *json, int len)
{
    if (parse_into(&s_parse, json, len) != 0) return -1;
    s_layout = s_parse;

    // Keep the raw JSON verbatim (for @RG read-back), and persist both.
    int n = len < 0 ? (int)strlen(json) : len;
    if (n > (int)sizeof(s_json) - 1) n = (int)sizeof(s_json) - 1;
    memcpy(s_json, json, n); s_json[n] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY, &s_layout, sizeof(s_layout));
        nvs_set_str(h, NVS_KEY_JSON, s_json);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "race layout updated (%d modules)", s_layout.count);
    return 0;
}
