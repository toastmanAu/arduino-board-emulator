# Getting started

## 1. Install host tools

See [README.md](../README.md#requirements).

Verify:

```bash
boardghost doctor
```

You should see `ok` for arduino-cli, cmake, ninja, and SDL2.

## 2. Install the Arduino libraries you want to use

Sketches must declare their library dependencies the normal Arduino way. Install them globally:

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install LovyanGFX
arduino-cli lib install lvgl
```

## 3. Run a built-in example

```bash
boardghost list-boards
boardghost run examples/hello_serial --board ssd1306_uno_sim
```

## 4. Open your own sketch

Your project should look like one of these layouts:

```
my-project/
├── sketch/
│   └── sketch.ino       (or any .ino — boardghost picks the first alphabetically)
```

or:

```
my-project/
├── src/
│   └── main.cpp         (PlatformIO-style)
```

Then:

```bash
boardghost run /path/to/my-project --board ili9488_esp32s3_sim
```

## 5. Handle unsupported APIs

If your sketch uses WiFi/BLE/FreeRTOS/ESP-IDF, wrap those calls:

```cpp
#ifndef BOARDGHOST_SIM
  WiFi.begin(ssid, password);
#endif
```

## 5b. Exiting cleanly from loop()

If your sketch has global LovyanGFX panel objects (e.g., `LGFX_ILI9488_SDL tft;`)
and you want to exit from `loop()` (e.g., for headless testing), use `_exit(0)`
from `<unistd.h>` instead of `std::exit(0)`:

```cpp
#include <unistd.h>
void loop() {
    // ... your code ...
    if (done) {
        Serial.flush();
        _exit(0);     // bypasses C++ destructor chain
    }
}
```

`std::exit(0)` runs static destructors, but LovyanGFX's `Panel_sdl` destructor
touches static state that may already be torn down — this can segfault. `_exit`
exits the process immediately without running C++ destructors.

## 6. Tune analog inputs

Override `analogRead(pin)` values from the environment:

```bash
BOARDGHOST_ANALOG_34=2048 boardghost run my-project --board ili9488_esp32s3_sim
```

## 7. Capture output

Serial goes to stdout; runtime diagnostics go to stderr.

```bash
boardghost run my-project --board ili9488_esp32s3_sim 1>serial.log 2>runtime.log
```

## 8. Using the desktop launcher

### Dev mode

Launch the Tauri desktop app in development mode:

```bash
cd crates/boardghost-launcher
npm install
npm run tauri dev
```

The launcher auto-locates the runtime tree via `CARGO_MANIFEST_DIR` in dev builds — no environment variables needed.

### Production build

Build the production launcher:

```bash
cd crates/boardghost-launcher
npm run tauri build
```

Outputs land at:
- Bare binary: `target/release/boardghost-launcher`
- Linux installers: `target/release/bundle/{deb,rpm}/`

### Prerequisite

`boardghost` must be on PATH. The launcher shells out to it; if missing, board listing fails.

Easiest way to set it up:

```bash
cargo build --release -p boardghost-cli
install -m 0755 target/release/boardghost ~/.local/bin/boardghost
```

Verify:

```bash
which boardghost
boardghost doctor
```

**Note:** In dev mode the launcher auto-locates the runtime tree, but production binaries currently require `BOARDGHOST_BOARDS` and `BOARDGHOST_RUNTIME` environment variables to find the board definitions and simulator resources. (Resource bundling is planned for M2.)
