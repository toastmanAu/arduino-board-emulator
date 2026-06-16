# Directive: BoardGhost Mirror — Android companion app

**Audience:** a coding agent building this app in a *separate* repo.
**Goal:** an Android app that mirrors a running BoardGhost desktop emulator's
**display** and **audio** over the LAN, with optional **touch-back** remote
control.

This is a standalone client. It depends only on the HTTP endpoints the desktop
emulator exposes (see "Desktop contract" below). It must feature-detect
endpoints and degrade gracefully — older desktop builds may expose only a
subset.

---

## Scope

In scope:
- Discover + connect to a desktop emulator on the LAN.
- Mirror the board display (live, ~15 fps).
- Mirror the board audio (LEDC tone synthesis, mono 16-bit PCM).
- Optional touch-back: tap the mirrored screen → inject a touch on the board.

Explicitly **out of scope** (do not build):
- Firmware flashing / OTA.
- GPIO inspector, serial monitor, receipt/scanner UIs (those stay on desktop).
- Screen/audio recording, cloud relay, account system.
- Any non-LAN transport.

---

## Desktop contract (the API this app consumes)

The desktop exposes these over HTTP on the **mirror port** (default `18082`,
advertised via mDNS). All requests carry the mirror token.

| Endpoint | Method | Response | Notes |
|---|---|---|---|
| `GET /mirror/info` | GET | JSON `{w,h,fps,audio:{rate,channels,bits}}` | Feature-detect; board dims for letterboxing |
| `GET /mirror/display` | GET | `multipart/x-mixed-replace; boundary=frame`, JPEG parts | Long-lived MJPEG stream |
| `GET /mirror/audio` | GET | `application/octet-stream`, chunked raw PCM | Continuous; silence when board is quiet |
| `POST /mirror/touch` | POST | `200 ok` | Body JSON `{x,y,space:"screen"}`; screen-pixel coords |

**Auth:** every request sends the token as header `X-BoardGhost-Mirror: <token>`
(preferred) or `?token=<token>` query param. A missing/wrong token returns 403.

**Discovery:** desktop advertises mDNS/DNS-SD service type
`_boardghost-mirror._tcp`. TXT record carries:
`port=<n>`, `w=<px>`, `h=<px>`, `board=<profile-name>`. The token is **not** in
the TXT record — the user pairs it once (QR or manual entry).

---

## Architecture

```
┌─────────────── Android app ───────────────┐
│  DiscoveryRepo (NSD/mDNS) ──► ConnectionVM │
│                                            │
│  DisplayStream ──► MjpegDecoder ──► Surface│
│  AudioStream   ──► PcmPlayer (AudioTrack)  │
│  TouchSink     ◄── tap gestures            │
└────────────────────────────────────────────┘
```

### Stack
- **Kotlin**, `minSdk 26`, single-activity Jetpack Compose.
- **OkHttp** for both streams — it handles chunked transfer and lets you read
  the raw `multipart/x-mixed-replace` body as an `InputStream`. Do **not** pull
  in WebRTC, ExoPlayer, or an RTSP lib; they're overkill for a LAN MJPEG/PCM
  mirror and the desktop doesn't speak those protocols.
- **`android.net.nsd.NsdManager`** for mDNS discovery.
- **`SurfaceView`/`TextureView`** sink for frames; **`AudioTrack`** in
  `MODE_STREAM` for PCM.

---

## Implementation milestones

### M1 — Display mirror (manual connect)
1. Settings screen: enter `host:port` + token, persist in DataStore.
2. `GET /mirror/info` → cache `w,h`.
3. Open `GET /mirror/display`. Parse the multipart stream:
   - Read boundary `--frame`, then per-part headers (`Content-Type`,
     `Content-Length`), then the JPEG bytes.
   - Decode with `BitmapFactory.decodeByteArray`; reuse a `Bitmap` via
     `inBitmap` to avoid GC churn at 15 fps.
   - Blit to the `SurfaceView`, **letterboxed** to preserve the board aspect
     ratio (`w:h` from `/mirror/info`).
4. Reconnect with exponential backoff (200ms→5s) when the stream ends — sketch
   restarts and idle-frame gaps are normal, not errors.

**Done when:** a running desktop sketch's screen appears live on the phone.

### M2 — Audio mirror
1. Open `GET /mirror/audio`. First bytes are a small fixed header — prefer the
   values from `/mirror/info.audio` (`rate=44100, channels=1, bits=16`) and
   treat the body as pure PCM.
2. Configure `AudioTrack` (`STREAM_MUSIC`, `MODE_STREAM`,
   `ENCODING_PCM_16BIT`, mono, 44100).
3. Maintain a ~100–200 ms jitter buffer; prioritize continuity over latency
   (these are short UI bleeps, not music). Underrun → write silence, don't
   stutter.
4. Mute toggle in the UI (default **on**/unmuted, but make muting one tap).

**Done when:** `playTune()` bleeps from the desktop sketch are audible on the
phone, roughly in sync with the on-screen action.

### M3 — Discovery
1. `NsdManager.discoverServices("_boardghost-mirror._tcp")`.
2. List discovered desktops (use `board` TXT for a friendly name).
3. Tapping one fills host/port; token still required (pair via QR scan of the
   URL the desktop's `--mirror` prints, or manual entry).

**Done when:** the app finds a desktop on the same Wi-Fi without typing an IP.

### M4 — Touch-back (feature-detected)
1. On connect, probe `/mirror/info`; enable touch-back only if `POST
   /mirror/touch` is advertised (e.g. an `endpoints` array in the info JSON, or
   a successful `OPTIONS`).
2. Map a tap on the rendered surface back to **board screen coords**: invert
   the letterbox transform (subtract letterbox offset, divide by scale).
3. `POST /mirror/touch {x,y,space:"screen"}`.
4. Setting to disable (view-only mode) — default **off** so the app is a viewer
   until the user opts into control.

**Done when:** tapping the mirrored screen drives the board's UI.

---

## Non-functional requirements
- **Resilience:** every stream auto-reconnects; a desktop restart must not
  require an app restart.
- **Battery/CPU:** stop both streams when the app is backgrounded; resume on
  foreground.
- **Security posture:** token required; warn the user if connecting to a
  non-RFC1918 host (mirror is meant for LAN only).
- **No secrets in logs.** Never log the token.

## Test plan
- Unit: multipart frame parser (feed canned multipart bytes, assert frame
  boundaries + payloads). Letterbox coord-inversion math.
- Integration: point at a desktop running `boardghost run --mirror`; verify
  display fps, audio continuity on `playTune`, touch-back lands a tap (visible
  on the desktop window).
- Resilience: kill + relaunch the desktop sketch mid-stream; app must recover.
