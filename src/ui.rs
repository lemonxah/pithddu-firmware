//! On-screen UI: parses the pushed @RS race layout + @BS button pages and renders
//! them with embedded-graphics. Render fns are generic over DrawTarget so the
//! display task can pass each ST7796 panel. The on-screen RPM strip reuses
//! pith_core::shift so it matches the physical LEDs.

use embedded_graphics::{
    pixelcolor::Rgb565,
    prelude::*,
    primitives::{Circle, PrimitiveStyle, Rectangle, RoundedRectangle},
};
use u8g2_fonts::{
    fonts,
    types::{FontColor, HorizontalAlignment, VerticalPosition},
    FontRenderer,
};

use pith_core::format::{self, Fmt, Pal, RuleOp};
use pith_core::registry::{field_def, field_id_from_str, field_value};
use pith_core::shift::{segment_rgb, CarData, RevCfg};
use pith_core::simhub::Telemetry;

pub const W: i32 = 480;
pub const H: i32 = 320;

// ---- palette (Pal token -> RGB565) ----
pub fn pal(p: Pal) -> Rgb565 {
    match p {
        Pal::Bg => rgb(8, 10, 14),
        Pal::Panel => rgb(28, 32, 40),
        Pal::White => rgb(235, 238, 245),
        Pal::Dim => rgb(120, 128, 140),
        Pal::Green => rgb(40, 220, 90),
        Pal::Amber => rgb(255, 180, 40),
        Pal::Red => rgb(240, 60, 60),
        Pal::Cyan => rgb(40, 210, 230),
        Pal::Blue => rgb(60, 130, 255),
        Pal::Purple => rgb(180, 110, 255),
    }
}
pub fn rgb(r: u8, g: u8, b: u8) -> Rgb565 {
    Rgb565::new(r >> 3, g >> 2, b >> 3)
}
fn rgb888(c: u32) -> Rgb565 {
    rgb((c >> 16) as u8, (c >> 8) as u8, c as u8)
}

pub const C_BG: Rgb565 = Rgb565::new(1, 2, 1);

// ---- text helper (picks a u8g2 font by requested pixel height) ----
#[allow(clippy::too_many_arguments)]
pub fn text<D: DrawTarget<Color = Rgb565>>(
    d: &mut D,
    s: &str,
    x: i32,
    y: i32,
    size: u32,
    color: Rgb565,
    h: HorizontalAlignment,
    v: VerticalPosition,
) {
    let p = Point::new(x, y);
    let fc = FontColor::Transparent(color);
    macro_rules! draw {
        ($f:ty) => {{
            let _ = FontRenderer::new::<$f>().render_aligned(s, p, v, h, fc, d);
        }};
    }
    match size {
        0..=11 => draw!(fonts::u8g2_font_6x13_tf),
        12..=15 => draw!(fonts::u8g2_font_helvB12_tf),
        16..=23 => draw!(fonts::u8g2_font_helvB18_tf),
        24..=33 => draw!(fonts::u8g2_font_helvB24_tf),
        _ => draw!(fonts::u8g2_font_logisoso32_tf),
    }
}

fn fill_rect<D: DrawTarget<Color = Rgb565>>(d: &mut D, x: i32, y: i32, w: i32, h: i32, c: Rgb565) {
    let _ = Rectangle::new(Point::new(x, y), Size::new(w.max(0) as u32, h.max(0) as u32))
        .into_styled(PrimitiveStyle::with_fill(c))
        .draw(d);
}
fn fill_round<D: DrawTarget<Color = Rgb565>>(d: &mut D, x: i32, y: i32, w: i32, h: i32, r: i32, c: Rgb565) {
    let _ = RoundedRectangle::with_equal_corners(
        Rectangle::new(Point::new(x, y), Size::new(w.max(0) as u32, h.max(0) as u32)),
        Size::new(r as u32, r as u32),
    )
    .into_styled(PrimitiveStyle::with_fill(c))
    .draw(d);
}

// ---- race layout model (parsed from @RS) ----
#[derive(Clone, Copy, PartialEq)]
pub enum Kind {
    Stat, Gear, GearSpeed, RpmStrip, TyreGrid, TcDual, Sectors, LapPair, Bar, Map, Flag, Position,
}
fn kind_of(s: &str) -> Kind {
    match s {
        "gear" => Kind::Gear,
        "gearSpeed" => Kind::GearSpeed,
        "rpmStrip" => Kind::RpmStrip,
        "tyreGrid" => Kind::TyreGrid,
        "tcDual" => Kind::TcDual,
        "sectors" => Kind::Sectors,
        "lapPair" => Kind::LapPair,
        "bar" => Kind::Bar,
        "map" => Kind::Map,
        "flag" => Kind::Flag,
        "position" => Kind::Position,
        _ => Kind::Stat,
    }
}

pub struct Module {
    pub kind: Kind,
    pub field: usize,
    pub label: String,
    pub fmt: Fmt,
    pub unit: String,
    pub scale: i32,
    pub base: Pal,
    pub rules: Vec<(RuleOp, i32, Pal)>,
    pub zone: usize, // 0=TOP 1=LEFT 2=CENTER 3=RIGHT 4=BOTTOM
    pub order: i32,
}

#[derive(Default)]
pub struct RaceLayout {
    pub mods: Vec<Module>,
}

pub fn parse_race(json: &str) -> Option<RaceLayout> {
    let v: serde_json::Value = serde_json::from_str(json).ok()?;
    let arr = v.get("mods")?.as_array()?;
    let mut out = RaceLayout::default();
    for m in arr {
        let field = m
            .get("f")
            .and_then(|x| x.as_str())
            .map(field_id_from_str)
            .unwrap_or(0);
        let def = field_def(field);
        let fmt = m
            .get("fmt")
            .and_then(|f| f.get("t"))
            .and_then(|x| x.as_str())
            .map(Fmt::from_str)
            .unwrap_or_else(|| def.map(|d| d.fmt).unwrap_or(Fmt::Int));
        let scale = m
            .get("fmt")
            .and_then(|f| f.get("sc"))
            .and_then(|x| x.as_i64())
            .map(|s| s as i32)
            .filter(|&s| s > 0)
            .unwrap_or_else(|| def.map(|d| d.scale).unwrap_or(1));
        let unit = m
            .get("fmt")
            .and_then(|f| f.get("u"))
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .to_string();
        let label = m
            .get("l")
            .and_then(|x| x.as_str())
            .map(|s| s.to_string())
            .unwrap_or_else(|| def.map(|d| d.label.to_string()).unwrap_or_default());
        let base = Pal::from_str(m.get("b").and_then(|x| x.as_str()).unwrap_or("white"));
        let mut rules = Vec::new();
        if let Some(rs) = m.get("r").and_then(|x| x.as_array()) {
            for r in rs.iter().take(4) {
                let op = RuleOp::from_str(r.get("op").and_then(|x| x.as_str()).unwrap_or(">"));
                let val = r.get("v").and_then(|x| x.as_i64()).unwrap_or(0) as i32;
                let col = Pal::from_str(r.get("c").and_then(|x| x.as_str()).unwrap_or("red"));
                rules.push((op, val, col));
            }
        }
        out.mods.push(Module {
            kind: kind_of(m.get("k").and_then(|x| x.as_str()).unwrap_or("stat")),
            field,
            label,
            fmt,
            unit,
            scale,
            base,
            rules,
            zone: m.get("z").and_then(|x| x.as_i64()).unwrap_or(0).clamp(0, 4) as usize,
            order: m.get("o").and_then(|x| x.as_i64()).unwrap_or(0) as i32,
        });
    }
    Some(out)
}

impl Module {
    fn color(&self, raw: i32) -> Rgb565 {
        for (op, v, c) in &self.rules {
            if op.matches(raw, *v) {
                return pal(*c);
            }
        }
        pal(self.base)
    }
}

// Zone rects (480x320), matching the legacy layout.
const ZONES: [(i32, i32, i32, i32, bool); 5] = [
    (0, 2, 480, 42, true),    // TOP (horizontal)
    (4, 50, 128, 220, false), // LEFT (vertical)
    (136, 50, 208, 220, false), // CENTER (vertical)
    (348, 50, 128, 220, false), // RIGHT (vertical)
    (0, 276, 480, 42, true),  // BOTTOM (horizontal)
];

pub fn render_race<D: DrawTarget<Color = Rgb565>>(d: &mut D, layout: &RaceLayout, t: &Telemetry, now_ms: i64) {
    let _ = d.clear(C_BG);
    for (zi, &(zx, zy, zw, zh, horiz)) in ZONES.iter().enumerate() {
        let mut zmods: Vec<&Module> = layout.mods.iter().filter(|m| m.zone == zi).collect();
        zmods.sort_by_key(|m| m.order);
        let n = zmods.len() as i32;
        if n == 0 {
            continue;
        }
        for (i, m) in zmods.iter().enumerate() {
            let i = i as i32;
            let (x, y, w, h) = if horiz {
                (zx + zw * i / n, zy, zw / n, zh)
            } else {
                (zx, zy + zh * i / n, zw, zh / n)
            };
            draw_module(d, x, y, w, h, m, t, now_ms);
        }
    }
}

fn draw_module<D: DrawTarget<Color = Rgb565>>(
    d: &mut D, x: i32, y: i32, w: i32, h: i32, m: &Module, t: &Telemetry, now_ms: i64,
) {
    let cx = x + w / 2;
    let raw = field_value(t, m.field);
    match m.kind {
        Kind::RpmStrip => {
            let seg = 12;
            let sw = w / seg;
            for i in 0..seg {
                let c = segment_rgb(t, i, seg, &RevCfg::default(), &CarData::default(), now_ms);
                let col = if c == 0 { pal(Pal::Panel) } else { rgb888(c) };
                fill_round(d, x + i * sw + 1, y + 4, sw - 2, h - 8, 3, col);
            }
        }
        Kind::Gear | Kind::GearSpeed => {
            let g = if t.gear == 0 { 'N' } else { t.gear as char };
            text(d, &g.to_string(), cx, y + h / 2 - 6, 40, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
            if m.kind == Kind::GearSpeed {
                text(d, &t.speed_kmh.to_string(), cx, y + h - 26, 24, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
                text(d, "KM/H", cx, y + h - 8, 11, pal(Pal::Dim), HorizontalAlignment::Center, VerticalPosition::Center);
            }
        }
        Kind::Position => {
            text(d, &m.label, cx, y + 12, 11, pal(Pal::Dim), HorizontalAlignment::Center, VerticalPosition::Center);
            let s = format!("P{}/{}", t.position, t.field_size);
            text(d, &s, cx, y + h / 2 + 4, 22, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
        }
        Kind::TcDual => {
            text(d, "TC", x + w / 4, y + 12, 11, pal(Pal::Dim), HorizontalAlignment::Center, VerticalPosition::Center);
            text(d, "ABS", x + 3 * w / 4, y + 12, 11, pal(Pal::Dim), HorizontalAlignment::Center, VerticalPosition::Center);
            text(d, &t.tc.to_string(), x + w / 4, y + h / 2 + 6, 22, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
            text(d, &t.abs.to_string(), x + 3 * w / 4, y + h / 2 + 6, 22, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
        }
        Kind::LapPair => {
            let cur = format::format(t.cur_lap_ms, Fmt::Time, 1, "");
            let best = format::format(t.best_lap_ms, Fmt::Time, 1, "");
            text(d, "CURRENT", cx, y + 10, 11, pal(Pal::Dim), HorizontalAlignment::Center, VerticalPosition::Center);
            text(d, &cur, cx, y + h / 4 + 6, 18, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
            text(d, "BEST", cx, y + h / 2 + 8, 11, pal(Pal::Dim), HorizontalAlignment::Center, VerticalPosition::Center);
            text(d, &best, cx, y + 3 * h / 4 + 4, 18, pal(Pal::Cyan), HorizontalAlignment::Center, VerticalPosition::Center);
        }
        Kind::Sectors => {
            let secs = [t.s1_ms, t.s2_ms, t.s3_ms];
            let bs = [t.bs1_ms, t.bs2_ms, t.bs3_ms];
            let sw = w / 3;
            for i in 0..3 {
                let col = if secs[i] > 0 && bs[i] > 0 && secs[i] <= bs[i] { pal(Pal::Green) } else { pal(Pal::Amber) };
                let s = format::format(secs[i], Fmt::Sector, 1, "");
                text(d, &s, x + i as i32 * sw + sw / 2, y + h / 2, 12, col, HorizontalAlignment::Center, VerticalPosition::Center);
            }
        }
        Kind::Bar => {
            let pct = if m.scale > 0 { (raw * 100 / m.scale).clamp(0, 100) } else { 0 };
            text(d, &m.label, x + 4, y + 10, 11, pal(Pal::Dim), HorizontalAlignment::Left, VerticalPosition::Center);
            fill_rect(d, x + 4, y + h / 2, w - 8, h / 3, pal(Pal::Panel));
            fill_rect(d, x + 4, y + h / 2, (w - 8) * pct / 100, h / 3, m.color(raw));
        }
        Kind::TyreGrid => {
            let temps = [t.tt_fl_m, t.tt_fr_m, t.tt_rl_m, t.tt_rr_m];
            let bw = w / 2;
            let bh = h / 2;
            for i in 0..4 {
                let (cxx, cyy) = (x + (i as i32 % 2) * bw, y + (i as i32 / 2) * bh);
                let col = if temps[i] > 95 { pal(Pal::Red) } else if temps[i] > 80 { pal(Pal::Amber) } else { pal(Pal::Green) };
                fill_round(d, cxx + 2, cyy + 2, bw - 4, bh - 4, 4, pal(Pal::Panel));
                text(d, &temps[i].to_string(), cxx + bw / 2, cyy + bh / 2, 14, col, HorizontalAlignment::Center, VerticalPosition::Center);
            }
        }
        Kind::Flag => {
            fill_round(d, x + 4, y + 4, w - 8, h - 8, 4, m.color(raw));
        }
        Kind::Map => {
            let _ = Circle::new(Point::new(cx - h / 3, y + h / 6), (h / 3).max(1) as u32)
                .into_styled(PrimitiveStyle::with_stroke(pal(Pal::Dim), 1))
                .draw(d);
        }
        Kind::Stat => {
            text(d, &m.label, cx, y + 11, 11, pal(Pal::Dim), HorizontalAlignment::Center, VerticalPosition::Center);
            let s = format::format(raw, m.fmt, m.scale, &m.unit);
            text(d, &s, cx, y + h / 2 + 6, 22, m.color(raw), HorizontalAlignment::Center, VerticalPosition::Center);
        }
    }
}

// ---- button box (parsed from @BS) ----
pub struct Button {
    pub label: String,
    pub toggle: bool,
    pub color: Rgb565,
    pub sync: bool,
    pub field: usize,
    pub hid: usize,
}
#[derive(Default)]
pub struct Buttons {
    pub pages: Vec<Vec<Button>>,
}

pub fn parse_buttons(json: &str) -> Option<Buttons> {
    let v: serde_json::Value = serde_json::from_str(json).ok()?;
    let pages = v.get("pages")?.as_array()?;
    let mut out = Buttons::default();
    for (pi, page) in pages.iter().enumerate() {
        let mut pg = Vec::new();
        if let Some(btns) = page.as_array() {
            for (j, b) in btns.iter().enumerate() {
                let color = b
                    .get("color")
                    .and_then(|x| x.as_str())
                    .and_then(|s| u32::from_str_radix(s.trim_start_matches('#'), 16).ok())
                    .map(rgb888)
                    .unwrap_or_else(|| rgb888(0x00E5A0));
                pg.push(Button {
                    label: b.get("label").and_then(|x| x.as_str()).unwrap_or("").to_string(),
                    toggle: b.get("kind").and_then(|x| x.as_str()) == Some("toggle"),
                    color,
                    sync: b.get("sync").and_then(|x| x.as_bool()).unwrap_or(false),
                    field: b.get("field").and_then(|x| x.as_str()).map(field_id_from_str).unwrap_or(0),
                    hid: pi * 8 + j,
                });
            }
        }
        out.pages.push(pg);
    }
    Some(out)
}

pub const TABH: i32 = 32;
const GRID_COLS: i32 = 3;
const GRID_ROWS: i32 = 2;

/// Rect of button `idx` on the side screen (below the tab bar), 3x2 grid.
pub fn button_rect(idx: usize) -> (i32, i32, i32, i32) {
    let m = 8;
    let gw = (W - m * (GRID_COLS + 1)) / GRID_COLS;
    let gh = (H - TABH - m * (GRID_ROWS + 1)) / GRID_ROWS;
    let col = idx as i32 % GRID_COLS;
    let row = idx as i32 / GRID_COLS;
    (m + col * (gw + m), TABH + m + row * (gh + m), gw, gh)
}

pub fn render_buttons<D: DrawTarget<Color = Rgb565>>(
    d: &mut D, buttons: &Buttons, page: usize, t: &Telemetry, toggle_on: &[bool; 32],
) {
    let _ = d.clear(C_BG);
    // tab bar
    let np = buttons.pages.len().max(1) as i32;
    let tw = W / np;
    for p in 0..np {
        let on = p as usize == page;
        text(d, &format!("P{}", p + 1), p * tw + tw / 2, TABH / 2, 12,
             if on { pal(Pal::White) } else { pal(Pal::Dim) },
             HorizontalAlignment::Center, VerticalPosition::Center);
        if on {
            fill_rect(d, p * tw + 8, TABH - 3, tw - 16, 3, pal(Pal::Cyan));
        }
    }
    if let Some(pg) = buttons.pages.get(page) {
        for b in pg {
            let (x, y, w, h) = button_rect(b.hid % 8);
            let lit = if b.sync && b.field != 0 {
                field_value(t, b.field) > 0
            } else {
                b.toggle && toggle_on[b.hid.min(31)]
            };
            let body = if lit { b.color } else { pal(Pal::Panel) };
            fill_round(d, x, y, w, h, 8, body);
            text(d, &b.label, x + w / 2, y + h / 2, 14, pal(Pal::White),
                 HorizontalAlignment::Center, VerticalPosition::Center);
        }
    }
}

// ---- config screen ----
pub const SLD: (i32, i32, i32, i32) = (40, 150, 400, 40); // brightness slider
pub const SIM_BTN: (i32, i32, i32, i32) = (40, 262, 180, 46);
pub const RBT_BTN: (i32, i32, i32, i32) = (260, 262, 180, 46);
pub const BACK_BTN: (i32, i32, i32, i32) = (20, 16, 120, 46);

pub fn render_config<D: DrawTarget<Color = Rgb565>>(d: &mut D, brightness: u8, sim: bool) {
    let _ = d.clear(C_BG);
    fill_round(d, BACK_BTN.0, BACK_BTN.1, BACK_BTN.2, BACK_BTN.3, 6, pal(Pal::Panel));
    text(d, "< RACE", BACK_BTN.0 + BACK_BTN.2 / 2, BACK_BTN.1 + BACK_BTN.3 / 2, 14, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
    text(d, "CONFIG", W / 2, 32, 16, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
    text(d, "FW 0.9.5", W - 20, 24, 11, pal(Pal::Dim), HorizontalAlignment::Right, VerticalPosition::Center);

    text(d, "REV COUNTER BRIGHTNESS", SLD.0, SLD.1 - 18, 12, pal(Pal::Dim), HorizontalAlignment::Left, VerticalPosition::Center);
    fill_round(d, SLD.0, SLD.1, SLD.2, SLD.3, 6, pal(Pal::Panel));
    fill_round(d, SLD.0, SLD.1, SLD.2 * brightness as i32 / 100, SLD.3, 6, pal(Pal::Cyan));
    text(d, &format!("{brightness}%"), SLD.0 + SLD.2 / 2, SLD.1 + SLD.3 / 2, 14, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);

    fill_round(d, SIM_BTN.0, SIM_BTN.1, SIM_BTN.2, SIM_BTN.3, 6, if sim { pal(Pal::Green) } else { pal(Pal::Panel) });
    text(d, "SIM", SIM_BTN.0 + SIM_BTN.2 / 2, SIM_BTN.1 + SIM_BTN.3 / 2, 14, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
    fill_round(d, RBT_BTN.0, RBT_BTN.1, RBT_BTN.2, RBT_BTN.3, 6, pal(Pal::Panel));
    text(d, "REBOOT", RBT_BTN.0 + RBT_BTN.2 / 2, RBT_BTN.1 + RBT_BTN.3 / 2, 14, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
}

pub fn render_ota<D: DrawTarget<Color = Rgb565>>(d: &mut D, pct: i32) {
    let _ = d.clear(C_BG);
    text(d, "FIRMWARE UPDATE", W / 2, 110, 18, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
    text(d, "Flashing — do not disconnect", W / 2, 145, 12, pal(Pal::Dim), HorizontalAlignment::Center, VerticalPosition::Center);
    fill_round(d, 60, 175, 360, 30, 6, pal(Pal::Panel));
    fill_round(d, 60, 175, 360 * pct.clamp(0, 100) / 100, 30, 6, pal(Pal::Cyan));
    text(d, &format!("{pct}%"), W / 2, 220, 14, pal(Pal::White), HorizontalAlignment::Center, VerticalPosition::Center);
}

pub fn hit(p: (i32, i32, i32, i32), tx: i32, ty: i32) -> bool {
    tx >= p.0 && tx < p.0 + p.2 && ty >= p.1 && ty < p.1 + p.3
}
