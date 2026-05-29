#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Touch_XPT2046 _touch_instance;

public:
    LGFX(void)
    {
        auto cfg = _panel_instance.config();
        cfg.panel_width = 320;
        cfg.panel_height = 480;
        cfg.offset_rotation = 2;
        _panel_instance.config(cfg);
        setPanel(&_panel_instance);
    }
};
