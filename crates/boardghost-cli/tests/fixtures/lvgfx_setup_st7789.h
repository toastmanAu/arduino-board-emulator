#include <LovyanGFX.hpp>

class MyLGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;

public:
    MyLGFX() {
        auto cfg = _panel_instance.config();
        cfg.panel_width  = 240;
        cfg.panel_height = 320;
        cfg.offset_rotation = 0;
        _panel_instance.config(cfg);
        setPanel(&_panel_instance);
    }
};
