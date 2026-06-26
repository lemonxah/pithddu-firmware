idf_component_register(
    SRCS "simhub_main.c"
         "simhub_proto.c"
         "led_rev.c"
         "hid_gamepad.c"
         "usb_descriptors.c"
         "device_serial.c"
         "device_pins.c"
         "ui_config.c"
         "car_data.c"
         "race_layout.c"
         "ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_tinyusb led_strip lovyangfx esp_timer driver json nvs_flash app_update esp_hw_support
)
