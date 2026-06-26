// LovyanGFX setup: two ST7796 panels + two XPT2046 touch on ONE shared SPI bus.
//
// Both panels share a single lgfx::Bus_SPI (shared SCLK/MOSI/MISO **and DC**);
// each panel has its own CS. Two separate Bus_SPI objects on the same SPI host
// fight over the peripheral (one panel stays black, the other glitches), so the
// bus MUST be a single shared instance — which means both panels' DC pins are
// wired to the same GPIO (PIN_DC).
//
// Shared bus:  SCLK=7  MOSI=9  MISO=8  DC=2
//   Display 1: CS=1   touch CS=5
//   Display 2: CS=3   touch CS=6
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

static constexpr uint32_t SPI_WRITE_HZ = 40000000;

// The one SPI bus, lazily configured on first use from the runtime pin map
// (pass the pins on the first call; later calls reuse the configured bus).
static lgfx::Bus_SPI &shared_bus(int sclk = -1, int mosi = -1, int miso = -1, int dc = -1)
{
    static lgfx::Bus_SPI bus;
    static bool configured = false;
    if (!configured && sclk >= 0) {
        auto cfg = bus.config();
        cfg.spi_host    = SPI2_HOST;
        cfg.spi_mode    = 0;
        cfg.freq_write  = SPI_WRITE_HZ;
        cfg.freq_read   = 16000000;
        cfg.spi_3wire   = false;
        cfg.use_lock    = true;
        cfg.dma_channel = SPI_DMA_CH_AUTO;
        cfg.pin_sclk    = sclk;
        cfg.pin_mosi    = mosi;
        cfg.pin_miso    = miso;
        cfg.pin_dc      = dc;
        bus.config(cfg);
        configured = true;
    }
    return bus;
}

class LGFX_ST7796 : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796  _panel;
    lgfx::Touch_XPT2046 _touch;

public:
    // Construct from the runtime pin map. The first instance configures the
    // shared bus (sclk/mosi/miso/dc); both instances pass the same bus pins.
    LGFX_ST7796(int sclk, int mosi, int miso, int dc, int pin_cs, int touch_cs)
    {
        _panel.setBus(&shared_bus(sclk, mosi, miso, dc));
        {
            auto cfg = _panel.config();
            cfg.pin_cs          = pin_cs;
            cfg.pin_rst         = -1;     // tied to 3V3
            cfg.pin_busy        = -1;
            cfg.panel_width     = 320;
            cfg.panel_height    = 480;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.readable        = false;
            cfg.invert          = false;
            cfg.rgb_order       = false;
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = true;
            _panel.config(cfg);
        }
        {
            auto cfg = _touch.config();
            // Y axis inverted (y_min > y_max); X normal — matches the panels.
            cfg.x_min      = 300;
            cfg.x_max      = 3900;
            cfg.y_min      = 3900;
            cfg.y_max      = 300;
            cfg.pin_int    = -1;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.spi_host   = SPI2_HOST;
            cfg.freq       = 1000000;
            cfg.pin_sclk   = sclk;
            cfg.pin_mosi   = mosi;
            cfg.pin_miso   = miso;
            cfg.pin_cs     = touch_cs;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
