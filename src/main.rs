// pithddu — ESP32-S3 SimHub dashboard firmware (Rust rewrite).
//
// Phase 2a: USB composite device (CDC + HID) up via the C shim over raw TinyUSB.
// SimHub telemetry arrives on CDC; the dashboard's commands arrive on the HID
// report-id-2 channel. A minimal dispatcher answers ?/@CAP/@T and stores
// telemetry. Full command set + OTA + NVS land in 2b; LEDs/display in later phases.

mod device;
mod usb;

use std::thread::sleep;
use std::time::Duration;

fn main() {
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    let serial = device::serial();
    log::info!("pithddu boot — serial {serial}");

    // Bring up the composite USB device (PHY + TinyUSB + device task).
    usb::init(serial);

    // Service the CDC RX FIFO (SimHub telemetry). The HID command channel is
    // driven by callbacks from the TinyUSB task, so this loop just drains CDC.
    let mut last_log = 0u32;
    let mut ticks = 0u32;
    loop {
        usb::poll_cdc();
        sleep(Duration::from_millis(5));
        ticks += 1;
        if ticks - last_log >= 1000 {
            last_log = ticks;
            log::info!("alive — usb mounted={}", usb::mounted());
        }
    }
}
