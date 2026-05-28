# BoardGhost

Run unmodified ESP32 + Arduino sketches that use **LovyanGFX** and **LVGL** on your desktop. No flashing, no real hardware.

**Status:** M1 shipping. Engine + CLI tagged at `m1a-engine`. Launcher (M1.B) building.

## What works (M1.A)

- ILI9488 480×320 simulated panel
- SSD1306 128×64 mono simulated panel
- LovyanGFX + LVGL v9 graphics
- Mouse → touch input
- `Serial.print`/`println`/`printf` capture
- Headless mode (`SDL_VIDEODRIVER=dummy`) for CI
- Tauri desktop launcher (`npm run tauri dev` in `crates/boardghost-launcher`)

## What doesn't work yet

- Other display controllers (ILI9341, ST7789, ST7796, GC9A01, SH1106, ST7735) — M2
- GPIO inspector / virtual buttons — M2
- SPI/I2C transaction decoding — M2
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
