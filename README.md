# BoardGhost

Run unmodified ESP32 + Arduino sketches that use **LovyanGFX** and **LVGL** on your desktop. No flashing, no real hardware.

**Status:** M2.A complete. Engine, CLI, launcher, and 7 displays + touch + GPIO + screenshot shipping.

## What works

- 7 simulated panels (LovyanGFX + LVGL): ILI9488 480×320, ILI9341 320×240,
  ST7789 240×320, ST7796 480×320, GC9A01 240×240 round, ST7735 160×128,
  SSD1306 128×64 mono
- Touch driver covering XPT2046 / FT6236 / GT911 — `tft.getTouch(&x, &y)`
  works in user code on any touch-enabled board
- Mouse → touch input (LVGL indev + LovyanGFX touch API)
- `Serial.print` capture
- GPIO inspector (live pin-state grid in the launcher)
- CLI: `boardghost {list-boards|doctor|build|run}` with `--screenshot PATH`
- Tauri desktop launcher with GPIO inspector + screenshot button
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
