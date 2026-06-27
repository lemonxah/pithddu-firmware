// pithddu — ESP32-S3 SimHub dashboard firmware (Rust rewrite).
//
// Phase 0: bring up the cargo / esp-idf-sys toolchain and confirm the board
// boots and logs. Subsequent phases add the pure-logic core, USB composite +
// command protocol, LED strip, displays/touch, and the UI.

use std::thread::sleep;
use std::time::Duration;

fn main() {
    // Required: applies runtime patches and sets up the esp-idf-sys runtime.
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    log::info!("pithddu boot");

    let mut tick: u64 = 0;
    loop {
        sleep(Duration::from_secs(5));
        tick += 1;
        log::info!("pithddu alive (tick {tick})");
    }
}
