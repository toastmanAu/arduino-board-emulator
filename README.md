# BoardGhost

Run unmodified ESP32 + Arduino sketches that use **LovyanGFX** and **LVGL** on your desktop. No flashing, no real hardware.

**Status:** M2.A complete. Engine, CLI, launcher, and 7 displays + touch + GPIO + screenshot shipping.

## What works

- 7 simulated panels (LovyanGFX + LVGL): ILI9488 480×320, ILI9341 320×240,
  ST7789 240×320, ST7796 480×320, GC9A01 240×240 round, ST7735 160×128,
  SSD1306 128×64 mono
- Touch driver (XPT2046 / FT6236 / GT911 in board profiles; SDL-mouse-backed in sim)
- Mouse + LovyanGFX touch API + LVGL indev
- IoT library stubs: WiFi, WiFiClient, WiFiClientSecure, HTTPClient, EEPROM,
  FS, SPIFFS, LittleFS, SD, TinyGsmClient, StreamDebugger
- Configurable network: `BOARDGHOST_NET=fake|fail|real` (real mode uses libcurl)
- Filesystem assets in `./sim-assets/<mount>/` (per-project, gitignorable)
- EEPROM persists to `./.boardghost/eeprom.bin`
- GPIO inspector (live pin-state grid in the launcher)
- CLI `--screenshot PATH` flag
- Tauri desktop launcher
- Headless mode (`SDL_VIDEODRIVER=dummy`) for CI

## What doesn't work yet

- SH1106 display controller — future release
- SPI/I2C transaction decoding — future release
- WiFi, BLE, FreeRTOS, ESP-IDF native — out of scope for the foreseeable future

## Requirements

- Linux (M1 target; macOS/Windows best-effort, untested)
- `arduino-cli` ≥ 0.35
- `cmake` ≥ 3.20
- `ninja`
- `SDL2` development headers
- A Rust toolchain (stable)

Install on Ubuntu:

```bash
sudo apt install cmake ninja-build libsdl2-dev
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

Both M1.A board profiles preprocess via the **ESP32 core**, even when simulating different hardware. Install it:

```bash
arduino-cli core install esp32:esp32
```

## Quick start

```bash
git clone --recurse-submodules https://github.com/<you>/arduino-board-emulator.git
cd arduino-board-emulator
cargo build --release -p boardghost-cli
./target/release/boardghost doctor          # verify host setup
./target/release/boardghost run examples/lvgl_hello_ili9488 \
        --board ili9488_esp32s3_sim
```

A 480×320 window appears with an LVGL button. Click it.

See [`docs/getting-started.md`](docs/getting-started.md) for the 5-minute tour.

## Using custom Arduino libraries (M2.C)

BoardGhost can build sketches against any Arduino library installed via
`arduino-cli`. Header-only libraries (ArduinoJson, TimeLib, ...) work
transparently; libraries with platform-specific transports can be shimmed.

```bash
# Install whatever your sketch needs
arduino-cli lib install ArduinoJson Time

# Then build normally
boardghost run --board generic-esp32-st7789 path/to/your/sketch
```

The CLI auto-discovers installed libraries via `arduino-cli lib list`,
maps `#include <...>` directives to libraries via their `provides_includes`
metadata, and injects each library's source dir into the generated
CMakeLists.txt.

### Library overrides

For libraries with hardware-specific code that won't compile against SDL
(e.g. ESP32-only TCP transports), drop a `<libname>.toml` file in
`runtime/library_overrides/`:

```toml
exclude_dirs = ["network/esp32", "network/esp8266"]
exclude_files = ["server.cpp"]
add_include_dirs = ["${BOARDGHOST_RUNTIME_DIR}/shims/sim_ws"]
shim_only = false   # set true if you ship a full header-only replacement
```

Existing overrides:
- `ArduinoWebsockets` — uses BoardGhost's header-only shim
  (`runtime/shims/ArduinoWebsockets.h`); real WebSocket support is future
  work but the shim compiles cleanly.

## Using sketches with hardware LGFX setups (M2.E)

If your sketch ships its own `lvgfx_setup.h` (or similar header) that declares
a `class LGFX : public lgfx::LGFX_Device` using hardware classes like
`lgfx::Panel_ILI9488` + `lgfx::Bus_SPI` + `lgfx::Touch_XPT2046`, boardghost
auto-detects it and rewrites the setup file at build time into a
`lgfx::Panel_sdl`-backed version that matches your declared panel dimensions
and rotation. All `#define` directives in your setup file (pin numbers, etc.)
are preserved verbatim.

Your original sketch on disk is **never modified**. The rewritten copy lives
at `.boardghost/<board>/sketch_src/<sketch-name>/`.

```bash
# Just build — the codemod fires transparently.
boardghost run --board st7789_esp32s3_sim my-cool-sketch/
```

You'll see a line like:

```
→ LGFX codemod: rewriting lvgfx_setup.h → Panel_sdl 320x480 (rotation 2)
```

### When the codemod skips

It skips silently when:
- The setup file already references an `LGFX_*_SDL.hpp` header (you've already
  done the swap).
- The regex parser can't extract panel dimensions (e.g. they come from a
  `#define` instead of literal numbers in the config). Add the dims as
  literals in your hardware setup, or open an issue with the source so the
  parser can be extended.

### Optional sim helpers

If your sketch calls `lcd.calibrateTouch(...)`, set
`BOARDGHOST_AUTO_TOUCH_CAL=1` in the environment so the sim's Touch_sdl
cycles through screen corners automatically and the calibration completes
without user input.
