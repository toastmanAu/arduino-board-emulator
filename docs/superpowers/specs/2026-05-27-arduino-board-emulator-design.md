# BoardGhost — Arduino/ESP32 Board & TFT Display Simulator

**Spec date:** 2026-05-27
**Status:** Design approved, ready for implementation planning
**Working name:** BoardGhost (placeholder — bikeshed later)

---

## 1. Purpose

A desktop tool that lets unmodified Arduino/ESP32 sketches — specifically those using LovyanGFX and/or LVGL — compile and run on a host machine with a simulated TFT/OLED display. Optimised first for the author's workflow (ESP32 + Arduino + ILI9488), built so that others can pick it up later. Not a cycle-accurate MCU emulator; a fast, developer-friendly **fake board runtime** for GUI work.

The killer use case: *"Open my LVGL ESP32 project, hit run, see the UI appear in a window without flashing hardware."*

## 2. Decisions locked in during brainstorming

| Topic | Decision |
|---|---|
| Audience | Author first; designed for portability/reproducibility so others can use it (Docker option, documented setup). Open-source friendly without being a polished product. |
| Sketch integration | **Transparent shim**: same header names as real Arduino/LovyanGFX/LVGL, resolved via include-path precedence. Unmodified sketch source compiles for sim and real ESP32. |
| Process model | **Standalone sketch binary + separate launcher**. Sketch is its own native executable; launcher is a separate Tauri app that spawns it. |
| Launcher tech | **Tauri (Rust + webview)** with Svelte + TypeScript frontend. |
| M1 displays | **ILI9488 480×320** (must-have) + **SSD1306 128×64 mono** (forces the framebuffer abstraction to handle different bit depths from day one). |
| M1 inputs | Touch only (mouse → XPT2046/FT6236-shaped events). |
| Build toolchain | **Arduino CLI** (`arduino-cli`) — author uses Arduino more than PlatformIO. PlatformIO support deferred to M2. |
| M1 observability | Serial monitor + framebuffer view. GPIO/SPI/I2C capture deferred to M2. |
| Display window | Sketch opens its own SDL2 window directly; launcher is a separate control window. |
| Integration approach | **B — Wrapper Tool**: `boardghost` CLI calls `arduino-cli --preprocess` then drives its own CMake build with shim headers. Launcher invokes the same CLI. |
| String class | Thin wrapper around `std::string` (~80% Arduino-compatible). |
| Unshimmed APIs | Hard compile error with clear message and `#ifndef BOARDGHOST_SIM` escape-hatch documentation. |
| Library policy | Strict allowlist (LovyanGFX, LVGL, Adafruit_GFX). Unknown libraries rejected with a clear message. |

## 3. Architecture overview

Three independent layers connected only through the CLI contract:

```
┌──────────────────────────┐      ┌──────────────────────────────────────┐
│  Tauri Launcher (Rust)   │      │  Sketch Process (native binary)      │
│  - Project list          │      │  ┌────────────────────────────────┐  │
│  - Display config UI     │      │  │ User sketch (setup/loop)       │  │
│  - "Build & Run" button  │      │  │   ↑ links against              │  │
│  - Serial log panel      │      │  │ Shims: Arduino.h, SPI, Wire    │  │
│  - Build output panel    │      │  │   ↑ which delegate to          │  │
└────────┬─────────────────┘      │  │ LovyanGFX-PC (SDL2 backend)    │  │
         │ spawns                 │  │ LVGL-PC drivers                │  │
         │ + stdout pipe          │  │ Sim runtime (millis, GPIO log) │  │
         ▼                        │  └──────────┬─────────────────────┘  │
┌──────────────────────────┐      │             │ opens                  │
│  boardghost CLI (Rust)   │─────▶│             ▼                        │
│  1. arduino-cli          │      │  ┌────────────────────────────────┐  │
│     --preprocess         │      │  │  SDL2 window (480×320 / 128×64)│  │
│  2. CMake configure      │      │  │  mouse → touch events          │  │
│  3. CMake build          │      │  └────────────────────────────────┘  │
│  4. exec ./sketch_bin    │      │  stdout → Serial.print() log         │
└──────────────────────────┘      └──────────────────────────────────────┘
```

### 3.1 Languages and major dependencies

| Layer | Language | Major deps |
|---|---|---|
| Launcher | Rust (Tauri v2) | `tauri`, `tokio`, `serde` |
| CLI | Rust | `clap`, `anyhow`, `tokio` (for child process streaming) |
| Sketch runtime | C/C++17 | SDL2, LovyanGFX (PC build, vendored), LVGL v9 (PC build, vendored), Arduino-shim (ours) |
| Build glue | CMake | — |
| Frontend (launcher webview) | TypeScript + Svelte | — |

### 3.2 Repository layout

```
arduino-board-emulator/
├── crates/
│   ├── boardghost-cli/          Rust CLI
│   └── boardghost-launcher/     Tauri app (frontend in src/, Rust in src-tauri/)
├── runtime/                     C++ sim runtime (built per-sketch via CMake)
│   ├── shims/
│   │   ├── Arduino.h
│   │   ├── SPI.h
│   │   ├── Wire.h
│   │   └── sim_runtime.cpp      millis/delay/GPIO state/serial bridge
│   ├── displays/
│   │   ├── ili9488_sdl.{hpp,cpp}
│   │   └── ssd1306_sdl.{hpp,cpp}
│   ├── boards/                  TOML board profiles
│   │   ├── ili9488_esp32s3_sim.toml
│   │   └── ssd1306_uno_sim.toml
│   ├── third_party/             vendored LovyanGFX-PC, LVGL-PC (git submodules)
│   ├── tests/                   GoogleTest unit + golden-image tests
│   └── CMakeLists.txt
├── examples/
│   ├── hello_serial/
│   ├── lvgl_hello_ili9488/
│   └── ssd1306_text/
├── docs/
│   └── superpowers/specs/
│       └── 2026-05-27-arduino-board-emulator-design.md   ← this file
├── tests/
│   └── e2e/                     headless end-to-end scripts
├── README.md
└── arduinoBoardEditor           original brainstorming chat (kept for reference)
```

### 3.3 Hard contract: the CLI

Every higher-level surface (Tauri launcher, future Arduino platform package, CI) goes through the CLI:

```
boardghost build  <project_dir> --board <board>   [--release | --debug]
boardghost run    <project_dir> --board <board>   [--release | --debug]
boardghost list-boards
boardghost doctor                                 # checks arduino-cli, cmake, SDL2
```

`run` = `build` then `exec ./sketch`. The Tauri launcher just spawns `boardghost run` and pipes stdout.

## 4. The shim and runtime

### 4.1 Arduino core shim — `runtime/shims/Arduino.h` + `sim_runtime.cpp`

| API | Sim implementation |
|---|---|
| `millis()`, `micros()` | Wallclock since process start (`std::chrono::steady_clock`) |
| `delay(ms)`, `delayMicroseconds(us)` | `std::this_thread::sleep_for` |
| `pinMode()`, `digitalWrite()`, `digitalRead()` | In-memory `pin_state[NUM_PINS]` table; logged but otherwise inert |
| `analogRead()`, `analogWrite()` | Reads/writes `pin_state[].analog`; M1 returns 0 unless overridden via env var (`BOARDGHOST_ANALOG_<PIN>=<val>`) |
| `Serial.print/println/printf/write` | Writes to stdout (captured by launcher pipe) |
| `Serial.read/available` | Reads from stdin (lets launcher inject input in future) |
| `String` | Thin wrapper around `std::string` with `.c_str()`, `.length()`, `+`, `.toInt()`, `.toFloat()`, `.indexOf()`, `.substring()`, `.startsWith()` |
| `setup()`, `loop()` | User defines; our injected `main()` calls `setup()` once then `loop()` in a `while(!quit)` with `sim_pump_events()` after each iteration |
| `yield()` | No-op in M1 (single-threaded); pumps SDL events in a later milestone |
| `random(min, max)` | `std::uniform_int_distribution` |

The injected `main()`:

```cpp
int main(int argc, char** argv) {
    sim_runtime_init(argc, argv);     // parses --board / env vars
    setup();
    while (!sim_should_quit()) {
        loop();
        sim_pump_events();             // pumps SDL, dispatches touch
    }
    sim_runtime_shutdown();
    return 0;
}
```

### 4.2 Display layer — reuse, don't reinvent

| Library | Strategy |
|---|---|
| **LovyanGFX** | Vendor LovyanGFX as a submodule; use its existing `Panel_sdl` backend (gated by `LGFX_USE_PANEL_SDL`). We provide thin concrete classes `LGFX_ILI9488_SDL` and `LGFX_SSD1306_SDL` that configure `Panel_sdl` with the right resolution / colour order / pixel format. User code does `LGFX tft;` as today. |
| **LVGL v9** | Vendor LVGL v9 as a submodule; its built-in SDL display + indev drivers are activated via `lv_conf.h`. Our shim's `lv_init()` wraps the user's call and registers the SDL display+input against the same window the LovyanGFX panel opens (LVGL renders into LovyanGFX's framebuffer in a typical user setup). |
| **Adafruit_SSD1306** (M1) | **Not shimmed.** Users wanting SSD1306 in M1 must use LovyanGFX. Documented limitation. Shim deferred to M2. |

### 4.3 Bus shims — `SPI.h`, `Wire.h`

Near-no-op in M1. LovyanGFX's PC build talks to its SDL panel directly; it does not go through `SPI.h`. So `SPI.begin()` / `SPI.transfer()` calls in user code outside the display path (e.g., SD card reads, MCP23017) are *logged* but inert:

```cpp
class SPIClass {
public:
    void begin() { sim_log("SPI.begin()"); }
    uint8_t transfer(uint8_t b) { sim_log_spi_byte(b); return 0; }
    void transferBytes(const uint8_t* data, uint8_t* out, size_t n);
    // ...
};
extern SPIClass SPI;
```

True SPI/I2C *traffic capture* with decoded transactions is an M2 observability feature.

### 4.4 Explicitly NOT shimmed in M1

Calling these out so scope is honest. Any sketch using these gets a hard compile error and is expected to gate with `#ifndef BOARDGHOST_SIM`:

- WiFi / BLE / NimBLE / ESPAsyncWebServer
- FreeRTOS task primitives (`xTaskCreate`, `vTaskDelay`, queues, semaphores, mutexes)
- ESP-IDF native APIs (`esp_log`, `nvs_flash`, `gpio_config`, `driver/*.h`)
- File systems (LittleFS, SPIFFS, SD)
- RMT / I2S / DAC / touch-pad / ULP / Camera

The M1 promise is: **"if your sketch uses Arduino core + LovyanGFX + LVGL + SPI/Wire calls, it will run."**

## 5. Build pipeline

### 5.1 Stages of `boardghost build`

```
Stage 1: Discover
  • Find sketch.ino (or src/main.cpp)
  • Parse #include lines for library hints
  • Validate --board against runtime/boards/*.toml

Stage 2: Preprocess
  • arduino-cli compile --preprocess --fqbn <board.arduino_fqbn_hint>
  • Produces a single concatenated .cpp with auto-generated forward decls
  • The real-ESP32 FQBN is used ONLY for preprocessing, never for compile

Stage 3: Resolve libraries
  • arduino-cli compile --show-properties → which library paths it resolved
  • Filter against the allowlist (LovyanGFX, lvgl, Adafruit_GFX)
  • Reject unknown libraries with a clear message

Stage 4: Generate build dir
  • Write CMakeLists.txt to .boardghost/<board>/
    - Includes user .cpp(s), shim headers first in include path,
      vendored LovyanGFX-PC, LVGL-PC, SDL2
    - target_compile_definitions: BOARDGHOST_SIM, LGFX_USE_PANEL_SDL,
      BOARD_PROFILE_<NAME>

Stage 5: Compile
  • cmake -S .boardghost/<board> -B .boardghost/<board>/build
  • cmake --build .boardghost/<board>/build -j
  • Output: .boardghost/<board>/build/sketch
```

### 5.2 Board profile format

Data, not code. Each supported board is a TOML in `runtime/boards/`:

```toml
# runtime/boards/ili9488_esp32s3_sim.toml
name                = "ili9488_esp32s3_sim"
description         = "ESP32-S3 with ILI9488 480x320 SPI display"
arduino_fqbn_hint   = "esp32:esp32:esp32s3"     # for preprocess only

[display]
controller          = "ILI9488"
width               = 480
height              = 320
rotation            = 1
bus                 = "spi"
color_depth         = 16

[touch]
controller          = "xpt2046"
```

Adding a new display in M2 = a new TOML plus (if a new controller class is needed) a new `displays/<name>_sdl.cpp`. No CLI changes.

### 5.3 Error UX

Each stage emits typed errors that the launcher passes through verbatim. Examples:

```
✗ Preprocess failed (arduino-cli exit 1)
  arduino-cli could not resolve library "LovyanGFX".
  Install it with: arduino-cli lib install LovyanGFX
```

```
✗ Unsupported library: WiFi.h
  M1 does not shim networking. To skip this code in sim builds:
    #ifndef BOARDGHOST_SIM
      WiFi.begin(ssid, pass);
    #endif
```

```
✗ Unknown board: ili9341_sim
  Run `boardghost list-boards` to see supported boards.
  M1 ships with: ili9488_esp32s3_sim, ssd1306_uno_sim.
```

## 6. Launcher (Tauri)

### 6.1 UI layout

Single window, two columns. Project list left, project detail + logs right. The TFT itself appears as a separate native SDL window (sketch process owns it).

```
┌────────────────────────────────────────────────────────────────────┐
│  BoardGhost                                              ─ □ ✕     │
├──────────────────────────┬─────────────────────────────────────────┤
│ Recent projects          │  Project: ~/code/lvgl-demo              │
│ ───────────────          │                                         │
│ ▸ lvgl-demo  (today)     │  Board:  [ ili9488_esp32s3_sim   ▼ ]    │
│   ssd1306-clock (Mon)    │                                         │
│   ...                    │  [   ▶  Build & Run   ]   [ Stop ]      │
│                          │  ─────────────────────────────────────  │
│ [ + Open Project ]       │  Build output:    [ scroll region ]     │
│                          │  Serial monitor:  [ scroll region ]     │
└──────────────────────────┴─────────────────────────────────────────┘
```

### 6.2 Tauri backend

Three responsibilities:

1. Persist `~/.config/boardghost/projects.json` (recent projects + last-used board per project).
2. Spawn `boardghost run` via `tokio::process::Command` with piped stdout + stderr.
3. Forward log lines to the webview via Tauri events (`emit("build-log", line)`, `emit("serial-log", line)`).

Tauri commands exposed to the frontend: `open_project`, `build_and_run`, `stop`, `list_boards`. Nothing else in M1.

### 6.3 Stream demuxing

Build output and serial output never need demuxing — they come from different child invocations. The CLI emits build progress on its own stdout during stages 1–5; once it `exec`s the sketch binary, that binary's stdout *is* the serial stream.

### 6.4 Lifecycle

```
User clicks "Build & Run"
    → Tauri spawns `boardghost run <proj> --board <board>`
    → CLI streams build lines to stdout (build panel)
    → On successful exec, CLI streams sketch stdout (serial panel)
    → SDL window appears (owned by sketch process)
User clicks "Stop"
    → Tauri sends SIGTERM to child, then SIGKILL after 2 s
    → SDL window closes; serial log freezes (does not clear)
Sketch crashes
    → Child exits non-zero; launcher shows "Sketch exited (code <N>, <signame>)"
    → Logs retained
```

### 6.5 NOT in launcher M1

- No code editor
- No syntax/compile error inline highlighting (build errors are plain text)
- No project creation wizard (existing folders only)
- No multi-sketch parallel runs (one at a time)
- No screenshot/recording controls (use OS tools on the SDL window)

## 7. Testing strategy

| Layer | Test type | Location |
|---|---|---|
| Sim runtime (C++) | GoogleTest unit tests for `millis`, `delay`, pin state, `String` wrapper, event pump | `runtime/tests/` |
| Display shims | Golden-image: draw a fixed pattern, hash framebuffer, compare to baseline PNG | `runtime/tests/displays/` |
| CLI | Integration: build each `examples/` project, assert exit code + presence of `sketch` binary | Rust `#[test]` in `crates/boardghost-cli/tests/` |
| End-to-end | `boardghost run examples/lvgl_hello_ili9488` headlessly with `SDL_VIDEODRIVER=dummy`, run 2 s, capture stdout, assert expected `Serial.print` appears | `tests/e2e/` |
| Launcher | Tauri webdriver: spawn launcher, click "Build & Run" on a known example, assert serial log populates | `crates/boardghost-launcher/tests/` |

**Coverage target:** 80% line coverage on the sim runtime (per global testing rules). CLI/launcher coverage is measured via the E2E suite rather than line-coverage targets.

## 8. Example projects (also smoke tests)

Three ship in `examples/`:

1. **`hello_serial/`** — `Serial.println("Hello"); delay(1000);` — validates Arduino core shim without a display.
2. **`lvgl_hello_ili9488/`** — LVGL label + button on ILI9488 — validates the full LVGL + LovyanGFX + ILI9488 stack and touch input.
3. **`ssd1306_text/`** — LovyanGFX scrolling text on SSD1306 — validates mono panel + bit-depth-aware framebuffer.

## 9. Error handling philosophy

| Category | Example | Strategy |
|---|---|---|
| User project errors | sketch has C++ syntax error | Pass compiler stderr through verbatim |
| Environment errors | arduino-cli missing, SDL2 missing | `boardghost doctor` detects up front; typed error with install hint |
| Internal errors | CMake template bug | `anyhow` chain + `RUST_BACKTRACE=1` hint in stderr |

No swallowed errors. All user-facing. No secrets are in scope.

## 10. M1 acceptance criteria

M1 is done when **all** of:

- [ ] `boardghost doctor` reports green on a clean Linux machine after the documented setup
- [ ] All three example projects build and run via `boardghost run` from the CLI
- [ ] The Tauri launcher opens, lists those examples, builds, runs, and displays serial output
- [ ] An unmodified copy of one of the author's real LVGL+LovyanGFX ESP32 projects runs to the point of showing the UI on the simulated ILI9488 (allowing for `#ifndef BOARDGHOST_SIM` blocks around WiFi/etc.)
- [ ] Touch (mouse) input drives LVGL button presses
- [ ] Sketch crashes do not crash the launcher
- [ ] CI runs the headless E2E suite green
- [ ] README has a 5-minute getting-started

## 11. Out of scope for M1

- Other displays (ILI9341, ST7789, ST7796, GC9A01, SH1106, ST7735) — **M2**
- GPIO panel UI / virtual buttons — **M2**
- Analog input sliders — **M2**
- Adafruit_SSD1306 shim — **M2**
- SPI / I2C byte capture and decoded transaction view — **M2**
- Screenshot / video recording UI — **M2**
- PlatformIO support — **M2**
- ESP32 QEMU backend — **M3**
- WiFi/BLE/FreeRTOS shimming — **M3+**
- Custom Arduino platform package (Approach A from brainstorming) — **M3**
- macOS and Windows support — best-effort, no M1 commitment

## 12. Risks

1. **arduino-cli `--preprocess` output format is not a documented contract.** Pin a tested arduino-cli version range in `boardghost doctor`; treat its output as the API and add a regression test.
2. **LovyanGFX-PC SDL backend drift.** Its SDL backend exists but is not the project's primary target. Vendor as a git submodule at a known-good SHA, not as a moving dependency.
3. **Tauri v2 tooling maturity.** Younger than v1 but the right long-term choice. Accept the risk.
4. **Single-threaded sketch loop blocks SDL event pump.** If a user's `loop()` blocks >100 ms, the SDL window stutters. Document as an M1 limitation; FreeRTOS-style task simulation is M3.
5. **LVGL v9 + LovyanGFX double-framebuffer pitfall.** LVGL flushes to a buffer that LovyanGFX then blits to the SDL panel — a misconfigured `lv_conf.h` can cause double-buffering corruption. Provide a canonical `lv_conf.h` in `examples/` and document the buffer-ownership rule in the runtime README.

## 13. Open questions deferred to implementation

These are intentionally not decided here — they belong to the implementation plan or M2:

- Final project/binary name (BoardGhost is a placeholder).
- Whether to rename the top-level directory from `arduinoBoardEditor` (typo) to `arduino-board-emulator` (current dir name already matches).
- Exact arduino-cli version range to pin.
- LovyanGFX-PC + LVGL submodule SHAs.
- Tauri v2 minimum version.
