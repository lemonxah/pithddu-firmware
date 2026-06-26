// Composite USB descriptors: CDC-ACM (SimHub Custom Serial) + HID gamepad
// (the touch button box). esp_tinyusb installs these via tinyusb_config_t's
// descriptor fields; the CDC class is still driven by the tinyusb_cdcacm
// wrapper, while HID is handled by the tud_hid_* callbacks below.

#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "device_serial.h"

// Command channel hooks (implemented in simhub_main.c). The PC app talks to the
// device over HID report id 2 (so SimHub can keep the CDC port).
extern void hid_cmd_rx(const uint8_t *buf, uint16_t len);
extern void hid_tx_complete(void);

// ---- HID report descriptor ----
// Report ID 1: 32-button JOYSTICK (the touch button box).
// Report ID 2: vendor IN/OUT command channel (63-byte reports) for the PC app.
static const uint8_t s_hid_report[] = {
    // --- Joystick, Report ID 1 ---
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x04,        // Usage (Joystick)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x20,        //   Usage Maximum (Button 32)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x20,        //   Report Count (32)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0xC0,              // End Collection
    // --- Vendor command channel, Report ID 2 ---
    0x06, 0x00, 0xFF,  // Usage Page (Vendor-defined 0xFF00)
    0x09, 0x01,        // Usage (0x01)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x3F,        //   Report Count (63)
    0x09, 0x01, 0x81, 0x02,  //   Input  (Data,Var,Abs)  device -> host
    0x09, 0x01, 0x91, 0x02,  //   Output (Data,Var,Abs)  host -> device
    0xC0               // End Collection
};

// TinyUSB calls this to fetch the report descriptor (single HID instance).
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report;
}

// Host -> device GET_REPORT: we have nothing to return.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

// Host -> device reports. The PC app sends command data on report id 2 (via the
// interrupt OUT endpoint or SET_REPORT). For interrupt OUT with report IDs, the
// id is the first payload byte and report_id arrives as 0 — handle both.
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_type;
    const uint8_t *data = buffer;
    uint16_t len = bufsize;
    uint8_t rid = report_id;
    if (rid == 0 && bufsize > 0) { rid = buffer[0]; data = buffer + 1; len = bufsize - 1; }
    if (rid == 2 && len > 0) hid_cmd_rx(data, len);
}

// An IN report finished sending — pump any queued command-reply chunks.
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)instance; (void)report; (void)len;
    hid_tx_complete();
}

// ---- Interface ----
enum {
    ITF_NUM_CDC = 0,    // CDC notification interface
    ITF_NUM_CDC_DATA,   // CDC data interface
    ITF_NUM_HID,        // HID gamepad interface
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82
#define EPNUM_HID_OUT   0x04    // host -> device command reports
#define EPNUM_HID_IN    0x83    // device -> host (joystick + replies)

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

// String descriptor index for each interface.
#define STRID_CDC  4
#define STRID_HID  5

// ---- Device descriptor (IAD-class so CDC + HID coexist on Windows) ----
const tusb_desc_device_t usb_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x303A,   // Espressif
    .idProduct          = 0x4002,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

// ---- Configuration descriptor: CDC + HID ----
const uint8_t usb_fs_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // CDC: notif EP, then data OUT/IN
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    // HID: IN (joystick + replies) + OUT (commands), 1 ms poll for low latency
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, STRID_HID, HID_ITF_PROTOCOL_NONE,
                             sizeof(s_hid_report), EPNUM_HID_OUT, EPNUM_HID_IN, 64, 1),
};

// ---- String descriptors (index 0 = language id) ----
// Index 3 (serial) is filled at runtime by usb_descriptors_init() with the
// per-device serial; the placeholder is only used if init is somehow skipped.
const char *usb_string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },   // 0: English (0x0409)
    "Pith",                         // 1: Manufacturer
    "Sim Dashboard",                // 2: Product
    "PITH-0000",                    // 3: Serial (set at boot, see below)
    "Pith Serial",                  // 4: CDC interface
    "Sim Dashboard HID",            // 5: HID interface
};
const int usb_string_descriptor_count =
    (int)(sizeof(usb_string_descriptors) / sizeof(usb_string_descriptors[0]));

// Resolve the device serial (generated/persisted on first boot) and point the
// serial string descriptor at it. Call once before installing the USB driver.
void usb_descriptors_init(void)
{
    usb_string_descriptors[3] = device_serial_get();
}
