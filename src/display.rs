//! Dual ST7796 SPI displays on the shared SPI2 bus. Phase 4: single-panel
//! bring-up to validate the mipidsi + esp-idf-hal SPI integration (dynamic pins
//! from the runtime map, shared bus). Dual-panel (shared DC) + PSRAM framebuffers
//! + the @RS layout UI follow.

use std::thread;
use std::time::Duration;

use embedded_graphics::pixelcolor::Rgb565;
use embedded_graphics::prelude::*;
use esp_idf_svc::hal::delay::Ets;
use esp_idf_svc::hal::gpio::{AnyIOPin, PinDriver};
use esp_idf_svc::hal::peripherals::Peripherals;
use esp_idf_svc::hal::spi::{config::Config as SpiConfig, SpiDeviceDriver, SpiDriver, SpiDriverConfig};
use esp_idf_svc::hal::units::FromValueType;
use mipidsi::interface::SpiInterface;
use mipidsi::models::ST7796;
use mipidsi::options::{ColorInversion, Orientation, Rotation};
use mipidsi::Builder;

use crate::state;

fn display_task() {
    let peripherals = match Peripherals::take() {
        Ok(p) => p,
        Err(e) => {
            log::error!("peripherals take failed: {e:?}");
            return;
        }
    };
    let pins = state::with(|s| s.pins);

    // Shared SPI2 bus (sclk/mosi/miso). Pins are runtime values from the map.
    let driver = SpiDriver::new(
        peripherals.spi2,
        unsafe { AnyIOPin::new(pins.sclk) },
        unsafe { AnyIOPin::new(pins.mosi) },
        Some(unsafe { AnyIOPin::new(pins.miso) }),
        &SpiDriverConfig::new(),
    )
    .expect("spi bus");

    let cfg = SpiConfig::new().baudrate(40.MHz().into());
    let dev1 = SpiDeviceDriver::new(&driver, Some(unsafe { AnyIOPin::new(pins.disp1_cs) }), &cfg)
        .expect("disp1 device");
    let dc = PinDriver::output(unsafe { AnyIOPin::new(pins.dc) }).expect("dc pin");

    let mut buffer = [0u8; 512];
    let di = SpiInterface::new(dev1, dc, &mut buffer);
    let mut delay = Ets;
    let mut display = Builder::new(ST7796, di)
        .display_size(320, 480)
        .orientation(Orientation::new().rotate(Rotation::Deg90))
        .invert_colors(ColorInversion::Normal)
        .init(&mut delay)
        .expect("st7796 init");

    let _ = display.clear(Rgb565::new(0, 0, 31)); // blue fill — bring-up smoke test
    log::info!("display 1 up (ST7796, single-panel bring-up)");

    loop {
        thread::sleep(Duration::from_millis(1000));
    }
}

/// Spawn the display task.
pub fn spawn() {
    thread::Builder::new()
        .stack_size(8192)
        .name("display".into())
        .spawn(display_task)
        .expect("spawn display task");
}
