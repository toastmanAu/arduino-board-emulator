// Simulated hardware setup — exercises the M2.E LGFX codemod.
// The codemod auto-detects the lgfx::Bus_SPI + Panel_ILI9488 + Touch_XPT2046
// pattern and rewrites this file at build time into a Panel_sdl wrapper.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define LCD_BL 5

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Touch_XPT2046 _touch_instance;

public:
    LGFX(void)
    {
        auto cfg = _panel_instance.config();
        cfg.panel_width  = 320;
        cfg.panel_height = 480;
        cfg.offset_rotation = 2;
        _panel_instance.config(cfg);
        setPanel(&_panel_instance);
    }
};
