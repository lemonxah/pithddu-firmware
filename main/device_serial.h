// Per-device serial number. Generated once on first boot from the chip's
// factory eFuse MAC (globally unique per ESP32) and persisted to NVS, so it is
// stable across reboots and reflashes.
#pragma once

// Returns the device serial, e.g. "PITH-A1B2C3D4E5F6". Generates and persists
// it on the first call (and first boot). The returned pointer is owned by this
// module and valid for the lifetime of the program. Requires NVS to be
// initialized first; if NVS is unavailable it still returns a valid (but
// non-persisted) MAC-derived serial.
const char *device_serial_get(void);
