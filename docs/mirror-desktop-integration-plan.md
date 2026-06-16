# Implementation plan: desktop-side mirror + agentic control plane

**Repo:** `arduino-board-emulator` (this repo).
**Scope:** add a unified HTTP **observe + act** surface to the runtime that
serves (a) the agentic GUI loop and (b) the Android companion app
([android-companion-directive.md](./android-companion-directive.md)). These
share one frame-capture refactor and one server, so they are built together.

## Why one plan covers both goals
- The companion needs a **live framebuffer stream** and an **audio stream**.
- The agent loop needs **on-demand framebuffer** + **live touch injection**.
- Both hang off the existing devtools HTTP server (`sim_devtools.cpp`) and both
  reuse the same `readRect` capture (`sim_screenshot.cpp:17`) and the same SDL
  audio mix (`sim_audio.cpp:audio_callback`). Building them separately would
  duplicate the capture, server-lifecycle, and security work.

---

## Current constraints to work around (verified)
1. **Devtools server is lazy-started on the first UART1 byte**
   (`sim_devtools.cpp:520`, via `boardghost_devtools_record_uart_out`). Display
   mirroring must not depend on a printer write → need an explicit start hook
   called from runtime init.
2. **Server binds hardcoded `127.0.0.1`** (`sim_devtools.cpp:499`). Mirroring
   needs LAN reach → parameterize the bind, gated + token-protected.
3. **Audio callback only runs after a device opens lazily on first tone**
   (`sim_audio.cpp:159`). A PCM tap produces nothing until then → the mirror
   audio endpoint must force `ensure_audio_init()` and emit silence when the
   ring is empty.
4. **`sim_screenshot` writes a PNG to a path** (`sim_screenshot.cpp:32`); it
   doesn't return pixels. Need a reusable capture that returns a buffer.
5. **cpp-httplib has no WebSocket support** → use MJPEG
   (`multipart/x-mixed-replace`) for display and chunked PCM for audio, both of
   which cpp-httplib streams natively via `set_content_provider`.

---

## Phase 0 — Frame capture refactor (foundation)
**Files:** `runtime/src/sim_screenshot.cpp`, `runtime/include/sim_runtime.h`

- Extract a reusable capture from `sim_screenshot`:
  `int boardghost_capture_rgb565(std::vector<uint16_t>& out, int& w, int& h)`
  that reads from the **active display** (the device registered via
  `sim_set_active_display()`), not a passed pointer.
- Rewrite `sim_screenshot()` to call it, then PNG-encode (no behaviour change).
- Add `boardghost_encode_jpeg(const uint16_t* rgb565,int w,int h, std::vector<uint8_t>& out, int quality)`
  using `stbi_write_jpg_to_func` (stb already vendored).

**Tests:** keep `runtime/tests/test_screenshot.cpp` green; add a unit test that
`capture_rgb565` returns non-zero dims and the expected pixel for a filled rect.

**Done when:** PNG screenshot still works *and* a buffer capture + JPEG encode
are callable independently.

---

## Phase 1 — Server lifecycle + security baseline
**Files:** `runtime/src/sim_devtools.cpp`, `runtime/include/sim_devtools.h`,
runtime init (wherever `setup()` is first entered)

- Add public `void boardghost_devtools_start(void)`; call it from runtime init
  so the server is up regardless of printer activity. Keep the lazy
  UART1-triggered start as a fallback (idempotent via `g_running`).
- Parameterize the bind: read `BOARDGHOST_MIRROR` (`off`|`lan`, default off →
  loopback). When `lan`, bind `0.0.0.0` **only if** `BOARDGHOST_MIRROR_TOKEN`
  is set; otherwise log a warning and stay loopback. Mirror the OTA
  loopback-default + warn-if-no-auth pattern already established in
  `sim_ota.cpp`.
- Add `BOARDGHOST_MIRROR_PORT` (default `18082`), distinct from the devtools
  port (`18081`), so the printer/scanner UI and the mirror can coexist.
- Token gate: a shared helper `mirror_authorized(req)` checking
  `X-BoardGhost-Mirror` header or `?token=` against `BOARDGHOST_MIRROR_TOKEN`.
  Apply to **all** `/mirror/*` routes. Keep the existing CSRF
  header+Origin gate on `/scanner/inject` unchanged.

**Security review checkpoint:** this phase opens a LAN socket — run
`security-reviewer` before merge. Required invariants: loopback unless
explicitly `lan`+token; constant-time-ish token compare; no token in logs;
size caps on any request body.

**Done when:** server starts without a printer write; loopback by default;
LAN bind only with a token.

---

## Phase 2 — Display: on-demand + stream
**Files:** `runtime/src/sim_devtools.cpp`

- `GET /mirror/info` → JSON `{w,h,fps,audio:{rate:44100,channels:1,bits:16},endpoints:[...]}`.
  `endpoints` lets the Android app and agents feature-detect `/mirror/touch`.
- `GET /screen.png` → on-demand single PNG (capture → encode → respond). This
  is the **agent-loop** observe primitive: no SIGUSR1, no delay race.
- `GET /mirror/display` → `multipart/x-mixed-replace; boundary=frame` via
  `res.set_content_provider`. Loop: capture → if framebuffer hash unchanged,
  sleep one frame and skip encoding (idle CPU ≈ 0); else JPEG-encode and write
  one part. Cap at `BOARDGHOST_MIRROR_FPS` (default 15).
- Optional determinism primitive (agent loop): `GET /screen.png?stable=N` —
  capture repeatedly until N consecutive frame hashes match (or timeout),
  giving a reproducible "settled screen" without guessing a delay.

**Tests:** an integration shell test (mirror of `tests/e2e/*`) that curls
`/screen.png` from a running sketch and asserts a valid PNG of board dims; curl
`/mirror/display` for ~1s and assert ≥1 well-formed multipart frame.

**Done when:** `<img src=".../mirror/display">` shows the live board in a
browser, and `GET /screen.png` returns the current frame on demand.

---

## Phase 3 — Audio stream
**Files:** `runtime/src/sim_audio.cpp`, `runtime/src/sim_devtools.cpp`

- In `sim_audio.cpp`, add a single-producer/single-consumer lock-free ring
  (producer = `audio_callback`, consumer = HTTP). After computing `out[i]`,
  also write the sample into the ring. Expose
  `size_t boardghost_audio_drain(int16_t* dst, size_t max)` for the consumer.
- Add `boardghost_audio_ensure_started()` so the mirror endpoint can force
  `ensure_audio_init()` even if the sketch hasn't played a tone yet.
- `GET /mirror/audio` → chunked PCM via `set_content_provider`. Drain the ring;
  when empty, emit silence sized to keep a steady ~44.1 kHz mono S16 cadence so
  `AudioTrack` never underruns. Honor `BOARDGHOST_SOUND=off` (stream silence).

**Caution:** the existing callback persists phase under a mutex twice
(`sim_audio.cpp:60` and `:111`) — don't add mutex contention in the ring write;
keep it lock-free so the audio thread stays real-time-safe.

**Done when:** a tone played by the sketch appears as PCM on `/mirror/audio`
and is audible through the Android client.

---

## Phase 4 — Touch injection (the agentic keystone)
**Files:** the touch backend (`runtime/displays/Panel_sdl_bg.hpp` /
`Touch_sdl.hpp` — same queue that `BOARDGHOST_SIM_TOUCHES_SCREEN` feeds),
`runtime/src/sim_devtools.cpp`

- Factor the scripted-touch consumer so a tap can be **enqueued at runtime**,
  not just parsed from the env var at startup. Expose
  `void boardghost_inject_touch(int x, int y, bool screen_space)`.
- `POST /mirror/touch` (token-gated) → body JSON `{x,y,space:"screen"|"raw"}` →
  `boardghost_inject_touch`. Reuse the rotation/HiDPI de-rotation already in
  `Panel_sdl_bg` for `screen` space.
- This single endpoint closes the agent loop (`POST /mirror/touch` →
  `GET /screen.png?stable` → decide → repeat) **and** powers the Android
  app's touch-back. Build it once.

**Tests:** unit-test that an injected screen-space tap lands at the expected raw
coords after rotation; integration test that a `POST /mirror/touch` advances a
known sketch's UI (assert via `/screen.png` diff or a stdout marker).

**Done when:** an HTTP POST taps the running board, observable in the next
frame.

---

## Phase 5 — Discovery + CLI/Tauri surface
**Files:** `runtime/src/sim_mdns.cpp`, `crates/boardghost-cli/src/cli.rs` +
`run.rs`, `arduinoBoardEditor/.../RunSettings.svelte`

- Advertise `_boardghost-mirror._tcp` via the existing `avahi-publish` path in
  `sim_mdns.cpp`, TXT = `port,w,h,board`. Gate behind `MIRROR=lan`.
- `boardghost run --mirror`: set `MIRROR=lan`, generate a random token, print
  the URL + token (and a QR for phone pairing). Without `--mirror`, nothing
  changes (loopback, off).
- Surface a "Mirror to LAN" toggle in `RunSettings.svelte` next to the existing
  NET / screenshot controls (wire through the `RunOptions` struct).

**Done when:** `boardghost run --mirror …` prints a pairing URL/QR and the
Android app auto-discovers the instance.

---

## Environment variables added
| Var | Default | Effect |
|---|---|---|
| `BOARDGHOST_MIRROR` | `off` | `lan` opens the mirror on `0.0.0.0` (token required) |
| `BOARDGHOST_MIRROR_PORT` | `18082` | Mirror HTTP port (distinct from devtools 18081) |
| `BOARDGHOST_MIRROR_TOKEN` | (unset) | Required for LAN bind; gates all `/mirror/*` |
| `BOARDGHOST_MIRROR_FPS` | `15` | Display stream frame cap |

(Update `docs/` env-var tables and the `sim-env-vars` memory once landed.)

## Sequencing & risk
1. Phase 0 (refactor) — low risk, unblocks everything.
2. Phase 1 (lifecycle+security) — **security gate**, do before any LAN bind.
3. Phase 2 (display) — highest user-visible value; test loopback first.
4. Phase 3 (audio) — independent of 2; can parallelize.
5. Phase 4 (touch) — shared keystone for agent loop + companion.
6. Phase 5 (discovery/CLI) — polish; makes it usable without typing IPs.

Phases 2 and 4 alone deliver the **agentic GUI loop** (observe + act on
localhost). Phases 1/3/5 add the **LAN companion**. Build 0→1→2→4 first if the
agent loop is the priority; add 3 + 5 for the Android app.

## Cross-cutting requirements
- Every new endpoint: body size caps, token gate, structured-error responses.
- Keep `BOARDGHOST_DEVTOOLS=off` honored (mirror should also respect an
  off-switch — reuse or add `BOARDGHOST_MIRROR=off`).
- TDD per repo convention: tests first for the parser/coord-math/auth helpers.
- `security-reviewer` pass mandatory before merging Phase 1 (LAN socket) and
  Phase 4 (remote input injection).
