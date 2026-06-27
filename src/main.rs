// pithddu — ESP32-S3 SimHub dashboard firmware (Rust rewrite).
//
// Phase 1: the pure-logic core (pith-core) is in place — telemetry parsing, wire
// formatting, and the field registry, all host-tested. This boot stub exercises
// it so the on-device build links and runs the shared code. Later phases add USB
// + command protocol, the LED strip, displays/touch, and the UI.

use std::thread::sleep;
use std::time::Duration;

use pith_core::format::{format, Fmt};
use pith_core::registry::{field_value, FIELD_COUNT};
use pith_core::simhub::{parse_line, Telemetry};

fn main() {
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    log::info!("pithddu boot — field registry: {} ids", FIELD_COUNT);

    // Smoke-test the shared core on-device so a regression shows in the boot log.
    let sample = "$3;212;6800;7500;7100;84012;;;;;-3000";
    let t = parse_line(sample).unwrap_or_else(Telemetry::idle);
    log::info!(
        "parsed: gear={} speed={} rpm={} delta={}",
        t.gear as char,
        field_value(&t, 1),
        field_value(&t, 2),
        format(t.delta_ms, Fmt::Delta, 1, ""),
    );

    let mut tick: u64 = 0;
    loop {
        sleep(Duration::from_secs(5));
        tick += 1;
        log::info!("pithddu alive (tick {tick})");
    }
}
