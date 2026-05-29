# lgfx_codemod_smoke

Smoke test for the M2.E LGFX codemod. The sketch ships a hardware-style
`lvgfx_setup.h` (Panel_ILI9488 + Bus_SPI + Touch_XPT2046) and expects
boardghost to rewrite it into a Panel_sdl-backed simulator wrapper at
build time. Also exercises the `#define` preservation path
(`pinMode(LCD_BL, OUTPUT)` references a constant declared inside the setup).

## Run

```bash
boardghost run --board st7789_esp32s3_sim examples/lgfx_codemod_smoke
```

Auto-exits after ~200 frames.
