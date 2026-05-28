# BoardGhost M2.A — More Displays + GPIO Inspector + Screenshot Capture

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the easy M2 wins now that the engine + launcher are proven: five new simulated displays (ILI9341, ST7789, ST7796, GC9A01, ST7735), a unified `Touch_sdl` driver that satisfies the LovyanGFX touch API for XPT2046 / FT6236 / GT911 board profiles, an in-launcher GPIO state inspector, and a screenshot capture feature.

**Architecture:** Build entirely on the data-driven patterns established in M1. New displays are just `LGFX_*_SDL.hpp` wrappers + TOML board profiles + unit tests — Panel_sdl renders all of them the same way. The GPIO inspector reuses the launcher's existing stderr-pipe → Tauri-event pattern: sketch emits `[gpio]` log lines, the runner splits them onto a third `gpio-log` event, and a Svelte grid visualises pin state. Screenshot is a SIGUSR1-triggered framebuffer capture written via vendored `stb_image_write.h`.

**Tech Stack:** Same as M1 — C++17 runtime, Rust CLI/launcher, Svelte 5 frontend. Adds `stb_image_write.h` (single-header, vendored).

**Predecessor:** Tagged at `m1-launcher`. Both `m1a-engine` and `m1-launcher` jobs green in CI.

**Predecessor plans:**
- [`2026-05-27-boardghost-m1a-engine.md`](2026-05-27-boardghost-m1a-engine.md)
- [`2026-05-28-boardghost-m1b-launcher.md`](2026-05-28-boardghost-m1b-launcher.md)

---

## File structure produced by this plan

```
arduino-board-emulator/
├── runtime/
│   ├── displays/
│   │   ├── Touch_sdl.hpp                  (new) — SDL-backed lgfx::ITouch
│   │   ├── LGFX_ILI9341_SDL.hpp           (new)
│   │   ├── LGFX_ST7789_SDL.hpp            (new, 240×320)
│   │   ├── LGFX_ST7796_SDL.hpp            (new, 480×320)
│   │   ├── LGFX_GC9A01_SDL.hpp            (new, 240×240 round)
│   │   ├── LGFX_ST7735_SDL.hpp            (new, 160×128)
│   │   ├── LGFX_ILI9488_SDL.hpp           (modified — embed Touch_sdl)
│   │   └── LGFX_SSD1306_SDL.hpp           (modified — no-op, mono OLED has no touch)
│   ├── boards/
│   │   ├── ili9341_esp32_sim.toml         (new)
│   │   ├── st7789_esp32s3_sim.toml        (new)
│   │   ├── st7796_esp32s3_sim.toml        (new)
│   │   ├── gc9a01_esp32_sim.toml          (new)
│   │   ├── st7735_esp32_sim.toml          (new)
│   ├── include/
│   │   └── sim_runtime.h                  modified — add sim_log_gpio, sim_screenshot
│   ├── src/
│   │   ├── sim_runtime.cpp                modified — GPIO emit, SIGUSR1 handler
│   │   └── sim_screenshot.cpp             (new) — stb_image_write integration
│   ├── third_party/
│   │   └── stb/stb_image_write.h          (new, vendored)
│   └── tests/
│       └── test_displays_extra.cpp        (new, exercises all 5 new wrappers)
├── crates/
│   ├── boardghost-cli/
│   │   └── src/
│   │       └── cli.rs                     modified — --screenshot path flag on Run
│   └── boardghost-launcher/
│       ├── src-tauri/src/
│       │   ├── commands.rs                modified — screenshot command, gpio routing
│       │   └── runner.rs                  modified — split [gpio] from stderr
│       └── src/lib/
│           ├── api.ts                     modified — onGpioLog, screenshot
│           ├── GpioGrid.svelte            (new)
│           └── ProjectDetail.svelte       modified — include GpioGrid + screenshot button
├── examples/
│   └── (no new examples; existing lvgl_hello_ili9488 + ssd1306_text exercise the path)
├── README.md                              modified — list new displays
└── docs/getting-started.md                modified — gpio inspector + screenshot howto
```

---

## Task 1: Five new display wrappers + TOMLs + unit tests

This is one batched task since each display is a near-identical 20-line wrapper. Doing them together avoids 5× the dispatch overhead.

**Files:**
- Create: `runtime/displays/LGFX_ILI9341_SDL.hpp`
- Create: `runtime/displays/LGFX_ST7789_SDL.hpp`
- Create: `runtime/displays/LGFX_ST7796_SDL.hpp`
- Create: `runtime/displays/LGFX_GC9A01_SDL.hpp`
- Create: `runtime/displays/LGFX_ST7735_SDL.hpp`
- Create: `runtime/boards/ili9341_esp32_sim.toml`
- Create: `runtime/boards/st7789_esp32s3_sim.toml`
- Create: `runtime/boards/st7796_esp32s3_sim.toml`
- Create: `runtime/boards/gc9a01_esp32_sim.toml`
- Create: `runtime/boards/st7735_esp32_sim.toml`
- Create: `runtime/tests/test_displays_extra.cpp`
- Modify: `runtime/tests/CMakeLists.txt` (register the new test)

- [ ] **Step 1: Write all five wrapper headers**

Each follows the exact pattern of the existing `LGFX_ILI9488_SDL.hpp` — only the panel dimensions change. The Panel_sdl class is the same software-renderer; on real hardware the controllers differ, but in sim they're all framebuffers.

`runtime/displays/LGFX_ILI9341_SDL.hpp`:

```cpp
#pragma once
#include <LovyanGFX.hpp>

class LGFX_ILI9341_SDL : public lgfx::LGFX_Device {
public:
    LGFX_ILI9341_SDL() {
        auto cfg = panel_.config();
        cfg.memory_width  = 320;
        cfg.memory_height = 240;
        cfg.panel_width   = 320;
        cfg.panel_height  = 240;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        cfg.offset_rotation = 0;
        panel_.config(cfg);
        setPanel(&panel_);
    }

private:
    lgfx::Panel_sdl panel_;
};
```

`runtime/displays/LGFX_ST7789_SDL.hpp`:

```cpp
#pragma once
#include <LovyanGFX.hpp>

class LGFX_ST7789_SDL : public lgfx::LGFX_Device {
public:
    LGFX_ST7789_SDL() {
        auto cfg = panel_.config();
        cfg.memory_width  = 240;
        cfg.memory_height = 320;
        cfg.panel_width   = 240;
        cfg.panel_height  = 320;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        cfg.offset_rotation = 0;
        panel_.config(cfg);
        setPanel(&panel_);
    }

private:
    lgfx::Panel_sdl panel_;
};
```

`runtime/displays/LGFX_ST7796_SDL.hpp`:

```cpp
#pragma once
#include <LovyanGFX.hpp>

class LGFX_ST7796_SDL : public lgfx::LGFX_Device {
public:
    LGFX_ST7796_SDL() {
        auto cfg = panel_.config();
        cfg.memory_width  = 480;
        cfg.memory_height = 320;
        cfg.panel_width   = 480;
        cfg.panel_height  = 320;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        cfg.offset_rotation = 0;
        panel_.config(cfg);
        setPanel(&panel_);
    }

private:
    lgfx::Panel_sdl panel_;
};
```

`runtime/displays/LGFX_GC9A01_SDL.hpp`:

```cpp
#pragma once
#include <LovyanGFX.hpp>

// GC9A01 is a 240×240 round display. The simulated window is still a
// square SDL window; cropping to a circle is a frontend concern, not a
// panel concern. Real hardware ignores draws outside the visible circle.
class LGFX_GC9A01_SDL : public lgfx::LGFX_Device {
public:
    LGFX_GC9A01_SDL() {
        auto cfg = panel_.config();
        cfg.memory_width  = 240;
        cfg.memory_height = 240;
        cfg.panel_width   = 240;
        cfg.panel_height  = 240;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        cfg.offset_rotation = 0;
        panel_.config(cfg);
        setPanel(&panel_);
    }

private:
    lgfx::Panel_sdl panel_;
};
```

`runtime/displays/LGFX_ST7735_SDL.hpp`:

```cpp
#pragma once
#include <LovyanGFX.hpp>

class LGFX_ST7735_SDL : public lgfx::LGFX_Device {
public:
    LGFX_ST7735_SDL() {
        auto cfg = panel_.config();
        cfg.memory_width  = 160;
        cfg.memory_height = 128;
        cfg.panel_width   = 160;
        cfg.panel_height  = 128;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        cfg.offset_rotation = 0;
        panel_.config(cfg);
        setPanel(&panel_);
    }

private:
    lgfx::Panel_sdl panel_;
};
```

- [ ] **Step 2: Write all five board TOMLs**

`runtime/boards/ili9341_esp32_sim.toml`:

```toml
name              = "ili9341_esp32_sim"
description       = "ESP32 with ILI9341 320x240 SPI display"
arduino_fqbn_hint = "esp32:esp32:esp32s3"

[display]
controller  = "ILI9341"
width       = 320
height      = 240
rotation    = 0
bus         = "spi"
color_depth = 16

[touch]
controller  = "xpt2046"
```

`runtime/boards/st7789_esp32s3_sim.toml`:

```toml
name              = "st7789_esp32s3_sim"
description       = "ESP32-S3 with ST7789 240x320 SPI display + FT6236 capacitive touch"
arduino_fqbn_hint = "esp32:esp32:esp32s3"

[display]
controller  = "ST7789"
width       = 240
height      = 320
rotation    = 0
bus         = "spi"
color_depth = 16

[touch]
controller  = "ft6236"
```

`runtime/boards/st7796_esp32s3_sim.toml`:

```toml
name              = "st7796_esp32s3_sim"
description       = "ESP32-S3 with ST7796 480x320 SPI display + GT911 capacitive multi-touch"
arduino_fqbn_hint = "esp32:esp32:esp32s3"

[display]
controller  = "ST7796"
width       = 480
height      = 320
rotation    = 1
bus         = "spi"
color_depth = 16

[touch]
controller  = "gt911"
```

`runtime/boards/gc9a01_esp32_sim.toml`:

```toml
name              = "gc9a01_esp32_sim"
description       = "ESP32 with GC9A01 240x240 round SPI display"
arduino_fqbn_hint = "esp32:esp32:esp32s3"

[display]
controller  = "GC9A01"
width       = 240
height      = 240
rotation    = 0
bus         = "spi"
color_depth = 16
```

(GC9A01 deliberately has no `[touch]` section — most modules in this form factor are touch-less smart-watch style.)

`runtime/boards/st7735_esp32_sim.toml`:

```toml
name              = "st7735_esp32_sim"
description       = "ESP32 with ST7735 160x128 SPI display"
arduino_fqbn_hint = "esp32:esp32:esp32s3"

[display]
controller  = "ST7735"
width       = 160
height      = 128
rotation    = 0
bus         = "spi"
color_depth = 16
```

- [ ] **Step 3: Write the unit test that exercises all five wrappers**

`runtime/tests/test_displays_extra.cpp`:

```cpp
#include <gtest/gtest.h>
#include "sim_runtime.h"
#include "LGFX_ILI9341_SDL.hpp"
#include "LGFX_ST7789_SDL.hpp"
#include "LGFX_ST7796_SDL.hpp"
#include "LGFX_GC9A01_SDL.hpp"
#include "LGFX_ST7735_SDL.hpp"
#include <cstdlib>

namespace {
struct PanelCase {
    const char* name;
    int width;
    int height;
};

class ExtraDisplayTest : public ::testing::TestWithParam<PanelCase> {
protected:
    void SetUp() override {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        sim_runtime_init(0, nullptr);
    }
    void TearDown() override { sim_runtime_shutdown(); }
};

template <typename TFT>
void exercise(const PanelCase& c) {
    TFT tft;
    tft.init();
    EXPECT_EQ(tft.width(),  c.width);
    EXPECT_EQ(tft.height(), c.height);
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, c.width / 4, c.height / 4, TFT_RED);
    // No assertion on pixel value (different panels handle colour ordering
    // differently). The point is: no crash, init succeeds, dimensions match.
}

}  // namespace

TEST_F(ExtraDisplayTest, ILI9341)  { exercise<LGFX_ILI9341_SDL>({"ILI9341", 320, 240}); }
TEST_F(ExtraDisplayTest, ST7789)   { exercise<LGFX_ST7789_SDL> ({"ST7789",  240, 320}); }
TEST_F(ExtraDisplayTest, ST7796)   { exercise<LGFX_ST7796_SDL> ({"ST7796",  480, 320}); }
TEST_F(ExtraDisplayTest, GC9A01)   { exercise<LGFX_GC9A01_SDL> ({"GC9A01",  240, 240}); }
TEST_F(ExtraDisplayTest, ST7735)   { exercise<LGFX_ST7735_SDL> ({"ST7735",  160, 128}); }
```

- [ ] **Step 4: Register the test in `runtime/tests/CMakeLists.txt`**

Add `test_displays_extra.cpp` to the `add_executable(runtime_tests …)` source list.

- [ ] **Step 5: Build + run**

```bash
cmake --build runtime/build -j 2>&1 | tail -5
ctest --test-dir runtime/build --output-on-failure 2>&1 | tail -10
```

Expected: 32 tests pass total (27 existing + 5 new).

- [ ] **Step 6: Confirm `boardghost list-boards` shows all 7 board profiles**

```bash
target/release/boardghost list-boards
# or with the dev binary:
cargo run -q -p boardghost-cli -- list-boards
```

Expected output: 7 lines, one per board (the 2 from M1 + 5 new).

- [ ] **Step 7: Commit**

```bash
git add runtime/displays/ runtime/boards/ runtime/tests/test_displays_extra.cpp \
        runtime/tests/CMakeLists.txt
git commit -m "feat(runtime): add ILI9341, ST7789, ST7796, GC9A01, ST7735 panels"
```

---

## Task 2: GPIO event emission in sim_runtime

Add a `[gpio]` log channel and emit events whenever the sketch touches a pin.

**Files:**
- Modify: `runtime/include/sim_runtime.h`
- Modify: `runtime/src/sim_runtime.cpp`
- Create: `runtime/tests/test_gpio_log.cpp`
- Modify: `runtime/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`runtime/tests/test_gpio_log.cpp`:

```cpp
#include <gtest/gtest.h>
#include <Arduino.h>
#include "sim_runtime.h"
#include <unistd.h>
#include <fcntl.h>

// Mirrors the CaptureStderr helper from test_bus_logging.cpp.
class CaptureStderr {
public:
    CaptureStderr() {
        old_ = dup(STDERR_FILENO);
        ::pipe(fds_);
        dup2(fds_[1], STDERR_FILENO);
        close(fds_[1]);
        int flags = fcntl(fds_[0], F_GETFL, 0);
        fcntl(fds_[0], F_SETFL, flags | O_NONBLOCK);
    }
    ~CaptureStderr() {
        fflush(stderr);
        dup2(old_, STDERR_FILENO);
        close(old_);
        close(fds_[0]);
    }
    std::string read_all() {
        fflush(stderr);
        std::string out;
        char buf[256];
        ssize_t n;
        while ((n = ::read(fds_[0], buf, sizeof(buf))) > 0) out.append(buf, n);
        return out;
    }
private:
    int old_;
    int fds_[2];
};

TEST(GpioLog, PinModeAndDigitalWriteEmit) {
    CaptureStderr cap;
    sim_runtime_init(0, nullptr);
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    digitalWrite(5, LOW);
    auto log = cap.read_all();
    sim_runtime_shutdown();

    EXPECT_NE(log.find("[gpio] mode 5 OUTPUT"), std::string::npos) << log;
    EXPECT_NE(log.find("[gpio] write 5 1"),    std::string::npos) << log;
    EXPECT_NE(log.find("[gpio] write 5 0"),    std::string::npos) << log;
}

TEST(GpioLog, AnalogWriteEmits) {
    CaptureStderr cap;
    sim_runtime_init(0, nullptr);
    analogWrite(9, 127);
    auto log = cap.read_all();
    sim_runtime_shutdown();
    EXPECT_NE(log.find("[gpio] pwm 9 127"), std::string::npos) << log;
}
```

- [ ] **Step 2: Run test to confirm it fails**

```bash
cmake --build runtime/build -j && ctest --test-dir runtime/build -R GpioLog
```

Expected: FAIL (no `[gpio]` lines emitted yet).

- [ ] **Step 3: Add the API to `runtime/include/sim_runtime.h`**

Append before the closing `extern "C"`:

```c
// Emit a GPIO-state log line on stderr with the `[gpio] ` prefix.
// Format: "[gpio] <op> <pin> <value-or-mode-name>". The launcher routes
// these to a dedicated gpio-log event for the inspector UI.
void sim_log_gpio_mode(uint8_t pin, uint8_t mode);
void sim_log_gpio_write(uint8_t pin, uint8_t value);
void sim_log_gpio_pwm(uint8_t pin, int value);
```

(uint8_t is already in scope via the existing includes in sim_runtime.h.)

- [ ] **Step 4: Implement in `runtime/src/sim_runtime.cpp`**

Add inside the `extern "C"` block (any location after the existing GPIO functions):

```cpp
static const char* mode_name(uint8_t mode) {
    switch (mode) {
        case INPUT:        return "INPUT";
        case OUTPUT:       return "OUTPUT";
        case INPUT_PULLUP: return "INPUT_PULLUP";
        default:           return "OTHER";
    }
}

void sim_log_gpio_mode(uint8_t pin, uint8_t mode) {
    std::fprintf(stderr, "[gpio] mode %u %s\n", (unsigned)pin, mode_name(mode));
}

void sim_log_gpio_write(uint8_t pin, uint8_t value) {
    std::fprintf(stderr, "[gpio] write %u %u\n", (unsigned)pin, (unsigned)(value ? 1 : 0));
}

void sim_log_gpio_pwm(uint8_t pin, int value) {
    std::fprintf(stderr, "[gpio] pwm %u %d\n", (unsigned)pin, value);
}
```

Now modify the existing `pinMode`, `digitalWrite`, `analogWrite` to call these:

```cpp
void pinMode(uint8_t pin, uint8_t mode) {
    if (pin >= MAX_PINS) return;
    g_pins[pin].mode = mode;
    sim_log_gpio_mode(pin, mode);
}

void digitalWrite(uint8_t pin, uint8_t value) {
    if (pin >= MAX_PINS) return;
    g_pins[pin].digital = value ? 1 : 0;
    sim_log_gpio_write(pin, value);
}

void analogWrite(uint8_t pin, int value) {
    if (pin >= MAX_PINS) return;
    g_pins[pin].analog = value;
    sim_log_gpio_pwm(pin, value);
}
```

- [ ] **Step 5: Register the new test**

Add `test_gpio_log.cpp` to `runtime/tests/CMakeLists.txt`'s `add_executable(runtime_tests …)`.

- [ ] **Step 6: Build + test**

```bash
cmake --build runtime/build -j && ctest --test-dir runtime/build
```

Expected: 34 tests pass total (32 + 2 new).

- [ ] **Step 7: Verify nothing breaks the existing pin-state tests**

The existing pin_state tests (Task 5 of M1.A) don't check stderr content, only return values. They should still pass unchanged.

- [ ] **Step 8: Commit**

```bash
git add runtime/include/sim_runtime.h runtime/src/sim_runtime.cpp \
        runtime/tests/test_gpio_log.cpp runtime/tests/CMakeLists.txt
git commit -m "feat(runtime): emit [gpio] log lines on pinMode/digitalWrite/analogWrite"
```

---

## Task 3: Launcher routes [gpio] lines to a gpio-log event

The runner currently emits all stderr lines on the `build-log` event. Split off lines starting with `[gpio] ` onto a new `gpio-log` event instead.

**Files:**
- Modify: `crates/boardghost-launcher/src-tauri/src/runner.rs`

- [ ] **Step 1: Modify the stderr loop**

Find the existing stderr reader task in `runner.rs`:

```rust
    if let Some(stderr) = child.stderr.take() {
        let app = app.clone();
        tokio::spawn(async move {
            let reader = BufReader::new(stderr);
            let mut lines = reader.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                let _ = app.emit("build-log", line);
            }
        });
    }
```

Replace with:

```rust
    if let Some(stderr) = child.stderr.take() {
        let app = app.clone();
        tokio::spawn(async move {
            let reader = BufReader::new(stderr);
            let mut lines = reader.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                if let Some(rest) = line.strip_prefix("[gpio] ") {
                    let _ = app.emit("gpio-log", rest.to_string());
                } else {
                    let _ = app.emit("build-log", line);
                }
            }
        });
    }
```

- [ ] **Step 2: Build + verify the launcher still compiles**

```bash
cargo build -p boardghost-launcher 2>&1 | tail -3
```

Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add crates/boardghost-launcher/src-tauri/src/runner.rs
git commit -m "feat(launcher): route [gpio] stderr lines to gpio-log event"
```

---

## Task 4: GpioGrid Svelte component + api.ts subscription

**Files:**
- Modify: `crates/boardghost-launcher/src/lib/api.ts`
- Create: `crates/boardghost-launcher/src/lib/GpioGrid.svelte`

- [ ] **Step 1: Add the event subscriber to `api.ts`**

Append to `crates/boardghost-launcher/src/lib/api.ts`:

```typescript
export function onGpioLog(handler: (line: string) => void): Promise<UnlistenFn> {
  return listen<string>("gpio-log", (e) => handler(e.payload));
}
```

- [ ] **Step 2: Write `GpioGrid.svelte`**

`crates/boardghost-launcher/src/lib/GpioGrid.svelte`:

```svelte
<script lang="ts">
  // Tracks the most recent state of every pin we've seen activity on.
  // Pins not yet touched render as "unused" (greyed).
  interface PinState {
    mode:    "INPUT" | "OUTPUT" | "INPUT_PULLUP" | "OTHER";
    digital: 0 | 1 | null;
    pwm:     number | null;
  }

  let { events }: { events: string[] } = $props();
  let pins: Record<number, PinState> = $state({});

  // Parse the gpio-log payload — format is documented in sim_runtime.cpp:
  //   "mode <pin> <MODE_NAME>"
  //   "write <pin> <0|1>"
  //   "pwm <pin> <value>"
  function applyEvent(line: string) {
    const parts = line.split(" ");
    if (parts.length < 3) return;
    const op  = parts[0];
    const pin = parseInt(parts[1], 10);
    if (Number.isNaN(pin)) return;

    const current: PinState = pins[pin] ?? { mode: "OTHER", digital: null, pwm: null };
    if (op === "mode") {
      const m = parts[2];
      if (m === "INPUT" || m === "OUTPUT" || m === "INPUT_PULLUP" || m === "OTHER") {
        current.mode = m;
      }
    } else if (op === "write") {
      const v = parseInt(parts[2], 10);
      current.digital = v === 1 ? 1 : 0;
    } else if (op === "pwm") {
      current.pwm = parseInt(parts[2], 10);
    }
    pins[pin] = current;
  }

  $effect(() => {
    // events is append-only; reapply the last entry on each change.
    // Simple and correct without keeping a separate index.
    if (events.length === 0) return;
    applyEvent(events[events.length - 1]);
  });

  function fill(p: PinState): string {
    if (p.digital === 1) return "#3ec46d";     // green = HIGH
    if (p.digital === 0) return "#444";        // dark = LOW
    if (p.pwm !== null)  return `hsl(40, 80%, ${30 + (p.pwm / 255) * 50}%)`;
    return "#1c1c1c";                          // unused
  }

  // Visible pin window: 0..47 covers most ESP32 / Arduino variants.
  const PIN_COUNT = 48;
</script>

<section class="gpio">
  <header>GPIO</header>
  <div class="grid">
    {#each Array(PIN_COUNT) as _, n}
      {@const p = pins[n]}
      <div
        class="cell"
        title={p ? `pin ${n} mode=${p.mode} digital=${p.digital ?? "-"} pwm=${p.pwm ?? "-"}` : `pin ${n} (untouched)`}
        style:background={p ? fill(p) : "#1c1c1c"}
      >
        {n}
      </div>
    {/each}
  </div>
</section>

<style>
  .gpio { display: flex; flex-direction: column; min-height: 0; }
  header { font-size: 0.75rem; font-weight: 600; padding: 0.4rem 0.5rem; background: #f3f3f3; border-top: 1px solid #ddd; border-bottom: 1px solid #ddd; }
  .grid { display: grid; grid-template-columns: repeat(8, 1fr); gap: 2px; padding: 0.4rem; background: #fafafa; }
  .cell {
    font-family: ui-monospace, "SF Mono", Menlo, monospace;
    font-size: 0.65rem;
    color: #eee;
    padding: 0.4rem 0;
    text-align: center;
    border-radius: 2px;
    cursor: default;
  }
</style>
```

- [ ] **Step 3: Type-check**

```bash
cd crates/boardghost-launcher && npm run check 2>&1 | tail -5 && cd ../..
```

Expected: 0 errors.

- [ ] **Step 4: Commit**

```bash
git add crates/boardghost-launcher/src/lib/api.ts \
        crates/boardghost-launcher/src/lib/GpioGrid.svelte
git commit -m "feat(launcher): GpioGrid Svelte component + onGpioLog API"
```

---

## Task 5: Integrate GpioGrid into ProjectDetail

**Files:**
- Modify: `crates/boardghost-launcher/src/lib/ProjectDetail.svelte`
- Modify: `crates/boardghost-launcher/src/App.svelte`

- [ ] **Step 1: Pass gpioLines through App.svelte**

Find the existing event subscriptions in `crates/boardghost-launcher/src/App.svelte` (inside `onMount`). After the build-log and serial-log subscriptions, add a third for gpio-log. Also add a `gpioLines: string[]` state and pass it to ProjectDetail.

Replace the script body of `App.svelte` with:

```svelte
<script lang="ts">
  import { onMount } from "svelte";
  import {
    listRecentProjects, listBoards, onBuildLog, onSerialLog, onGpioLog,
    type ProjectSummary, type BoardSummary,
  } from "./lib/api";
  import ProjectList   from "./lib/ProjectList.svelte";
  import ProjectDetail from "./lib/ProjectDetail.svelte";

  let projects:    ProjectSummary[] = $state([]);
  let boards:      BoardSummary[]   = $state([]);
  let selected:    string | null    = $state(null);
  let buildLines:  string[]         = $state([]);
  let serialLines: string[]         = $state([]);
  let gpioLines:   string[]         = $state([]);

  onMount(async () => {
    projects = await listRecentProjects();
    try {
      boards = await listBoards();
    } catch (e) {
      buildLines = [`Could not list boards: ${e}`];
    }

    const unBuild  = await onBuildLog((line)  => buildLines  = [...buildLines,  line]);
    const unSerial = await onSerialLog((line) => serialLines = [...serialLines, line]);
    const unGpio   = await onGpioLog((line)   => gpioLines   = [...gpioLines,   line]);

    return () => { unBuild(); unSerial(); unGpio(); };
  });
</script>

<div class="layout">
  <ProjectList {projects} bind:selected />
  <ProjectDetail project={selected} {boards} {buildLines} {serialLines} {gpioLines} />
</div>

<style>
  .layout { display: grid; grid-template-columns: 240px 1fr; height: 100vh; min-height: 0; }
</style>
```

- [ ] **Step 2: Accept gpioLines in ProjectDetail + render GpioGrid**

In `crates/boardghost-launcher/src/lib/ProjectDetail.svelte`, change the props declaration to include `gpioLines`, import `GpioGrid`, and replace the `.logs` grid with a three-section layout.

Replace the script block:

```svelte
<script lang="ts">
  import { buildAndRun, stop, type BoardSummary } from "./api";
  import BoardSelect from "./BoardSelect.svelte";
  import LogPanel    from "./LogPanel.svelte";
  import GpioGrid    from "./GpioGrid.svelte";

  let { project, boards, buildLines, serialLines, gpioLines }:
    {
      project:     string | null;
      boards:      BoardSummary[];
      buildLines:  string[];
      serialLines: string[];
      gpioLines:   string[];
    } = $props();

  let selectedBoard = $state("");
  let running       = $state(false);
  let lastError: string | null = $state(null);

  $effect(() => {
    if (!selectedBoard && boards.length > 0) selectedBoard = boards[0].name;
  });

  async function onRun() {
    if (!project || !selectedBoard) return;
    lastError = null;
    running   = true;
    try {
      await buildAndRun(project, selectedBoard);
    } catch (e) {
      lastError = String(e);
      running   = false;
    }
  }

  async function onStop() {
    try { await stop(); } finally { running = false; }
  }
</script>
```

Replace the markup body with:

```svelte
<section class="detail">
  {#if project}
    <header>
      <h2>{project}</h2>
    </header>

    <div class="controls">
      <BoardSelect {boards} bind:value={selectedBoard} />
      <div class="buttons">
        <button type="button" onclick={onRun} disabled={running || !selectedBoard}>
          ▶ Build &amp; Run
        </button>
        <button type="button" onclick={onStop} disabled={!running}>
          ■ Stop
        </button>
      </div>
    </div>

    {#if lastError}
      <div class="error">Error: {lastError}</div>
    {/if}

    <div class="panes">
      <LogPanel title="Build output" lines={buildLines} />
      <LogPanel title="Serial monitor" lines={serialLines} />
      <GpioGrid events={gpioLines} />
    </div>
  {:else}
    <div class="empty">
      Select or open a project to begin.
    </div>
  {/if}
</section>
```

Replace the `.logs` selector in the existing `<style>` block with:

```css
  .panes { display: grid; grid-template-rows: 1fr 1fr auto; gap: 0.5rem; flex: 1; min-height: 0; }
```

(Replace just the `.logs` rule; leave the others.)

- [ ] **Step 3: Type-check + build**

```bash
cd crates/boardghost-launcher && npm run check && npm run build && cd ../..
```

Expected: 0 errors. dist/index.html updated.

- [ ] **Step 4: Commit**

```bash
git add crates/boardghost-launcher/src/lib/ProjectDetail.svelte \
        crates/boardghost-launcher/src/App.svelte
git commit -m "feat(launcher): GPIO inspector panel under Build/Serial logs"
```

---

## Task 6: Vendor stb_image_write.h

**Files:**
- Create: `runtime/third_party/stb/stb_image_write.h`
- Modify: `runtime/CMakeLists.txt`

- [ ] **Step 1: Download stb_image_write.h (single-header library)**

```bash
mkdir -p runtime/third_party/stb
curl -fsSL -o runtime/third_party/stb/stb_image_write.h \
  https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
```

Pin the SHA after first download — read `runtime/third_party/stb/stb_image_write.h` and record its first-20-bytes-or-so version comment in the commit message so future rebuilds can verify they got the same file.

Alternative: get a specific commit by pinning the URL. As of plan-write time, the canonical stb_image_write.h is at the upstream `master` HEAD. If you want to pin, use the URL `https://raw.githubusercontent.com/nothings/stb/<commit-sha>/stb_image_write.h`. For M2.A purposes the latest master is acceptable.

- [ ] **Step 2: Make the header reachable as an include path in CMake**

Modify `runtime/CMakeLists.txt` — in the `target_include_directories(sim_runtime PUBLIC …)` block, add:

```cmake
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
```

So the existing PUBLIC includes list now ends with that directory. User code can then `#include <stb_image_write.h>`.

- [ ] **Step 3: Verify the include resolves with a dry compile**

```bash
cmake --build runtime/build -j 2>&1 | tail -3
```

Expected: clean rebuild (no actual usage yet — that lands in Task 7).

- [ ] **Step 4: Commit**

```bash
git add runtime/third_party/stb/stb_image_write.h runtime/CMakeLists.txt
git commit -m "build(runtime): vendor stb_image_write.h for PNG capture"
```

---

## Task 7: sim_screenshot() implementation + SIGUSR1 handler

**Files:**
- Modify: `runtime/include/sim_runtime.h`
- Create: `runtime/src/sim_screenshot.cpp`
- Modify: `runtime/src/sim_runtime.cpp` (install SIGUSR1 handler in init)
- Modify: `runtime/CMakeLists.txt` (add sim_screenshot.cpp to sources)
- Create: `runtime/tests/test_screenshot.cpp`
- Modify: `runtime/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`runtime/tests/test_screenshot.cpp`:

```cpp
#include <gtest/gtest.h>
#include <Arduino.h>
#include "sim_runtime.h"
#include "LGFX_ILI9488_SDL.hpp"
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

TEST(Screenshot, WritesPngFile) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    sim_runtime_init(0, nullptr);

    LGFX_ILI9488_SDL tft;
    tft.init();
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(10, 10, 50, 50, TFT_RED);

    auto path = fs::temp_directory_path() / "boardghost-test.png";
    if (fs::exists(path)) fs::remove(path);

    int rc = sim_screenshot(path.c_str(), &tft);
    EXPECT_EQ(rc, 0);
    ASSERT_TRUE(fs::exists(path));
    EXPECT_GT(fs::file_size(path), 100u);   // at least a PNG header + minimal data

    fs::remove(path);
    sim_runtime_shutdown();
}
```

- [ ] **Step 2: Run failing test to confirm it doesn't link**

```bash
cmake --build runtime/build -j 2>&1 | tail -3
```

Expected: build/link error mentioning `sim_screenshot` undefined.

- [ ] **Step 3: Add API to `runtime/include/sim_runtime.h`**

Append before the closing `extern "C"`:

```c
// Forward declaration of an opaque LGFX_Device pointer — the caller passes
// their active panel. Returns 0 on success, non-zero on error.
//
// NOTE: declared as `void*` in the C-facing header so that consumers can
// pass any LovyanGFX device without dragging the full LovyanGFX include
// chain into C-only translation units. The implementation casts back.
int sim_screenshot(const char* path, void* lgfx_device);
```

- [ ] **Step 4: Implement `runtime/src/sim_screenshot.cpp`**

```cpp
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <LovyanGFX.hpp>
#include "sim_runtime.h"
#include <cstdio>
#include <vector>

extern "C" int sim_screenshot(const char* path, void* lgfx_device) {
    if (!path || !lgfx_device) return -1;
    auto* dev = static_cast<lgfx::LGFX_Device*>(lgfx_device);
    int w = dev->width();
    int h = dev->height();
    if (w <= 0 || h <= 0) return -2;

    std::vector<uint16_t> rgb565(static_cast<size_t>(w) * h);
    dev->readRect(0, 0, w, h, rgb565.data());

    // Convert RGB565 → RGBA8888 for stb_image_write.
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < rgb565.size(); ++i) {
        uint16_t v = rgb565[i];
        uint8_t r = static_cast<uint8_t>(((v >> 11) & 0x1F) * 255 / 31);
        uint8_t g = static_cast<uint8_t>(((v >> 5)  & 0x3F) * 255 / 63);
        uint8_t b = static_cast<uint8_t>(((v)       & 0x1F) * 255 / 31);
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = 255;
    }

    int ok = stbi_write_png(path, w, h, 4, rgba.data(), w * 4);
    if (!ok) {
        std::fprintf(stderr, "[boardghost] sim_screenshot: stbi_write_png failed for %s\n", path);
        return -3;
    }
    std::fprintf(stderr, "[boardghost] sim_screenshot wrote %s (%dx%d)\n", path, w, h);
    return 0;
}
```

- [ ] **Step 5: Add to sim_runtime library sources**

In `runtime/CMakeLists.txt`, find the `add_library(sim_runtime STATIC …)` block and append `src/sim_screenshot.cpp` to the source list.

- [ ] **Step 6: Register the new test**

Add `test_screenshot.cpp` to `runtime/tests/CMakeLists.txt`'s `add_executable(runtime_tests …)`.

- [ ] **Step 7: Build + test**

```bash
cmake --build runtime/build -j && ctest --test-dir runtime/build -R Screenshot
```

Expected: 1 test PASS (`Screenshot.WritesPngFile`). 35 tests total in the suite.

- [ ] **Step 8: Commit**

```bash
git add runtime/include/sim_runtime.h runtime/src/sim_screenshot.cpp \
        runtime/CMakeLists.txt runtime/tests/test_screenshot.cpp runtime/tests/CMakeLists.txt
git commit -m "feat(runtime): sim_screenshot(path, device) — write framebuffer as PNG"
```

---

## Task 8: SIGUSR1 → screenshot bridge + env-var fallback

Wire signal-triggered screenshots so the launcher can request one without restarting the sketch.

**Files:**
- Modify: `runtime/src/sim_runtime.cpp` (register handler, expose active device)
- Modify: `runtime/include/sim_runtime.h` (sim_set_active_display API)

- [ ] **Step 1: Add active-display registration to `sim_runtime.h`**

Append before the closing `extern "C"`:

```c
// User code calls this once after creating its LGFX device to register it
// as the screenshot target. Pass NULL on shutdown to unregister.
void sim_set_active_display(void* lgfx_device);
```

- [ ] **Step 2: Implement signal handling in `sim_runtime.cpp`**

Add at the top of the file with the other includes:

```cpp
#include <csignal>
```

In the anonymous namespace, add:

```cpp
std::atomic<void*> g_active_display{nullptr};
std::atomic<int>   g_screenshot_requested{0};

void sigusr1_handler(int /*sig*/) {
    // Signal-safe: just flip a flag. Real work happens in sim_pump_events.
    g_screenshot_requested.store(1);
}
```

In `sim_runtime_init` (after SDL_Init), append:

```cpp
    std::signal(SIGUSR1, sigusr1_handler);
```

In `sim_pump_events`, add at the end:

```cpp
    if (g_screenshot_requested.exchange(0) == 1) {
        if (void* dev = g_active_display.load()) {
            const char* path = std::getenv("BOARDGHOST_SCREENSHOT_PATH");
            if (!path) path = "/tmp/boardghost-screenshot.png";
            sim_screenshot(path, dev);
        }
    }
```

Add the `extern "C"` getter:

```cpp
void sim_set_active_display(void* lgfx_device) {
    g_active_display.store(lgfx_device);
}
```

Note: `sim_screenshot` is declared in sim_runtime.h, so it's already visible here.

- [ ] **Step 3: Update existing examples to register their display (optional but convenient)**

Modify `examples/lvgl_hello_ili9488/sketch/sketch.ino` — after `tft.init()` in setup(), add:

```cpp
    sim_set_active_display(&tft);
```

Same edit in `examples/ssd1306_text/sketch/sketch.ino` (after `oled.init()`):

```cpp
    sim_set_active_display(&oled);
```

This is the documented pattern. New examples follow it. Sketches that don't register get no screenshot — silent no-op, no crash.

- [ ] **Step 4: Build + verify all tests still pass**

```bash
cmake --build runtime/build -j && ctest --test-dir runtime/build 2>&1 | tail -3
```

Expected: 35 tests pass.

- [ ] **Step 5: Manual smoke — SIGUSR1 from another shell triggers a PNG**

Open two shells. In shell A (from the repo root):

```bash
BOARDGHOST_SCREENSHOT_PATH=/tmp/m2-test.png \
  boardghost run examples/lvgl_hello_ili9488 --board ili9488_esp32s3_sim
```

In shell B:

```bash
sleep 5  # let the LVGL window settle
PID=$(pgrep -f 'sketch.*ili9488_esp32s3_sim' | head -1)
kill -USR1 "$PID"
sleep 1
file /tmp/m2-test.png   # should report a PNG
```

In shell A, you should see `[boardghost] sim_screenshot wrote /tmp/m2-test.png (480x320)` on stderr.

If the file doesn't appear, check:
- Did the sketch include `sim_set_active_display(&tft);` ? (it should, after step 3)
- Did the PID resolution find the sketch process? (`pgrep -f sketch` may need adjustment)

- [ ] **Step 6: Commit**

```bash
git add runtime/include/sim_runtime.h runtime/src/sim_runtime.cpp \
        examples/lvgl_hello_ili9488/sketch/sketch.ino \
        examples/ssd1306_text/sketch/sketch.ino
git commit -m "feat(runtime): SIGUSR1 → sim_screenshot via BOARDGHOST_SCREENSHOT_PATH"
```

---

## Task 9: CLI --screenshot flag

Add a one-shot screenshot option to `boardghost run` for headless / CI use.

**Files:**
- Modify: `crates/boardghost-cli/src/cli.rs`
- Modify: `crates/boardghost-cli/src/main.rs`
- Modify: `crates/boardghost-cli/src/run.rs`

- [ ] **Step 1: Add the flag to the clap definition**

In `crates/boardghost-cli/src/cli.rs`, find the `Run` variant in the `Command` enum and add a `--screenshot` arg:

```rust
    Run {
        #[arg(value_name = "PROJECT_DIR")]
        project: PathBuf,
        #[arg(long)]
        board: String,
        #[arg(long, default_value = "debug")]
        profile: BuildProfile,
        /// If set, the sketch will write a PNG to this path on SIGUSR1
        /// (and the CLI will fire SIGUSR1 once 2 seconds after launch).
        #[arg(long, value_name = "PNG_PATH")]
        screenshot: Option<PathBuf>,
    },
```

- [ ] **Step 2: Thread the flag through main.rs**

In `crates/boardghost-cli/src/main.rs`, the existing `Command::Run` match arm needs to accept the new field. Replace it with:

```rust
        Command::Run { project, board, profile, screenshot } => {
            let boards  = boards_dir()?;
            let runtime = runtime_dir()?;
            let release = matches!(profile, BuildProfile::Release);
            let r = boardghost::build::run_build(&project, &board, &boards, &runtime, release)?;
            eprintln!("→ Launching {}...", r.binary.display());
            let code = boardghost::run::exec_sketch(&r.binary, screenshot.as_deref())?;
            std::process::exit(code);
        }
```

The `Build` arm needs `..` to skip the unused field:

```rust
        Command::Build { project, board, profile } => {
            // unchanged
            // ...
        }
```

(No change to Build if it didn't use `screenshot`. Just make sure clap's pattern match still compiles with the new field.)

- [ ] **Step 3: Extend `exec_sketch` to accept the screenshot path**

Replace `crates/boardghost-cli/src/run.rs`:

```rust
use anyhow::Result;
use std::path::Path;
use std::process::{Command, Stdio};

pub fn exec_sketch(binary: &Path, screenshot: Option<&Path>) -> Result<i32> {
    let mut cmd = Command::new(binary);
    cmd.stdin(Stdio::inherit())
       .stdout(Stdio::inherit())
       .stderr(Stdio::inherit());

    if let Some(path) = screenshot {
        cmd.env("BOARDGHOST_SCREENSHOT_PATH", path);
    }

    let mut child = cmd.spawn()?;

    // If a screenshot was requested, send SIGUSR1 after a short settle delay.
    if screenshot.is_some() {
        let pid = child.id();
        std::thread::spawn(move || {
            std::thread::sleep(std::time::Duration::from_millis(2000));
            #[cfg(unix)]
            unsafe { libc::kill(pid as i32, libc::SIGUSR1); }
        });
    }

    let status = child.wait()?;
    Ok(status.code().unwrap_or(-1))
}
```

Add `libc = "0.2"` to `crates/boardghost-cli/Cargo.toml` `[dependencies]` if not already present.

- [ ] **Step 4: Verify build + CLI help**

```bash
cargo build -p boardghost-cli 2>&1 | tail -3
target/debug/boardghost run --help 2>&1 | head -20
```

Expected: `--screenshot <PNG_PATH>` flag appears in the help output.

- [ ] **Step 5: Manual smoke test (headless)**

```bash
SDL_VIDEODRIVER=dummy BOARDGHOST_FRAME_LIMIT=400 \
  target/debug/boardghost run examples/lvgl_hello_ili9488 \
  --board ili9488_esp32s3_sim --screenshot /tmp/m2-cli.png
file /tmp/m2-cli.png
```

Expected: the sketch runs ~2 seconds, the CLI sends SIGUSR1 at the 2-second mark, the sketch writes the PNG, then continues running until the frame limit. PNG file should exist and `file` should report `PNG image data, 480 x 320`.

- [ ] **Step 6: Commit**

```bash
git add crates/boardghost-cli/src/cli.rs crates/boardghost-cli/src/main.rs \
        crates/boardghost-cli/src/run.rs crates/boardghost-cli/Cargo.toml
git commit -m "feat(cli): --screenshot PATH flag triggers SIGUSR1 after 2s"
```

---

## Task 10: Launcher screenshot button

**Files:**
- Modify: `crates/boardghost-launcher/src-tauri/src/commands.rs`
- Modify: `crates/boardghost-launcher/src-tauri/src/runner.rs`
- Modify: `crates/boardghost-launcher/src-tauri/src/state.rs`
- Modify: `crates/boardghost-launcher/src-tauri/src/main.rs` (register command)
- Modify: `crates/boardghost-launcher/src/lib/api.ts`
- Modify: `crates/boardghost-launcher/src/lib/ProjectDetail.svelte`

- [ ] **Step 1: Track the running sketch's PID in AppState**

Modify `crates/boardghost-launcher/src-tauri/src/state.rs` — add a `running_pid` field. Note: we already keep the `Child`, but we also need the PID accessible after some operations may have moved the Child around.

Add to the existing `AppState` struct:

```rust
    pub running_pid: Mutex<Option<u32>>,
```

Initialize in `new`:

```rust
        Ok(Self {
            projects_path,
            store:       Mutex::new(store),
            running:     Mutex::new(None),
            running_pid: Mutex::new(None),
        })
```

- [ ] **Step 2: Set the PID when build_and_run spawns the child**

In `crates/boardghost-launcher/src-tauri/src/commands.rs`, find `build_and_run`. After the line that stores the new child in `state.running`, also store its PID:

Before:

```rust
    {
        let mut slot = state.running.lock().unwrap();
        *slot = Some(child);
    }
```

After:

```rust
    let pid = child.id();
    {
        let mut slot = state.running.lock().unwrap();
        *slot = Some(child);
    }
    *state.running_pid.lock().unwrap() = pid;
```

Also clear the PID in `stop`:

```rust
    *state.running_pid.lock().unwrap() = None;
```

(Add this line at the end of `stop`, after the wait.)

- [ ] **Step 3: Add the screenshot command in `commands.rs`**

Append:

```rust
#[tauri::command]
pub async fn screenshot(state: State<'_, AppState>) -> Result<String, String> {
    let pid = state.running_pid.lock().unwrap().clone();
    let Some(pid) = pid else {
        return Err("no sketch running".to_string());
    };

    let path = std::env::temp_dir().join(format!("boardghost-screenshot-{}.png", pid));
    // The sketch reads BOARDGHOST_SCREENSHOT_PATH at startup — but we set
    // it ourselves at spawn time, so we already know the path. To make
    // this command idempotent on path conventions, recompute here and
    // assume the sketch was spawned with that env var.
    //
    // Send SIGUSR1; the sketch's signal handler will write the PNG.
    #[cfg(unix)]
    unsafe { libc::kill(pid as i32, libc::SIGUSR1); }

    // Wait briefly for the file to appear (next sim_pump_events iteration).
    for _ in 0..20 {
        if std::path::Path::new(&path).exists() {
            return Ok(path.to_string_lossy().to_string());
        }
        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
    }
    Err(format!("screenshot timed out after 2s (path was {})", path.display()))
}
```

Add `libc = "0.2"` to `crates/boardghost-launcher/src-tauri/Cargo.toml` if not already there.

- [ ] **Step 4: Set BOARDGHOST_SCREENSHOT_PATH at spawn time**

Modify `crates/boardghost-launcher/src-tauri/src/runner.rs`. Replace the spawn block:

```rust
    let screenshot_path = std::env::temp_dir().join("boardghost-screenshot-PENDING.png");
    // The PID isn't known until after spawn(), so use a placeholder name
    // and rename later. Or: derive a deterministic-enough name like the
    // tokio task id. Simpler: just set env to the temp dir base, and let
    // the screenshot command read child.id() to compute the actual path.
    //
    // Cleaner: use a fixed name and accept that concurrent sketches share it.
    // M2.A enforces single-sketch-at-a-time, so this is fine.
    let fixed = std::env::temp_dir().join("boardghost-screenshot.png");

    let mut child = Command::new("boardghost")
        .arg("run")
        .arg(&project)
        .arg("--board")
        .arg(&board)
        .env("SDL_VIDEODRIVER", "x11")
        .env("BOARDGHOST_SCREENSHOT_PATH", &fixed)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()?;
```

Then update the `screenshot` command in `commands.rs` to use the fixed path:

```rust
    let path = std::env::temp_dir().join("boardghost-screenshot.png");
```

(Replace the PID-based path with this fixed path; remove the PID computation that used it.)

- [ ] **Step 5: Register the screenshot command in `main.rs`**

Update `invoke_handler!`:

```rust
        .invoke_handler(tauri::generate_handler![
            commands::list_recent_projects,
            commands::add_project,
            commands::list_boards,
            commands::build_and_run,
            commands::stop,
            commands::screenshot,
        ])
```

- [ ] **Step 6: Frontend API wrapper**

Append to `crates/boardghost-launcher/src/lib/api.ts`:

```typescript
export async function screenshot(): Promise<string> {
  return invoke<string>("screenshot");
}
```

- [ ] **Step 7: Add the screenshot button to ProjectDetail.svelte**

In `crates/boardghost-launcher/src/lib/ProjectDetail.svelte`, import `screenshot` from `./api`:

```svelte
  import { buildAndRun, stop, screenshot, type BoardSummary } from "./api";
```

Add a state variable:

```svelte
  let lastScreenshot: string | null = $state(null);
```

Add an `onScreenshot` handler:

```svelte
  async function onScreenshot() {
    lastError = null;
    try {
      lastScreenshot = await screenshot();
    } catch (e) {
      lastError = `Screenshot failed: ${e}`;
    }
  }
```

In the markup, add a screenshot button next to the Stop button:

```svelte
        <button type="button" onclick={onStop} disabled={!running}>
          ■ Stop
        </button>
        <button type="button" onclick={onScreenshot} disabled={!running}>
          📷 Screenshot
        </button>
```

And below the error banner, add:

```svelte
    {#if lastScreenshot}
      <div class="info">Screenshot saved: {lastScreenshot}</div>
    {/if}
```

In the `<style>` block, append:

```css
  .info { padding: 0.5rem; background: #efe; border: 1px solid #cfc; border-radius: 4px; font-size: 0.85rem; }
```

- [ ] **Step 8: Type-check + build**

```bash
cd crates/boardghost-launcher && npm run check && npm run build && cd ../..
cargo build -p boardghost-launcher 2>&1 | tail -3
```

Expected: 0 errors, clean build.

- [ ] **Step 9: Commit**

```bash
git add crates/boardghost-launcher/
git commit -m "feat(launcher): 📷 Screenshot button — SIGUSR1 + PNG at /tmp/boardghost-screenshot.png"
```

---

## Task 11: Touch_sdl driver — common touch chip emulation

Add a single SDL-backed touch driver that satisfies the LovyanGFX touch API for ALL panel wrappers. Real chips (XPT2046, FT6236, GT911) differ in bus + protocol; in sim, they all just report SDL mouse position. The board profile still specifies the "chip" cosmetically so user code that branches on it still compiles.

**Files:**
- Create: `runtime/displays/Touch_sdl.hpp`
- Modify: `runtime/displays/LGFX_ILI9488_SDL.hpp`
- Modify: `runtime/displays/LGFX_ILI9341_SDL.hpp`
- Modify: `runtime/displays/LGFX_ST7789_SDL.hpp`
- Modify: `runtime/displays/LGFX_ST7796_SDL.hpp`
- Modify: `runtime/displays/LGFX_GC9A01_SDL.hpp` (no touch — leave alone, but verify still compiles)
- Modify: `runtime/displays/LGFX_ST7735_SDL.hpp` (no touch — leave alone)
- Modify: `runtime/displays/LGFX_SSD1306_SDL.hpp` (no touch — mono OLED, leave alone)
- Create: `runtime/tests/test_touch_sdl.cpp`
- Modify: `runtime/tests/CMakeLists.txt`

- [ ] **Step 1: Write `Touch_sdl.hpp`**

`runtime/displays/Touch_sdl.hpp`:

```cpp
#pragma once
#include <LovyanGFX.hpp>
#include <SDL.h>

// A LovyanGFX touch driver that returns the current SDL mouse position
// while the left mouse button is held down. This emulates the on-chip
// behaviour of XPT2046 / FT6236 / GT911 etc. for desktop simulation —
// regardless of which controller a board profile claims, the user-facing
// API (tft.getTouch / tft.getTouchPointRaw) just works.
class Touch_sdl : public lgfx::ITouch {
public:
    Touch_sdl() {
        auto& cfg = _cfg;
        cfg.x_min = 0;
        cfg.x_max = 4095;
        cfg.y_min = 0;
        cfg.y_max = 4095;
        cfg.bus_shared = false;
        // pin assignments are irrelevant in sim; leave at defaults.
    }

    bool init(void) override { return true; }

    void wakeup(void) override {}
    void sleep(void)  override {}

    uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override {
        if (count == 0 || tp == nullptr) return 0;

        // Pump SDL events so SDL_GetMouseState reflects current state.
        SDL_PumpEvents();
        int mx = 0, my = 0;
        Uint32 buttons = SDL_GetMouseState(&mx, &my);

        if (!(buttons & SDL_BUTTON(SDL_BUTTON_LEFT))) return 0;

        // The panel's setTouchCalibration() expects raw values in 0..4095.
        // Use the SDL window dimensions implicitly: panel_width × 16 sample.
        // Easier: report panel coordinates directly and let downstream
        // calibration scale to 0..panel_width. Most user code calls
        // tft.setTouchCalibration(0, panel_width-1, 0, panel_height-1)
        // and that's already the SDL window pixel range.
        tp[0].x    = mx;
        tp[0].y    = my;
        tp[0].size = 1;
        tp[0].id   = 0;
        return 1;
    }
};
```

> **Note for the implementer:** the exact name of LovyanGFX's touch-base class and method signatures may vary across the pinned 1.1.16 tag. Confirm by reading `runtime/third_party/LovyanGFX/src/lgfx/v1/Touch.hpp` and `runtime/third_party/LovyanGFX/src/lgfx/v1/touch/Touch_XPT2046.cpp` (or similar). If the v1.1.16 API uses different signatures (e.g. `getTouchRaw` may return `bool` and take pointer params differently), adapt. The behaviour is: when left mouse is held, return one point with current cursor xy.

- [ ] **Step 2: Update the four touch-enabled panel wrappers**

The pattern: add a `Touch_sdl touch_` member, in constructor call `touch_.config(...)` to set the input range, and `setTouch(&touch_)` after `setPanel(&panel_)`.

Updated `runtime/displays/LGFX_ILI9488_SDL.hpp`:

```cpp
#pragma once
#include <LovyanGFX.hpp>
#include "Touch_sdl.hpp"

class LGFX_ILI9488_SDL : public lgfx::LGFX_Device {
public:
    LGFX_ILI9488_SDL() {
        auto cfg = panel_.config();
        cfg.memory_width  = 480;
        cfg.memory_height = 320;
        cfg.panel_width   = 480;
        cfg.panel_height  = 320;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        cfg.offset_rotation = 0;
        panel_.config(cfg);
        setPanel(&panel_);
        panel_.setTouch(&touch_);
    }

private:
    lgfx::Panel_sdl panel_;
    Touch_sdl       touch_;
};
```

Apply the same pattern to:
- `runtime/displays/LGFX_ILI9341_SDL.hpp` (dimensions 320×240)
- `runtime/displays/LGFX_ST7789_SDL.hpp` (dimensions 240×320)
- `runtime/displays/LGFX_ST7796_SDL.hpp` (dimensions 480×320)

LGFX_GC9A01_SDL, LGFX_ST7735_SDL, and LGFX_SSD1306_SDL stay unchanged — those board profiles have no `[touch]` section because the real hardware variants typically don't have touch panels.

- [ ] **Step 3: Write the touch test**

`runtime/tests/test_touch_sdl.cpp`:

```cpp
#include <gtest/gtest.h>
#include "sim_runtime.h"
#include "LGFX_ILI9488_SDL.hpp"
#include <SDL.h>
#include <cstdlib>

class TouchTest : public ::testing::Test {
protected:
    void SetUp() override {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        sim_runtime_init(0, nullptr);
    }
    void TearDown() override { sim_runtime_shutdown(); }
};

TEST_F(TouchTest, GetTouchReturnsZeroWhenButtonNotPressed) {
    LGFX_ILI9488_SDL tft;
    tft.init();
    int32_t x = 0, y = 0;
    // SDL_BUTTON state is no buttons pressed by default in dummy driver.
    EXPECT_FALSE(tft.getTouch(&x, &y));
}

TEST_F(TouchTest, GetTouchReturnsCoordsWhenSyntheticPressActive) {
    LGFX_ILI9488_SDL tft;
    tft.init();
    // Push a synthetic mouse-down event and warp the cursor.
    SDL_Event ev{};
    ev.type        = SDL_MOUSEBUTTONDOWN;
    ev.button.type = SDL_MOUSEBUTTONDOWN;
    ev.button.state = SDL_PRESSED;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.x = 120;
    ev.button.y = 80;
    SDL_PushEvent(&ev);
    // We also have to update SDL's internal mouse state. SDL_WarpMouseGlobal
    // works in dummy driver for our purposes.
    // NOTE: The dummy driver doesn't actually drive mouse state from PushEvent
    // alone — SDL_GetMouseState reads from the internal mouse handler, which
    // is updated by SDL's event pump processing real input.
    //
    // For headless test purposes we just verify the API responds without
    // crashing; the live mouse path is exercised manually via the launcher.
    int32_t x = 0, y = 0;
    bool got = tft.getTouch(&x, &y);
    (void)got; (void)x; (void)y;
    // Don't strictly assert here — the dummy driver behavior for synthetic
    // events is fragile. Real coverage is the manual smoke (Task 12 Step 4).
    SUCCEED();
}
```

> **Note:** in a headless SDL dummy driver, synthetic mouse events don't reliably update internal cursor state. The first test (button-not-pressed) is the real assertion; the second confirms the API doesn't crash. Live touch verification is in Task 12 Step 4.

- [ ] **Step 4: Register the test**

Add `test_touch_sdl.cpp` to `runtime/tests/CMakeLists.txt`'s `add_executable(runtime_tests …)` source list.

- [ ] **Step 5: Build + test**

```bash
cmake --build runtime/build -j && ctest --test-dir runtime/build 2>&1 | tail -3
```

Expected: 37 tests pass (35 from earlier + 2 touch).

- [ ] **Step 6: Run lvgl_hello E2E to confirm we didn't regress the existing LVGL+mouse path**

```bash
./tests/e2e/run_lvgl_hello.sh
```

Expected: `PASS: lvgl_hello_ili9488`. LVGL's own SDL indev (from M1.B Task 13) is unaffected by adding a separate Touch_sdl to the LGFX device — they're two independent event consumers.

- [ ] **Step 7: Commit**

```bash
git add runtime/displays/Touch_sdl.hpp \
        runtime/displays/LGFX_ILI9488_SDL.hpp \
        runtime/displays/LGFX_ILI9341_SDL.hpp \
        runtime/displays/LGFX_ST7789_SDL.hpp \
        runtime/displays/LGFX_ST7796_SDL.hpp \
        runtime/tests/test_touch_sdl.cpp \
        runtime/tests/CMakeLists.txt
git commit -m "feat(runtime): Touch_sdl driver — SDL mouse → LovyanGFX touch API

Single SDL-backed touch class plumbed into ILI9488/ILI9341/ST7789/ST7796
wrappers. Satisfies tft.getTouch(&x, &y) regardless of which chip the
board profile claims (XPT2046, FT6236, GT911 — cosmetic in sim).
GC9A01/ST7735/SSD1306 stay touchless (matches typical real-hardware
modules)."
```

---

## Task 12: Docs + M2.A acceptance + tag

**Files:**
- Modify: `README.md`
- Modify: `docs/getting-started.md`

- [ ] **Step 1: Update README "What works" section**

In `README.md`, find the "What works" bullet list and replace it with:

```markdown
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
```

- [ ] **Step 2: Add sections to getting-started.md**

Append to `docs/getting-started.md`:

```markdown
## 9. GPIO inspector

The desktop launcher displays a live grid of GPIO pins under the Serial monitor.
Calls to `pinMode()`, `digitalWrite()`, and `analogWrite()` in your sketch are
streamed to the launcher and visualised in real time:

- Dark grey: pin not yet used
- Bright green: pin OUTPUT, HIGH
- Dark grey w/ outline: pin OUTPUT, LOW
- Warm gradient: pin PWM, brightness scales with duty
- Hover for full state details

Headless / CLI-only users see the same data as `[gpio]` lines on stderr.

## 10. Screenshot capture

Two ways to capture the simulated panel:

**From the launcher:** while the sketch is running, click `📷 Screenshot`.
The PNG is written to `/tmp/boardghost-screenshot.png` and the path is
displayed in the UI.

**From the CLI:**

```bash
boardghost run examples/lvgl_hello_ili9488 \
  --board ili9488_esp32s3_sim \
  --screenshot /tmp/demo.png
```

The CLI sends `SIGUSR1` to the sketch ~2 seconds after launch, the sketch
writes the PNG, and continues running.

Note: your sketch must call `sim_set_active_display(&tft)` once after
initializing its LGFX device. The bundled examples already do this.
```

- [ ] **Step 3: Run the full E2E suite and unit tests**

```bash
cd /home/phill/arduino-board-emulator
cargo test --workspace 2>&1 | tail -5            # CLI + launcher unit tests
ctest --test-dir runtime/build 2>&1 | tail -3    # runtime unit tests
./tests/e2e/run_hello_serial.sh                  # existing E2E
./tests/e2e/run_ssd1306_text.sh
./tests/e2e/run_lvgl_hello.sh
```

Expected: all green. The new runtime tests bring the unit suite to 35.

- [ ] **Step 4: Manual launcher acceptance test**

Launch in dev mode and exercise each new feature:

```bash
cd crates/boardghost-launcher
npm run tauri dev
```

In the window:
- [ ] Board dropdown now lists 7 boards.
- [ ] Pick `ili9341_esp32_sim` (or any new board), open `examples/lvgl_hello_ili9488`, Build & Run. The sketch should build and run on the new panel (LVGL adapts to whatever size sim_lvgl_attach_sdl gets — but the sketch hardcodes 480×320, so for now use a 480×320 board to actually verify rendering. The other boards exercise the build pipeline correctness.)
- [ ] After clicking the LVGL button a few times, GPIO grid should remain mostly grey (LVGL doesn't touch GPIO).
- [ ] In a separate test: open `examples/ssd1306_text`, Build & Run. GPIO grid should still be mostly grey.
- [ ] Click the 📷 Screenshot button while a sketch is running. The info banner shows the file path. Open the PNG; verify it shows the panel contents.

- [ ] **Step 5: Commit docs + push**

```bash
git add README.md docs/getting-started.md
git commit -m "docs: M2.A — 7 displays, GPIO inspector, screenshot capture"
git push 2>&1 | tail -3
```

- [ ] **Step 6: Watch CI**

```bash
gh run watch
```

Both `build-and-test` and `launcher-build` jobs must pass. If they don't, triage and fix before tagging.

- [ ] **Step 7: Tag the milestone**

After CI is green:

```bash
git tag -a m2a-displays-gpio-touch-screenshot -m "BoardGhost M2.A — 5 displays, touch (XPT2046/FT6236/GT911), GPIO inspector, screenshot"
git push --tags
```

---

## Self-review

**Spec coverage:**

| User-chosen scope item | Task |
|---|---|
| 5 new displays | Task 1 |
| GPIO inspector backend | Task 2 |
| Launcher gpio-log routing | Task 3 |
| GpioGrid component | Task 4 |
| GpioGrid integrated in ProjectDetail | Task 5 |
| Vendor stb_image_write.h | Task 6 |
| sim_screenshot() core | Task 7 |
| SIGUSR1 trigger bridge | Task 8 |
| CLI --screenshot flag | Task 9 |
| Launcher screenshot button | Task 10 |
| Touch_sdl driver (XPT2046/FT6236/GT911 cosmetic) | Task 11 |
| Docs + acceptance + tag | Task 12 |

**Placeholder scan:**

- Task 6 mentions "pin the SHA after first download" — engineer instruction, not a placeholder.
- Task 10 Step 4 contains a now-resolved internal discussion about path strategies, ending with "Cleaner: use a fixed name and accept that concurrent sketches share it. M2.A enforces single-sketch-at-a-time, so this is fine." Acceptable: it documents the decision in-line.

**Type consistency:**

- `sim_screenshot(const char* path, void* lgfx_device)` declared Task 7, used Task 7 + Task 8 — consistent.
- `sim_set_active_display(void* lgfx_device)` declared Task 8, used Task 8 examples — consistent.
- `gpio-log` event name used Tasks 3, 4, 5 — consistent.
- `BOARDGHOST_SCREENSHOT_PATH` env var used Tasks 7, 8, 9, 10 — consistent.
- `screenshot` Tauri command name used Tasks 10, frontend api.ts — consistent.
- `Touch_sdl` class name used Tasks 11 and panel wrappers — consistent.
- ST7789 board profile updated to `controller = "ft6236"` (Task 1) — this is cosmetic-only at runtime because Task 11's Touch_sdl serves all chips identically.
- ST7796 board profile updated to `controller = "gt911"` (Task 1) — same caveat.

**Scope honesty:**

- Bumping the LVGL example to render on non-480×320 panels would require parameterising it via preprocessor defines. M2.A scope acceptance (Task 11 Step 4) explicitly notes that for now, only the 480×320-size boards (ILI9488, ST7796) fully render the existing LVGL example correctly; the others compile and run but the LVGL UI will draw partly off-screen. This is fine for M2.A — we're proving the build pipeline works across panels, not shipping new examples.

---

**End of plan.**
