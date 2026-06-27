//! USB HID gamepad — a 32-button "button box" the sim sees as a controller.
//! Touch widgets call pulse()/set(); a task pushes a joystick report (id 1) via
//! the pith_usb shim whenever the button mask changes. Port of hid_gamepad.c.

use std::sync::Mutex;
use std::thread;
use std::time::Duration;

use esp_idf_svc::sys;

const N: usize = 32;
const PULSE_MS: i64 = 60; // how long a tapped button stays "pressed"

struct Gamepad {
    mask: u32,            // current button bitmask
    sent: u32,            // last mask reported to the host
    release_us: [i64; N], // 0 = not auto-releasing
}

static PAD: Mutex<Gamepad> = Mutex::new(Gamepad {
    mask: 0,
    sent: 0,
    release_us: [0; N],
});

fn now_us() -> i64 {
    unsafe { sys::esp_timer_get_time() }
}

/// Momentary press of `btn` (0..31): pressed now, auto-released shortly.
pub fn pulse(btn: usize) {
    if btn >= N {
        return;
    }
    let mut p = PAD.lock().unwrap();
    p.mask |= 1 << btn;
    p.release_us[btn] = now_us() + PULSE_MS * 1000;
}

/// Hold/release `btn` explicitly (toggles / press-and-hold widgets).
pub fn set(btn: usize, pressed: bool) {
    if btn >= N {
        return;
    }
    let mut p = PAD.lock().unwrap();
    if pressed {
        p.mask |= 1 << btn;
    } else {
        p.mask &= !(1 << btn);
    }
    p.release_us[btn] = 0; // explicit control cancels any pulse timer
}

/// Service pulse timers and push a report when the mask changed.
fn service() {
    let mask = {
        let mut p = PAD.lock().unwrap();
        let now = now_us();
        for i in 0..N {
            if p.release_us[i] != 0 && now >= p.release_us[i] {
                p.mask &= !(1 << i);
                p.release_us[i] = 0;
            }
        }
        if p.mask == p.sent {
            return;
        }
        p.mask
    };
    if !unsafe { sys::pith_hid_ready() } {
        return;
    }
    let bytes = mask.to_le_bytes();
    if unsafe {
        sys::pith_hid_send(1, bytes.as_ptr() as *const core::ffi::c_void, bytes.len() as i32)
    } {
        PAD.lock().unwrap().sent = mask;
    }
}

/// Spawn the HID service task (~100 Hz).
pub fn spawn() {
    thread::Builder::new()
        .stack_size(2048)
        .name("hid".into())
        .spawn(|| loop {
            service();
            thread::sleep(Duration::from_millis(10));
        })
        .expect("spawn hid task");
}
