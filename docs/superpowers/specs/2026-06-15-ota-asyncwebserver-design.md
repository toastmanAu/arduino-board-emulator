# Design: ArduinoOTA + AsyncWebServer shims

**Date:** 2026-06-15
**Status:** Approved (design), pending implementation plan
**Repo:** arduino-board-emulator (BoardGhost)

## Goal

Add two backlog network shims to the BoardGhost runtime, in order:

1. **ArduinoOTA** — real espota-protocol OTA receiver + a `boardghost ota push` CLI.
2. **AsyncWebServer** (ESPAsyncWebServer) — core request/response API + Server-Sent
   Events, over a dedicated cpp-httplib server.

Both layer on existing real backings and add no new third-party dependencies.

### Hard caveat (documented, not a bug)

The emulator runs a host ELF, not an ESP32 image. A faithfully-received `.bin`
**cannot be flashed and rebooted into a running sketch.** Real OTA fidelity buys
verifiable byte-receipt (bytes land on disk, MD5-checked), IDE/`arduino-cli`
discovery, and real firing of the sketch's OTA callbacks — not a re-running sketch.

## Existing patterns this design follows

- Shims: public header in `runtime/shims/*.h` forward-declaring an `Impl` (pImpl) so
  heavy deps stay out of the header; real backing in `runtime/src/sim_*.cpp`.
- Network shims gate on `BOARDGHOST_NET` (`fake`/`fail`/`real`) via `sim_net.h`.
- Real backings already present: POSIX sockets (`sim_wifi.cpp`), cpp-httplib server
  (`sim_webserver.cpp`), OpenSSL TLS, avahi mDNS via spawned `avahi-publish*`
  children (`sim_mdns.cpp`), and the `Update` shim that writes firmware to
  `.boardghost/ota-firmware.bin` (`BOARDGHOST_OTA_PATH` override) with size + MD5
  verification (`sim_update.cpp`).
- Tests: GoogleTest files in `runtime/tests/test_*.cpp`, registered in BOTH
  `runtime/CMakeLists.txt` (source list) and `runtime/tests/CMakeLists.txt`
  (`runtime_tests` executable list).
- CLI: clap subcommands in `crates/boardghost-cli/src/cli.rs`, dispatched in
  `main.rs`. Existing `uart inject` talks to the runtime via files/FIFOs under
  `<project>/.boardghost/`.

---

## Feature 1 — ArduinoOTA (real espota protocol + CLI)

### Components

- **`runtime/shims/ArduinoOTA.h`** (new) — full API surface:
  - Builders: `setPort(uint16_t)`, `setHostname(const char*)`, `setPassword(const char*)`,
    `setPasswordHash(const char*)`, `setRebootOnSuccess(bool)`, `setMdnsEnabled(bool)`,
    `setTimeout(int)`.
  - Callbacks: `onStart(fn)`, `onEnd(fn)`, `onProgress(fn(unsigned,unsigned))`,
    `onError(fn(ota_error_t))`.
  - Lifecycle: `begin()`, `handle()`, `getCommand()`.
  - `enum ota_error_t { OTA_AUTH_ERROR, OTA_BEGIN_ERROR, OTA_CONNECT_ERROR,
    OTA_RECEIVE_ERROR, OTA_END_ERROR }`.
  - `extern ArduinoOTAClass ArduinoOTA;`
- **`runtime/src/sim_ota.cpp`** (new, pImpl) — espota state machine.
- **mDNS extension** in `ESPmDNS.h` / `sim_mdns.cpp` — add `addServiceTxt(svc, proto,
  key, value)` so the `_arduino._tcp` advertisement carries the TXT records the IDE
  expects (`board=`, `tcp_check=`, `ssh_upload=`, `auth_upload=`). avahi-publish-service
  accepts trailing `key=value` args, so this is a small addition.
- **CLI:** `Command::Ota { action: OtaAction }`, `OtaAction::Push { project, file,
  port, password }` in `cli.rs`; impl in new `crates/boardghost-cli/src/ota.rs` (a
  built-in espota client).

### espota receiver flow (`handle()`)

1. `begin()` binds **UDP :3232** (`BOARDGHOST_OTA_PORT` override), non-blocking.
   Advertises `_arduino._tcp` on that port via mDNS **only when bound to a routable
   (non-loopback) address**.
2. `handle()` does non-blocking `recvfrom`. On a datagram, parse the invite:
   `"<command> <host_tcp_port> <size> <md5>\n"` (command `0`=U_FLASH, `100`=U_SPIFFS).
3. **Auth:** if a password/hash is set, reply `"AUTH <nonce>\n"` over UDP, await the
   host's `"<cnonce> <response>"` datagram, and validate
   `response == md5(md5(password) + ":" + nonce + ":" + cnonce)`. Mismatch →
   reply `"Authentication Failed"`, fire `onError(OTA_AUTH_ERROR)`, abort.
   Reuses the MD5 implementation already used by `sim_update.cpp`.
   No password set → reply `"OK"`.
4. TCP-connect back to the datagram sender at `host_tcp_port`. Failure →
   `onError(OTA_CONNECT_ERROR)`.
5. `Update.begin(size, command)` → `onStart()`. Read `size` bytes in chunks, feed
   each to `Update.write()`, fire `onProgress(written, size)`. Short/failed read →
   `onError(OTA_RECEIVE_ERROR)`.
6. `Update.end()` (verifies size + MD5). Success → send `"OK"` on the TCP socket,
   `onEnd()`. Failure → `onError(OTA_END_ERROR)`.

### Security (flagged for security-reviewer — auth + listener code)

- UDP listener binds **127.0.0.1 only by default**, consistent with the devtools
  localhost lock (`b395a5e`, `e824e4a`). Opt-in `BOARDGHOST_OTA_BIND=0.0.0.0` for
  real over-LAN IDE testing. On localhost bind the TCP connect-back target is always
  loopback (the only possible sender), so no SSRF surface; the 0.0.0.0 opt-in connects
  back to the LAN host per espota's design and is documented as such.
- mDNS advertisement is suppressed on loopback bind (nothing routable to announce).

### CLI: `boardghost ota push`

A built-in espota client (`ota.rs`): sends the UDP invite to `127.0.0.1:<port>`,
opens a local TCP server, serves the file bytes, handles the optional auth challenge.
Exercises the exact same real receiver path — so flashing the sim needs no external
tooling. Requires the sketch running with `BOARDGHOST_NET=real` and having called
`ArduinoOTA.begin()`.

### Gating

Receiver activates under `BOARDGHOST_NET=real` (matches all other real-socket shims).
In fake mode, `begin()`/`handle()` are inert and log a one-line hint.

### Tests — `runtime/tests/test_ota.cpp`

In-process espota client over loopback UDP+TCP drives the receiver:
- Begin/write/end roundtrip → bytes match in `BOARDGHOST_OTA_PATH`, MD5 verified.
- `onStart`/`onProgress`/`onEnd` fire with correct totals.
- Size/MD5 mismatch → `onError(OTA_END_ERROR)`, no false success.
- Auth: correct password accepted; wrong password → `OTA_AUTH_ERROR`, no write.
- Connect-back failure path → `OTA_CONNECT_ERROR`.

---

## Feature 2 — AsyncWebServer (core + SSE)

### Components

- **`runtime/shims/ESPAsyncWebServer.h`** (new) — API surface.
- **`runtime/shims/AsyncTCP.h`** (new, tiny stub) — sketches `#include` it before
  ESPAsyncWebServer; empty header so the include resolves.
- **`runtime/src/sim_asyncwebserver.cpp`** (new) — own `httplib::Server`. Leaves the
  working `WebServer` untouched (lowest regression risk). The ~5-line env-port-resolve
  logic is duplicated rather than extracted, to avoid editing the working `WebServer`
  impl at all.

### API (core + SSE)

- `AsyncWebServer(uint16_t port)`; `begin()`, `end()`.
- `on(uri, WebRequestMethodComposite, ArRequestHandlerFunction)` and `on(uri, fn)`,
  where `ArRequestHandlerFunction = std::function<void(AsyncWebServerRequest*)>`.
- `onNotFound(fn)`.
- `serveStatic(uri, FS&, path, cache=nullptr)` — serves from the FS shim.
- `addHandler(AsyncWebHandler*)` — for `AsyncEventSource`.
- **`AsyncWebServerRequest`** (per-request object, no serializing mutex — maps to
  httplib's per-thread request naturally): `url()`, `method()`, `host()`,
  `params()`, `getParam(name|idx, post, file)`, `hasParam(...)`, `arg(name)`,
  `header(name)`, `hasHeader(name)`; responses: `send(code, type, body)`,
  `send_P(code, type, content, AwsTemplateProcessor)`,
  `send(FS&, path, type, download, AwsTemplateProcessor)`, `redirect(url)`,
  `beginResponse(...)` + `send(AsyncWebServerResponse*)`.
- **`AsyncWebParameter`**: `name()`, `value()`, `isPost()`, `isFile()`.
- **Template processor** `AwsTemplateProcessor = std::function<String(const String&)>`:
  scan body for `%TOKEN%`, substitute via the callback (real ESPAsyncWebServer
  behaviour).
- **`AsyncEventSource`** (SSE): `AsyncEventSource(url)`, `onConnect(fn)`,
  `send(message, event=nullptr, id=0, reconnect=0)`, `count()`.

### SSE implementation (the intricate piece)

Register the EventSource URL as a chunked streaming GET via httplib's
`set_chunked_content_provider`. Each connected client holds the connection and drains
a per-client queue guarded by a mutex + `condition_variable`. `events.send()` formats
`data: <msg>\n\n` (plus optional `event:`/`id:`/`retry:` lines) and fans it out to all
clients' queues. `end()` signals all clients to release so the server thread joins
cleanly.

### API-clash note

ESPAsyncWebServer's `HTTP_GET/POST/...` are bitmask values that differ from
`WebServer.h`'s sequential enum — identical to the real-hardware situation where you
can't include both headers in one sketch. The shim mirrors the real bitmask values
(`HTTP_GET=1, HTTP_POST=2, HTTP_DELETE=4, HTTP_PUT=8, HTTP_PATCH=16, HTTP_HEAD=32,
HTTP_OPTIONS=64, HTTP_ANY=0b01111111`) and accepts the one-or-the-other constraint.

### Ports

Own env `BOARDGHOST_ASYNC_WEBSERVER_PORT` (default the constructor port, typically 80),
distinct from `BOARDGHOST_WEBSERVER_PORT`, so a sketch can run both servers at once.

### Tests — `runtime/tests/test_asyncwebserver.cpp`

Start on an ephemeral port, drive with the vendored cpp-httplib client / raw sockets:
- `request->send` round-trips status/type/body.
- Query params parsed; `getParam`/`hasParam` correct.
- `serveStatic` serves a file written via the FS shim.
- Template processor substitutes `%TOKEN%`.
- `onNotFound` fires for unknown routes.
- SSE: connect a client, call `events.send(...)`, assert the `data:` frame arrives;
  `end()` releases the client.

---

## Cross-cutting

- **CMake:** add `src/sim_ota.cpp` + `src/sim_asyncwebserver.cpp` to the `sim_runtime`
  source list (`runtime/CMakeLists.txt`); add `test_ota.cpp` +
  `test_asyncwebserver.cpp` to the `runtime_tests` list
  (`runtime/tests/CMakeLists.txt`).
- **TDD:** write each test file first (RED), implement to green, refactor.
- **Build order:** ArduinoOTA first, then AsyncWebServer.
- **Docs/memory:** update the `sim-env-vars` cheat sheet (new `BOARDGHOST_OTA_PORT`,
  `BOARDGHOST_OTA_BIND`, `BOARDGHOST_ASYNC_WEBSERVER_PORT`) and the `boardghost-state`
  memory; move both items off the "Outstanding" list.

## New environment variables

| Var | Default | Purpose |
|-----|---------|---------|
| `BOARDGHOST_OTA_PORT` | `3232` | espota UDP listener port |
| `BOARDGHOST_OTA_BIND` | `127.0.0.1` | listener bind address; set `0.0.0.0` for LAN IDE testing |
| `BOARDGHOST_ASYNC_WEBSERVER_PORT` | constructor port | AsyncWebServer HTTP port override |

(`BOARDGHOST_OTA_PATH` already exists — where received firmware is written.)

## Out of scope (YAGNI)

- AsyncWebSocket (server-side) and AsyncJson — no in-repo consumer; add later if a
  sketch needs them.
- Actually executing a received firmware image (impossible — host ELF, not ESP32).
- ArduinoOTA SPIFFS-partition semantics beyond recording the command int.
