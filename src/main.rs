// pithddu — ESP32-S3 SimHub dashboard firmware (Rust rewrite).
//
// Phase 2: USB composite device (CDC + HID) via the C shim over raw TinyUSB, the
// full `@`-command protocol (config pushes + NVS persistence + capability
// handshake), and OTA-over-USB. SimHub telemetry arrives on CDC; the dashboard's
// commands on the HID report-id-2 channel. LEDs/display land in later phases.

mod device;
mod display;
mod hid;
mod led;
mod ota;
mod sim;
mod state;
mod ui;
mod usb;

use std::thread::sleep;
use std::time::Duration;

fn main() {
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    let serial = device::serial();
    log::info!("pithddu boot — serial {serial}");

    // Restore PC-pushed config (pins, layout, buttons, car, brightness) from NVS.
    state::init();

    // Bring up the composite USB device (PHY + TinyUSB + device task).
    usb::init(serial);

    // Rev/TC/ABS LED strip (own task: self-test + telemetry-driven shift lights).
    led::spawn();
    // HID gamepad service + bench-test sim generator.
    hid::spawn();
    sim::spawn();
    // Displays + touch + UI (own task).
    display::spawn();

    // We booted and ran successfully — confirm this image so a just-OTA'd update
    // isn't rolled back on the next reset.
    sleep(Duration::from_millis(1000));
    ota::mark_valid();
    log::info!("USB up; image marked valid");

    let mut ticks: u32 = 0;
    loop {
        usb::poll_cdc(); // drain SimHub telemetry on CDC (HID is callback-driven)
        ota::check_timeout();

        if ota::should_reboot() {
            log::warn!("OTA complete — rebooting into the new image");
            sleep(Duration::from_millis(200));
            unsafe { esp_idf_svc::sys::esp_restart() };
        }
        if state::with(|s| s.cfg_reboot) {
            log::warn!("pin layout changed — rebooting to apply");
            sleep(Duration::from_millis(300)); // let the OK reply flush first
            unsafe { esp_idf_svc::sys::esp_restart() };
        }

        sleep(Duration::from_millis(5));
        ticks = ticks.wrapping_add(1);
        if ticks % 2000 == 0 {
            log::info!("alive — usb mounted={}", usb::mounted());
        }
    }
}
