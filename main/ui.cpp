#include "ui.hpp"
#include "lgfx_setup.hpp"

extern "C" {
#include "hid_gamepad.h"
#include "ui_config.h"
#include "led_rev.h"
#include "race_layout.h"
#include "field_registry_gen.h"
#include "device_pins.h"
void sim_set(bool on);   // input simulation toggle (simhub_main.c)
bool sim_get(void);
bool ui_ota_active(void);   // firmware update in progress (simhub_main.c)
int  ui_ota_pct(void);      // OTA progress 0..100
}

#include "esp_timer.h"
#include "esp_system.h"
#include <cstdio>
#include <cstring>

#define FW_VERSION "0.9.3"

// ---- palette (RGB565) ----
static constexpr uint16_t C_BG     = 0x0841;
static constexpr uint16_t C_PANEL  = 0x10A2;
static constexpr uint16_t C_WHITE  = 0xEF9E;
static constexpr uint16_t C_DIM    = 0x5B4E;
static constexpr uint16_t C_GREEN  = 0x370E;
static constexpr uint16_t C_PURPLE = 0xB2FE;
static constexpr uint16_t C_RED    = 0xF1C5;
static constexpr uint16_t C_AMBER  = 0xF587;
static constexpr uint16_t C_CYAN   = 0x3E3C;
static constexpr uint16_t C_BLUE   = 0x5D5E;
static constexpr uint16_t C_ACCENT = 0x0734;   // app accent #00E5A0 — press highlight

static constexpr int W = 480, H = 320;

// Two panels on the shared bus, each with its own framebuffer in PSRAM.
// Constructed in ui_init() from the runtime pin map (device_pins), so a
// PC-pushed pin layout takes effect on the next boot.
static LGFX_ST7796 *disp1 = nullptr;
static LGFX_ST7796 *disp2 = nullptr;
static LGFX_Sprite *fb1 = nullptr;
static LGFX_Sprite *fb2 = nullptr;
// Role pointers — which physical display shows the race screen vs the buttons,
// assigned from device_pins.race_screen in ui_init().
static LGFX_ST7796 *disp_race = nullptr, *disp_btn = nullptr;
static LGFX_Sprite *fb_race = nullptr, *fb_btn = nullptr;

// ---- page state ----
enum D1Page { D1_RACE = 0, D1_CONFIG };

// Brightness slider + reboot button geometry (shared by render + touch).
#define SLD_X 40
#define SLD_Y 150
#define SLD_W 400
#define SLD_H 40
#define RBT_X 260
#define RBT_Y 262
#define RBT_W 180
#define RBT_H 46
#define SIM_X 40
#define SIM_Y 262
#define SIM_W 180
#define SIM_H 46
// The side screen is the button box: 3 button pages.
enum D2Page { D2_P1 = 0, D2_P2, D2_P3, D2_PAGES };

static int s_d1 = D1_RACE;
static int s_d2 = D2_P1;

// Drive the 2nd display only when it's physically wired. With only Display 1
// connected, touching/pushing the absent panel on the shared SPI2 bus hangs
// the render task. Flip to true once Display 2 is wired.
static bool s_use_disp2 = true;

// Button press feedback (which button on which page, and when).
static int     s_press_btn = -1, s_press_page = -1;
static int64_t s_press_at = 0;
static bool    s_btn_held = false;     // a button is currently held (finger down)
// Latched toggle state per page/button (toggle buttons light up when on).
static bool    s_btn_on[UI_PROFILE_PG][UI_PAGE_BTNS] = { { false } };

// touch edge-debounce per display
static bool s_t1_down, s_t2_down;
static int64_t s_config_at = 0;   // last activity on CONFIG; auto-exits after 8s

// ---- self-learned track map ----
#define MAP_MAX 320
struct MapState {
    bool   learned = false;
    int    n = 0;
    int    last_lap = -1;
    int16_t px[MAP_MAX];
    int16_t pz[MAP_MAX];
    int    minx = 0, maxx = 0, minz = 0, maxz = 0;
};
static MapState s_map;

// ---- color logic (firmware-side thresholds) ----
static uint16_t tyre_temp_color(int c)
{
    if (c < 78)  return C_BLUE;
    if (c < 98)  return C_GREEN;
    if (c < 106) return C_AMBER;
    return C_RED;
}
static uint16_t wear_color(int p)
{
    if (p > 60) return C_GREEN;
    if (p >= 30) return C_AMBER;
    return C_RED;
}
static uint16_t brake_color(int c)
{
    if (c < 200) return C_BLUE;
    if (c < 350) return C_CYAN;
    if (c < 600) return C_GREEN;
    if (c < 720) return C_AMBER;
    return C_RED;
}
// Map a shared palette token to this display's RGB565 color.
static uint16_t palette_color(uint8_t tok)
{
    switch (tok) {
    case PAL_BG:    return C_BG;     case PAL_PANEL:  return C_PANEL;
    case PAL_WHITE: return C_WHITE;  case PAL_DIM:    return C_DIM;
    case PAL_GREEN: return C_GREEN;  case PAL_AMBER:  return C_AMBER;
    case PAL_RED:   return C_RED;    case PAL_CYAN:   return C_CYAN;
    case PAL_BLUE:  return C_BLUE;   case PAL_PURPLE: return C_PURPLE;
    default:        return C_WHITE;
    }
}

// 0xRRGGBB (from the shared rev-bar logic) -> RGB565 for the on-screen strip.
static uint16_t rgb565(uint32_t c)
{
    return (uint16_t)((((c >> 19) & 0x1F) << 11) | (((c >> 10) & 0x3F) << 5) | ((c >> 3) & 0x1F));
}

// Module color = base, overridden by the first matching design-language rule.
static uint16_t spec_color(const race_mod_spec_t *m, int iv)
{
    uint8_t tok = m->base;
    for (int i = 0; i < m->rule_n; i++)
        if (fmtc_rule_match(iv, m->rules[i].op, m->rules[i].v)) { tok = m->rules[i].color; break; }
    return palette_color(tok);
}

// ---- small drawing helpers ----
static void panel(LGFX_Sprite &fb, int x, int y, int w, int h)
{
    fb.fillRoundRect(x, y, w, h, 6, C_PANEL);
}
static void label(LGFX_Sprite &fb, const char *s, int x, int y)
{
    fb.setFont(&fonts::Font2);
    fb.setTextColor(C_DIM);
    fb.setTextDatum(textdatum_t::top_left);
    fb.drawString(s, x, y);
}

// ===================== DISPLAY 1 =====================
// ---- data-driven race screen (the layout language §race_layout) ----

// Monospace value fonts (mirror the app's mono data text), largest first.
static const lgfx::IFont *const k_mono[] = {
    &fonts::FreeMonoBold24pt7b, &fonts::FreeMonoBold18pt7b,
    &fonts::FreeMonoBold12pt7b, &fonts::FreeMonoBold9pt7b,
};

// Pick (and set) the largest monospace font whose rendering of `val` fits the box.
// Which dimension drives the auto-fit. Narrow rails fill the WIDTH (so values
// come out smaller); short/wide top & bottom strips and the big center fill the
// HEIGHT. The other axis is always clamped so text never overflows the box.
enum { FIT_BOTH = 0, FIT_WIDTH, FIT_HEIGHT };
static int zone_axis(int zone)
{
    return (zone == RZ_LEFT || zone == RZ_RIGHT) ? FIT_WIDTH : FIT_HEIGHT;
}

// Fit a monospace value into the box: pick the native size nearest the target,
// then fractionally scale it (LovyanGFX float setTextSize) along the driving
// axis, clamped by the other. `size_pct` (>0) forces a height target (% of box).
// Leaves the chosen size active — caller resets to 1.
static void fit_value_font(LGFX_Sprite &fb, const char *val, int w, int h, int size_pct, int axis)
{
    // Match the app preview's fractions: rails 30% of box height, others 42%
    // (explicit size_pct overrides). Always clamp by width so nothing overflows.
    float frac = size_pct > 0 ? size_pct / 100.0f : (axis == FIT_WIDTH ? 0.30f : 0.42f);
    float hTarget = h * frac;
    const lgfx::IFont *base = k_mono[3];            // smallest as fallback
    for (auto f : k_mono) {                         // largest-first
        fb.setFont(f); fb.setTextSize(1.0f);
        if (fb.fontHeight() <= hTarget) { base = f; break; }
    }
    fb.setFont(base); fb.setTextSize(1.0f);
    float nh = fb.fontHeight(); if (nh < 1) nh = 1;
    float nw = fb.textWidth(val); if (nw < 1) nw = 1;
    float sW = (float)(w - 6) / nw;                 // clamp to width
    float sH = hTarget / nh;                        // hit the height target
    float s = sW < sH ? sW : sH;
    if (s < 0.30f) s = 0.30f;
    if (s > 3.0f)  s = 3.0f;
    fb.setTextSize(s);
}

// A fixed widest-case string for a format, so the value size does NOT change as
// the data changes (e.g. "+0" stays the same size as "-9.999").
static void value_ref(int fmt_t, const char *unit, char *o, int n)
{
    switch (fmt_t) {
    case FMT_TIME:   snprintf(o, n, "0:00.000"); break;
    case FMT_SECTOR: snprintf(o, n, "00.000"); break;
    case FMT_DELTA:  snprintf(o, n, "-99.999"); break;
    case FMT_FIXED1: snprintf(o, n, "000.0%s", unit); break;
    case FMT_FIXED2: snprintf(o, n, "000.00%s", unit); break;
    default:         snprintf(o, n, "8888%s", unit); break;   // int
    }
}

// "LABEL over value" stat, matching the app's Stat component: centered label on
// top, monospace value below. `degree` appends a small ° ring; `size_pct` is the
// design-language text size (% of box height, 0 = auto-fit). `ref` is the
// widest-case string used for SIZING (so size is content-independent); when null
// the value itself is used.
static void mod_stat(LGFX_Sprite &fb, const char *lab, const char *val,
                     int x, int y, int w, int h, uint16_t col,
                     bool degree = false, int size_pct = 0, int axis = FIT_HEIGHT,
                     const char *ref = nullptr)
{
    float ls = h < 70 ? (h < 44 ? 0.7f : 0.85f) : 1.0f;   // shrink label in short boxes
    fb.setFont(&fonts::Font2);
    fb.setTextSize(ls);
    fb.setTextColor(C_DIM);
    fb.setTextDatum(textdatum_t::top_center);
    fb.drawString(lab, x + w / 2, y + 3);

    fit_value_font(fb, ref ? ref : val, w, h, size_pct, axis);
    fb.setTextColor(col);
    fb.setTextDatum(textdatum_t::middle_center);
    int cy = y + h / 2 + 7;
    fb.drawString(val, x + w / 2, cy);
    if (degree) {
        int tw = fb.textWidth(val), fh = fb.fontHeight();
        int r = fh / 9; if (r < 2) r = 2;
        fb.drawCircle(x + w / 2 + tw / 2 + r + 2, cy - fh / 3, r, col);
    }
    fb.setTextSize(1.0f);   // don't leak the scale to the next module
}

// 2x2 tyre grid. `wear` shows wear% per corner; otherwise mid-temp + a ° ring.
static void mod_tyres(LGFX_Sprite &fb, int x, int y, int w, int h,
                      const simhub_telemetry_t *t, bool wear)
{
    const int temp[4] = { t->tt_fl_m, t->tt_fr_m, t->tt_rl_m, t->tt_rr_m };
    const int wr[4]   = { t->tw_fl, t->tw_fr, t->tw_rl, t->tw_rr };
    int cw = (w - 6) / 2, ch = (h - 6) / 2;
    fb.setTextSize(1.0f);
    for (int i = 0; i < 4; i++) {
        int cx = x + (i % 2) * (cw + 6), cy = y + (i / 2) * (ch + 6);
        uint16_t col = wear ? wear_color(wr[i]) : tyre_temp_color(temp[i]);
        fb.fillRoundRect(cx, cy, cw, ch, 4, C_PANEL);
        fb.drawRoundRect(cx, cy, cw, ch, 4, col);
        char b[8];
        if (wear) snprintf(b, sizeof(b), "%d%%", wr[i]);
        else      snprintf(b, sizeof(b), "%d", temp[i]);
        fb.setFont(&fonts::Font2);
        fb.setTextColor(col);
        fb.setTextDatum(textdatum_t::middle_center);
        int ccx = cx + cw / 2, ccy = cy + ch / 2;
        fb.drawString(b, ccx, ccy);
        if (!wear) {
            int tw = fb.textWidth(b);
            fb.drawCircle(ccx + tw / 2 + 3, ccy - fb.fontHeight() / 3, 2, col);
        }
    }
}

// The big 7-segment fonts (Font7/Font8) have NO letters, so 'N'/'R' render blank.
// Draw digit gears with the big 7-seg font and letter gears with a proportional
// font (Font4 = FreeSans) scaled up so neutral/reverse actually show.
static void draw_gear(LGFX_Sprite &fb, char gc, int cx, int cy)
{
    fb.setTextDatum(textdatum_t::middle_center);
    fb.setTextColor(C_WHITE);
    bool isdig = (gc >= '1' && gc <= '9');
    if (isdig) {
        fb.setFont(&fonts::Font8);
        fb.setTextSize(0.44f);          // 50% smaller gear
    } else {
        if (gc < 32) gc = 'N';          // empty/garbage -> neutral
        fb.setFont(&fonts::Font4);
        fb.setTextSize(1.85f);          // 50% smaller gear (FreeSans caps)
    }
    char g[2] = { gc, 0 };
    fb.drawString(g, cx, cy);
    fb.setTextSize(1);
}

// The unit string is UTF-8 "°" (0xC2 0xB0) — the monospace device font has no
// degree glyph, so mod_stat draws a small ring instead when this is set.
static bool unit_is_degree(const char *u)
{
    return (unsigned char)u[0] == 0xC2 && (unsigned char)u[1] == 0xB0;
}

// Generic module interpreter: render any module from its uploaded spec.
static void draw_module(LGFX_Sprite &fb, const race_mod_spec_t *m,
                        int x, int y, int w, int h, const simhub_telemetry_t *t)
{
    char b[28];
    switch (m->kind) {
    case RK_STAT: {
        int iv = field_value(t, m->field);
        bool deg = unit_is_degree(m->unit);
        fmtc_format(iv, m->fmt_t, m->scale, deg ? "" : m->unit, b, sizeof(b));
        char rb[16]; value_ref(m->fmt_t, deg ? "" : m->unit, rb, sizeof(rb));
        mod_stat(fb, m->label, b, x, y, w, h, spec_color(m, iv), deg, m->size_pct, zone_axis(m->zone), rb);
        break;
    }
    case RK_BAR: {
        int iv = field_value(t, m->field);
        int pct = m->scale > 1 ? iv / m->scale : iv;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        label(fb, m->label, x, y);
        int by = y + 18, bh = h - 22;
        if (bh < 8) { by = y + h / 2 - 4; bh = 8; }
        fb.fillRoundRect(x, by, w, bh, 3, C_PANEL);
        fb.fillRoundRect(x, by, w * pct / 100, bh, 3, spec_color(m, iv));
        break;
    }
    case RK_GEARSPEED: {
        draw_gear(fb, t->gear, x + w / 2, y + h * 3 / 10);     // gear nudged up (was 2/5)
        snprintf(b, sizeof(b), "%d", t->speed_kmh);
        fit_value_font(fb, "888", w, h, 34, FIT_HEIGHT);       // bigger speedo (~34% of box height)
        fb.setTextColor(C_WHITE);
        fb.setTextDatum(textdatum_t::middle_center);
        fb.drawString(b, x + w / 2, y + h * 16 / 25);          // speed moved up (was 3/4)
        fb.setTextSize(1.0f);
        fb.setFont(&fonts::Font2);
        fb.setTextColor(C_DIM);
        fb.setTextDatum(textdatum_t::top_center);
        fb.drawString("KM/H", x + w / 2, y + h * 21 / 25);     // KM/H up, closer to the speed (was h-15)
        break;
    }
    case RK_GEAR:
        draw_gear(fb, t->gear, x + w / 2, y + h / 2);
        break;
    case RK_RPMSTRIP: {
        // Same per-LED color as the physical strip (shift config / car + flash).
        int bw = (w - 11 * 3) / 12;
        int bh = h / 2;
        if (bh < 10) bh = 10;
        if (bh > 48) bh = 48;
        int by = y + (h - bh) / 2;
        for (int i = 0; i < 12; i++) {
            uint32_t rgb = led_rev_segment_rgb(t, i, 12);
            uint16_t col = rgb ? rgb565(rgb) : C_PANEL;
            fb.fillRoundRect(x + i * (bw + 3), by, bw, bh, 3, col);
        }
        break;
    }
    case RK_POSITION: {
        uint8_t fid = m->field ? m->field : (uint8_t)FIELD_POSITION;
        int iv = field_value(t, fid);
        snprintf(b, sizeof(b), "P%d/%d", t->position, t->field_size);
        mod_stat(fb, m->label[0] ? m->label : "POS", b, x, y, w, h, spec_color(m, iv), false, m->size_pct, zone_axis(m->zone), "P88/88");
        break;
    }
    case RK_LAPPAIR: {
        int hh = h / 2;
        const char *labs[2] = { "CURRENT", "BEST" };
        uint16_t cols[2] = { C_WHITE, C_CYAN };
        int times[2] = { t->cur_lap_ms, t->best_lap_ms };
        // ~2x the previous time size (clamped to width), text nudged up so both
        // the label and the big time fit and stay readable.
        int frac = m->size_pct > 0 ? m->size_pct : 115;  // bigger lap times (easier to read)
        for (int k = 0; k < 2; k++) {
            int yy = y + k * hh;
            fmtc_format(times[k], FMT_TIME, 1, "", b, sizeof(b));
            fb.setFont(&fonts::Font2);
            fb.setTextSize(0.6f);                         // smaller label frees room for the time
            fb.setTextColor(C_DIM);
            fb.setTextDatum(textdatum_t::top_left);
            fb.drawString(labs[k], x + 3, yy + 1);
            fb.setTextSize(1.0f);
            fit_value_font(fb, "0:00.000", w, hh, frac, FIT_HEIGHT);
            fb.setTextColor(cols[k]);
            fb.setTextDatum(textdatum_t::middle_center);
            fb.drawString(b, x + w / 2, yy + hh / 2 + 2);
            fb.setTextSize(1.0f);
        }
        break;
    }
    case RK_SECTORS: {
        const int sc[3] = { t->s1_ms, t->s2_ms, t->s3_ms };
        const int bsc[3] = { t->bs1_ms, t->bs2_ms, t->bs3_ms };
        int cw = (w - 8) / 3;
        for (int i = 0; i < 3; i++) {
            int sx = x + i * (cw + 4);
            fb.fillRoundRect(sx, y, cw, h, 4, C_PANEL);
            char sl[4]; snprintf(sl, sizeof(sl), "S%d", i + 1);
            label(fb, sl, sx + 5, y + 3);
            uint16_t col = (sc[i] > 0 && bsc[i] > 0) ? (sc[i] <= bsc[i] ? C_GREEN : C_AMBER) : C_WHITE;
            fb.setFont(&fonts::Font2);
            fb.setTextColor(col);
            fb.setTextDatum(textdatum_t::middle_center);
            fmtc_format(sc[i], FMT_SECTOR, 1, "", b, sizeof(b));
            fb.drawString(b, sx + cw / 2, y + h / 2 + 6);
        }
        break;
    }
    case RK_TCDUAL: {
        int hw = w / 2;
        snprintf(b, sizeof(b), "%d", t->tc);
        mod_stat(fb, "TC", b, x, y, hw, h, C_WHITE, false, m->size_pct, zone_axis(m->zone), "88");
        snprintf(b, sizeof(b), "%d", t->abs);
        mod_stat(fb, "ABS", b, x + hw, y, hw, h, C_WHITE, false, m->size_pct, zone_axis(m->zone), "88");
        break;
    }
    case RK_TYREGRID:
        mod_tyres(fb, x, y, w, h, t, m->unit[0] == '%');
        break;
    case RK_FLAG: {
        uint16_t fc = m->base == PAL_WHITE ? C_GREEN : palette_color(m->base);
        fb.fillRoundRect(x, y, w, h, 6, fc);
        break;
    }
    case RK_MAP: {
        int rr = (w < h ? w : h) * 7 / 20;
        int cx = x + w / 2, cy = y + h / 2;
        fb.drawCircle(cx, cy, rr, C_DIM);
        int dr = rr / 5; if (dr < 2) dr = 2;
        fb.fillCircle(cx + rr * 2 / 5, cy - rr * 3 / 5, dr, C_CYAN);
        break;
    }
    default:
        break;
    }
}

// Lay this zone's modules (ordered by `order`) across a rect: horizontal
// (top/bottom strips) or vertical (rails / center), splitting the space evenly.
static void draw_zone(LGFX_Sprite &fb, const race_layout_t *l, int zone,
                      int x, int y, int w, int h, bool horizontal,
                      const simhub_telemetry_t *t)
{
    int idx[RACE_MAX_MODS], n = 0;
    for (int i = 0; i < l->count; i++)
        if (l->mods[i].zone == zone) idx[n++] = i;
    if (n <= 0) return;
    for (int i = 1; i < n; i++) {     // insertion sort by order
        int v = idx[i], j = i - 1;
        while (j >= 0 && l->mods[idx[j]].order > l->mods[v].order) { idx[j + 1] = idx[j]; j--; }
        idx[j + 1] = v;
    }
    for (int i = 0; i < n; i++) {
        int mx, my, mw, mh;
        if (horizontal) { mw = w / n; mh = h; mx = x + i * mw; my = y; }
        else            { mw = w; mh = h / n; mx = x; my = y + i * mh; }
        draw_module(fb, &l->mods[idx[i]], mx + 2, my + 2, mw - 4, mh - 4, t);
    }
}

static void render_race(LGFX_Sprite &fb, const simhub_telemetry_t *t)
{
    fb.fillScreen(C_BG);
    const race_layout_t *l = race_layout_get();

    // Regions on the 480x320 main display.
    draw_zone(fb, l, RZ_TOP,    0,   2,   W,   42, true,  t);
    draw_zone(fb, l, RZ_LEFT,   4,  50, 128, 220, false, t);
    draw_zone(fb, l, RZ_CENTER, 136, 50, 208, 220, false, t);
    draw_zone(fb, l, RZ_RIGHT, 348, 50, 128, 220, false, t);
    draw_zone(fb, l, RZ_BOTTOM,  0, 276,   W,  42, true,  t);
}

static void map_record(const simhub_telemetry_t *t)
{
    if (s_map.learned) return;
    if (s_map.last_lap < 0) s_map.last_lap = t->laps_done;

    int16_t x = (int16_t)t->pos_x, z = (int16_t)t->pos_z;
    if (s_map.n == 0 ||
        abs(x - s_map.px[s_map.n - 1]) > 3 || abs(z - s_map.pz[s_map.n - 1]) > 3) {
        if (s_map.n < MAP_MAX) {
            s_map.px[s_map.n] = x;
            s_map.pz[s_map.n] = z;
            if (s_map.n == 0) { s_map.minx = s_map.maxx = x; s_map.minz = s_map.maxz = z; }
            if (x < s_map.minx) s_map.minx = x;
            if (x > s_map.maxx) s_map.maxx = x;
            if (z < s_map.minz) s_map.minz = z;
            if (z > s_map.maxz) s_map.maxz = z;
            s_map.n++;
        }
    }
    // freeze once a lap completes (or buffer full)
    if ((t->laps_done > s_map.last_lap && s_map.n > 30) || s_map.n >= MAP_MAX)
        s_map.learned = true;
}

static void render_map(LGFX_Sprite &fb, const simhub_telemetry_t *t)
{
    fb.fillScreen(C_BG);
    label(fb, "TRACK", 16, 10);

    int spanx = s_map.maxx - s_map.minx; if (spanx < 1) spanx = 1;
    int spanz = s_map.maxz - s_map.minz; if (spanz < 1) spanz = 1;
    const int M = 30, vw = W - 2 * M, vh = H - 2 * M;
    float sc = (float)vw / spanx;
    float scz = (float)vh / spanz;
    if (scz < sc) sc = scz;
    int ox = M + (vw - (int)(spanx * sc)) / 2;
    int oy = M + (vh - (int)(spanz * sc)) / 2;

    auto tx = [&](int x){ return ox + (int)((x - s_map.minx) * sc); };
    auto tz = [&](int z){ return oy + (int)((z - s_map.minz) * sc); };

    if (s_map.n > 1) {
        for (int i = 1; i < s_map.n; i++)
            fb.drawLine(tx(s_map.px[i-1]), tz(s_map.pz[i-1]),
                        tx(s_map.px[i]),   tz(s_map.pz[i]), C_DIM);
        if (s_map.learned)   // close the loop
            fb.drawLine(tx(s_map.px[s_map.n-1]), tz(s_map.pz[s_map.n-1]),
                        tx(s_map.px[0]), tz(s_map.pz[0]), C_DIM);
    } else {
        fb.setTextColor(C_DIM);
        fb.setTextDatum(textdatum_t::middle_center);
        fb.setFont(&fonts::Font2);
        fb.drawString("learning track...", W/2, H/2);
    }
    // current car dot
    fb.fillCircle(tx(t->pos_x), tz(t->pos_z), 5, C_CYAN);
}

// ---- DISPLAY 1: CONFIG (replaces the map for now) ----
static void render_config(LGFX_Sprite &fb)
{
    fb.fillScreen(C_BG);
    // back button (top-left)
    fb.fillRoundRect(20, 16, 120, 46, 6, C_PANEL);
    fb.drawRoundRect(20, 16, 120, 46, 6, C_CYAN);
    fb.setFont(&fonts::Font2);
    fb.setTextColor(C_CYAN);
    fb.setTextDatum(textdatum_t::middle_center);
    fb.drawString("< RACE", 80, 39);
    fb.setTextColor(C_DIM);
    fb.drawString("CONFIG", W / 2, 39);
    // firmware version (top-right)
    fb.setTextDatum(textdatum_t::middle_right);
    fb.drawString("FW v" FW_VERSION, W - 16, 39);

    // rev-counter brightness slider (tap anywhere on the track to set)
    int pct = led_rev_get_brightness();
    label(fb, "REV COUNTER BRIGHTNESS", SLD_X, SLD_Y - 28);
    fb.fillRoundRect(SLD_X, SLD_Y, SLD_W, SLD_H, 6, C_PANEL);
    fb.fillRoundRect(SLD_X, SLD_Y, SLD_W * pct / 100, SLD_H, 6, C_CYAN);
    int kx = SLD_X + SLD_W * pct / 100;
    fb.fillCircle(kx, SLD_Y + SLD_H / 2, SLD_H / 2 + 2, C_WHITE);
    fb.setFont(&fonts::Font4);
    fb.setTextColor(C_WHITE);
    fb.setTextDatum(textdatum_t::middle_center);
    char b[8]; snprintf(b, sizeof(b), "%d%%", pct);
    fb.drawString(b, W / 2, SLD_Y + 70);

    // simulate-inputs toggle (left)
    bool sim = sim_get();
    fb.fillRoundRect(SIM_X, SIM_Y, SIM_W, SIM_H, 6, sim ? C_GREEN : C_PANEL);
    fb.drawRoundRect(SIM_X, SIM_Y, SIM_W, SIM_H, 6, C_GREEN);
    fb.setFont(&fonts::Font4);
    fb.setTextColor(sim ? C_BG : C_GREEN);
    fb.setTextDatum(textdatum_t::middle_center);
    fb.drawString(sim ? "SIM: ON" : "SIM: OFF", SIM_X + SIM_W / 2, SIM_Y + SIM_H / 2);

    // reboot button (right)
    fb.fillRoundRect(RBT_X, RBT_Y, RBT_W, RBT_H, 6, C_PANEL);
    fb.drawRoundRect(RBT_X, RBT_Y, RBT_W, RBT_H, 6, C_AMBER);
    fb.setTextColor(C_AMBER);
    fb.drawString("REBOOT", RBT_X + RBT_W / 2, RBT_Y + RBT_H / 2);
}

// ===================== DISPLAY 2 =====================
// Number of button pages the active profile defines (1..UI_PROFILE_PG).
static int d2_page_count(void)
{
    int n = ui_active_profile()->page_count;
    if (n < 1) n = 1;
    if (n > UI_PROFILE_PG) n = UI_PROFILE_PG;
    return n;
}

#define TABH 32     // page-tab strip height (at the TOP, matching the app)

static void tabbar(LGFX_Sprite &fb, int active)
{
    int n = d2_page_count();
    int tw = W / n;
    char nm[16];
    for (int i = 0; i < n; i++) {
        fb.fillRect(i * tw, 0, tw - 2, TABH, i == active ? C_PANEL : C_BG);
        if (i == active) fb.fillRect(i * tw, TABH - 3, tw - 2, 3, C_ACCENT);   // accent underline
        fb.setFont(&fonts::Font2);
        fb.setTextColor(i == active ? C_WHITE : C_DIM);
        fb.setTextDatum(textdatum_t::middle_center);
        snprintf(nm, sizeof(nm), "P%d", i + 1);
        fb.drawString(nm, i * tw + tw / 2, TABH / 2);
    }
}

static void corner_box(LGFX_Sprite &fb, int x, int y, const char *nm,
                       int ti, int tm, int to, int press, int wear)
{
    char b[24];
    panel(fb, x, y, 220, 120);
    label(fb, nm, x + 10, y + 6);
    // 3 temp zones
    const int zt[3] = { ti, tm, to };
    for (int i = 0; i < 3; i++) {
        fb.fillRoundRect(x + 10 + i * 50, y + 34, 44, 30, 3, tyre_temp_color(zt[i]));
        fb.setFont(&fonts::Font2);
        fb.setTextColor(C_BG);
        fb.setTextDatum(textdatum_t::middle_center);
        fb.drawNumber(zt[i], x + 10 + i * 50 + 22, y + 49);
    }
    // pressure + wear
    fb.setTextDatum(textdatum_t::top_left);
    fb.setFont(&fonts::Font2);
    fb.setTextColor(C_WHITE);
    snprintf(b, sizeof(b), "%d.%02d bar", press / 100, press % 100);
    fb.drawString(b, x + 10, y + 74);
    fb.setTextColor(wear_color(wear));
    snprintf(b, sizeof(b), "wear %d%%", wear);
    fb.drawString(b, x + 10, y + 96);
}

static void render_tyres(LGFX_Sprite &fb, const simhub_telemetry_t *t)
{
    fb.fillScreen(C_BG);
    corner_box(fb,  16,  8, "FL", t->tt_fl_i, t->tt_fl_m, t->tt_fl_o, t->tp_fl, t->tw_fl);
    corner_box(fb, 244,  8, "FR", t->tt_fr_i, t->tt_fr_m, t->tt_fr_o, t->tp_fr, t->tw_fr);
    corner_box(fb,  16, 150, "RL", t->tt_rl_i, t->tt_rl_m, t->tt_rl_o, t->tp_rl, t->tw_rl);
    corner_box(fb, 244, 150, "RR", t->tt_rr_i, t->tt_rr_m, t->tt_rr_o, t->tp_rr, t->tw_rr);
    tabbar(fb, s_d2);
}

static void brake_box(LGFX_Sprite &fb, int x, int y, const char *nm, int c)
{
    panel(fb, x, y, 220, 80);
    label(fb, nm, x + 10, y + 6);
    fb.fillRoundRect(x + 10, y + 46, 200, 22, 3, C_PANEL);
    int v = c * 200 / 850; if (v < 0) v = 0; if (v > 200) v = 200;
    fb.fillRoundRect(x + 10, y + 46, v, 22, 3, brake_color(c));
    fb.setFont(&fonts::Font4);
    fb.setTextColor(brake_color(c));
    fb.setTextDatum(textdatum_t::top_right);
    fb.drawNumber(c, x + 200, y + 8);
}

static void render_brakes(LGFX_Sprite &fb, const simhub_telemetry_t *t)
{
    fb.fillScreen(C_BG);
    brake_box(fb,  16,   8, "FL", t->bt_fl);
    brake_box(fb, 244,   8, "FR", t->bt_fr);
    brake_box(fb,  16,  96, "RL", t->bt_rl);
    brake_box(fb, 244,  96, "RR", t->bt_rr);
    // engine line
    char b[24];
    panel(fb, 16, 188, 448, 64);
    fb.setFont(&fonts::Font4);
    fb.setTextDatum(textdatum_t::middle_left);
    snprintf(b, sizeof(b), "H2O %dC", t->water_c);
    fb.setTextColor(t->water_c > 108 ? C_RED : t->water_c > 103 ? C_AMBER : C_GREEN);
    fb.drawString(b, 30, 220);
    snprintf(b, sizeof(b), "OIL %dC", t->oil_c);
    fb.setTextColor(t->oil_c > 120 ? C_RED : t->oil_c > 112 ? C_AMBER : C_WHITE);
    fb.drawString(b, 250, 220);
    tabbar(fb, s_d2);
}

// Config-driven button page (index into the active profile).
static void render_buttons(LGFX_Sprite &fb, int profile_page, const simhub_telemetry_t *t)
{
    fb.fillScreen(C_BG);
    const ui_profile_t *p = ui_active_profile();
    if (profile_page < p->page_count) {
        const ui_button_page_t *pg = &p->pages[profile_page];
        fb.setFont(&fonts::Font4);
        fb.setTextDatum(textdatum_t::middle_center);
        for (int i = 0; i < pg->count; i++) {
            const ui_button_t *bt = &pg->buttons[i];
            int by = bt->y + TABH;     // buttons sit below the top tab strip
            bool held = s_btn_held && s_press_page == profile_page && s_press_btn == i;
            // Sync-from-game toggles mirror a telemetry field; others use the local latch.
            bool on   = bt->toggle && (bt->field ? (field_value(t, bt->field) != 0)
                                                  : s_btn_on[profile_page][i]);
            // held -> accent (finger down); toggled-on -> full colour; toggle-off ->
            // a dim tint of its colour (so toggles always read as stateful); push -> panel.
            uint16_t dim = (uint16_t)((bt->color >> 1) & 0x7BEF);   // ~half-brightness RGB565
            uint16_t bg = held ? C_ACCENT : (on ? bt->color : (bt->toggle ? dim : C_PANEL));
            fb.fillRoundRect(bt->x, by, bt->w, bt->h, 8, bg);
            fb.drawRoundRect(bt->x, by, bt->w, bt->h, 8, bt->color);
            fb.setTextColor((held || on) ? C_BG : bt->color);
            fb.drawString(bt->label, bt->x + bt->w / 2, by + bt->h / 2);
        }
    }
    tabbar(fb, s_d2);
}

// ---- touch handling ----
static void touch_d2(int x, int y)
{
    if (y < TABH) {                          // tab bar (top)
        int n = d2_page_count();
        int tw = W / n;
        int tab = x / tw;
        if (tab >= 0 && tab < n) s_d2 = tab;
        return;
    }
    // button pages: hit-test profile buttons (authored coords are below the tab)
    int pp = s_d2;   // each side-screen tab is a button page
    if (pp < 0) return;
    const ui_profile_t *p = ui_active_profile();
    if (pp >= p->page_count) return;
    const ui_button_page_t *pg = &p->pages[pp];
    int yb = y - TABH;
    for (int i = 0; i < pg->count; i++) {
        const ui_button_t *bt = &pg->buttons[i];
        if (x >= bt->x && x < bt->x + bt->w && yb >= bt->y && yb < bt->y + bt->h) {
            if (bt->action == UI_ACT_HID)
                hid_button_pulse(bt->param);            // momentary tap
            else if (bt->action == UI_ACT_HID_HOLD)
                hid_button_set(bt->param, true);        // press now, release on lift
            if (bt->toggle && pp < UI_PROFILE_PG && i < UI_PAGE_BTNS)
                s_btn_on[pp][i] = !s_btn_on[pp][i];     // latch toggle state
            s_press_btn = i;
            s_press_page = pp;
            s_press_at = esp_timer_get_time();
            s_btn_held = true;                          // highlight while held
            return;
        }
    }
}

void ui_init(void)
{
    // Build the displays from the runtime pin map (loaded from NVS at boot).
    const device_pins_t *p = device_pins_get();
    disp1 = new LGFX_ST7796(p->sclk, p->mosi, p->miso, p->dc, p->disp1_cs, p->touch1_cs);
    fb1 = new LGFX_Sprite(disp1);

    disp1->init();
    disp1->setRotation(1);
    fb1->setPsram(true); fb1->setColorDepth(16); fb1->createSprite(W, H);

    if (s_use_disp2) {
        disp2 = new LGFX_ST7796(p->sclk, p->mosi, p->miso, p->dc, p->disp2_cs, p->touch2_cs);
        fb2 = new LGFX_Sprite(disp2);
        disp2->init();
        disp2->setRotation(1);
        fb2->setPsram(true); fb2->setColorDepth(16); fb2->createSprite(W, H);
        fb2->fillScreen(C_BG);
        fb2->pushSprite(0, 0);
    }

    fb1->fillScreen(C_BG);
    fb1->setTextColor(C_CYAN);
    fb1->setTextDatum(textdatum_t::middle_center);
    fb1->setFont(&fonts::Font4);
    fb1->drawString("SimHub Dash", W / 2, H / 2);
    fb1->pushSprite(0, 0);

    // Assign display roles. With one display, race always owns it.
    if (p->race_screen == 1 && s_use_disp2) {
        disp_race = disp2; fb_race = fb2; disp_btn = disp1; fb_btn = fb1;
    } else {
        disp_race = disp1; fb_race = fb1; disp_btn = disp2; fb_btn = fb2;
    }
}

// Full-screen "updating firmware" panel with a progress bar, shown on both
// displays while an OTA is in flight so the user knows not to disconnect.
static void render_ota(LGFX_Sprite &fb, int pct)
{
    fb.fillScreen(C_BG);
    fb.setTextDatum(textdatum_t::middle_center);
    fb.setFont(&fonts::Font4);
    fb.setTextColor(C_ACCENT);
    fb.drawString("FIRMWARE UPDATE", 240, 92);
    fb.setFont(&fonts::Font2);
    fb.setTextColor(C_WHITE);
    fb.drawString("Flashing — do not disconnect", 240, 128);
    const int bx = 60, by = 168, bw = 360, bh = 26;
    fb.drawRoundRect(bx, by, bw, bh, 6, C_ACCENT);
    int fillw = (bw - 4) * pct / 100;
    if (fillw < 0) fillw = 0;
    if (fillw > bw - 4) fillw = bw - 4;
    if (fillw > 0) fb.fillRoundRect(bx + 2, by + 2, fillw, bh - 4, 4, C_ACCENT);
    fb.setFont(&fonts::Font4);
    fb.setTextColor(C_WHITE);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    fb.drawString(buf, 240, 224);
}

void ui_tick(const simhub_telemetry_t *t)
{
    int32_t tx, ty;

    // Firmware update in progress: take over both displays with a progress
    // screen and skip the normal UI (also avoids touching the shared SPI bus
    // for anything but the update indicator).
    if (ui_ota_active()) {
        int pct = ui_ota_pct();
        render_ota(*fb_race, pct);
        fb_race->pushSprite(0, 0);
        if (s_use_disp2 && fb_btn) { render_ota(*fb_btn, pct); fb_btn->pushSprite(0, 0); }
        return;
    }

    // display 1 touch: RACE taps -> CONFIG; on CONFIG, tap the slider to set
    // brightness or the back button to return.
    bool d1 = disp_race->getTouch(&tx, &ty);
    if (d1 && s_d1 == D1_CONFIG) s_config_at = esp_timer_get_time();  // keep alive
    if (d1 && !s_t1_down) {
        if (s_d1 == D1_RACE) {
            s_d1 = D1_CONFIG;
            s_config_at = esp_timer_get_time();
        } else if (tx >= SLD_X && tx <= SLD_X + SLD_W &&
                   ty >= SLD_Y - 25 && ty <= SLD_Y + SLD_H + 25) {
            int pct = (tx - SLD_X) * 100 / SLD_W;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            led_rev_set_brightness(pct);
        } else if (tx >= SIM_X && tx <= SIM_X + SIM_W &&
                   ty >= SIM_Y && ty <= SIM_Y + SIM_H) {
            sim_set(!sim_get());
        } else if (tx >= RBT_X && tx <= RBT_X + RBT_W &&
                   ty >= RBT_Y && ty <= RBT_Y + RBT_H) {
            esp_restart();
        } else if (tx < 150 && ty < 70) {
            s_d1 = D1_RACE;
        }
    }
    s_t1_down = d1;

    // auto-return to RACE after 8s of no touch on the config screen
    if (s_d1 == D1_CONFIG && esp_timer_get_time() - s_config_at > 8000000)
        s_d1 = D1_RACE;

    // record map points whenever we have position
    if (t->pos_x || t->pos_z) map_record(t);

    // Display 2 FIRST (and rarely): poll touch every tick so the tabs stay
    // responsive, but render/push WAY less often. Pushing it before the race
    // screen means Display 1's push is always the LAST thing on the shared bus,
    // so any stray write to Display 1 (the 1px top-line glitch) is immediately
    // repaired by its own push.
    if (s_use_disp2 && disp_btn) {
        bool d2 = disp_btn->getTouch(&tx, &ty);
        bool changed = (d2 != s_t2_down);                // touch down/up edge
        int prev = s_d2;
        if (d2 && !s_t2_down) touch_d2(tx, ty);          // press
        if (!d2 && s_t2_down) {                          // release
            // release any held HID button before dropping the highlight
            if (s_press_btn >= 0 && s_press_page >= 0) {
                const ui_profile_t *ap = ui_active_profile();
                if (s_press_page < ap->page_count) {
                    const ui_button_page_t *rpg = &ap->pages[s_press_page];
                    if (s_press_btn < rpg->count) {
                        const ui_button_t *rbt = &rpg->buttons[s_press_btn];
                        if (rbt->action == UI_ACT_HID_HOLD)
                            hid_button_set(rbt->param, false);
                    }
                }
            }
            s_btn_held = false;                          // drop the highlight
            s_press_btn = -1;
            s_press_page = -1;
        }
        s_t2_down = d2;
        if (s_d2 >= d2_page_count()) s_d2 = 0;           // clamp if page count shrank
        // Signature of the current page's sync-from-game toggle states, so the
        // button screen re-renders the instant a bound game field flips (keeps
        // the toggle mirrored to the sim, not just on the ~0.7s heartbeat).
        uint32_t sync_sig = 0;
        const ui_profile_t *bp = ui_active_profile();
        if (s_d2 < bp->page_count) {
            const ui_button_page_t *pg = &bp->pages[s_d2];
            for (int i = 0; i < pg->count; i++) {
                const ui_button_t *bt = &pg->buttons[i];
                if (bt->toggle && bt->field)
                    sync_sig = sync_sig * 31u + (uint32_t)((field_value(t, bt->field) != 0) ? (i + 1) : 0);
            }
        }
        static uint32_t s_sync_sig = 0xFFFFFFFFu;
        bool sync_changed = (sync_sig != s_sync_sig);
        s_sync_sig = sync_sig;
        static int s_d2_div = 99;                        // force a render on the first tick
        if (s_d2 != prev || changed || sync_changed || ++s_d2_div >= 20) {   // tab/touch/sync change or ~0.7s
            s_d2_div = 0;
            render_buttons(*fb_btn, s_d2, t);
            fb_btn->pushSprite(0, 0);
        }
    }

    // race screen — rendered/pushed every tick, LAST on the bus
    if (s_d1 == D1_CONFIG) render_config(*fb_race);
    else                   render_race(*fb_race, t);
    fb_race->pushSprite(0, 0);
}
