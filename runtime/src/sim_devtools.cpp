// Devtools web server. Spawns cpp-httplib on BOARDGHOST_DEVTOOLS_PORT
// (default 18081) and serves two browser-driven peripheral UIs:
//
//   GET  /receipt           HTML — live thermal-printer tape viewer
//   GET  /receipt/data      JSON — { hex: "...", text: "..." } for polling
//   GET  /scanner           HTML — text-input + USB-camera QR injector
//   POST /scanner/inject    body = barcode text → writes uart-2-queue.bin
//
// The HTML is inlined as raw string literals — the alternative (a separate
// dist/ dir served as static files) would mean shipping files alongside the
// runtime binary and dealing with where they live at install time. Inlining
// keeps the runtime self-contained. The pages pull jsQR from a CDN for
// camera-side QR decoding (BOARDGHOST_NET=real already implies internet).
//
// BOARDGHOST_DEVTOOLS=off disables the server (CI, no-network, paranoia).

#include "sim_devtools.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr size_t kPrinterRingMax = 64 * 1024;  // 64 KB scrollback per port

struct PrinterPort {
    std::vector<uint8_t> ring;
    std::mutex           mtx;
};

PrinterPort g_printer;  // UART1 is the only printer we surface in v1
std::atomic<bool> g_running{false};
std::unique_ptr<httplib::Server> g_srv;
std::thread g_thread;

fs::path project_state_dir() {
    if (const char* env = std::getenv("BOARDGHOST_UART_DIR"); env && *env) {
        return fs::path(env);
    }
    auto cwd = fs::current_path();
    return cwd.parent_path().parent_path();
}

bool disabled() {
    const char* env = std::getenv("BOARDGHOST_DEVTOOLS");
    return env && (std::strcmp(env, "off") == 0 || std::strcmp(env, "0") == 0);
}

uint16_t devtools_port() {
    if (const char* env = std::getenv("BOARDGHOST_DEVTOOLS_PORT"); env && *env) {
        int p = std::atoi(env);
        if (p > 0 && p < 65536) return (uint16_t)p;
    }
    return 18081;
}

std::string to_hex(const std::vector<uint8_t>& v) {
    static const char* HEX = "0123456789abcdef";
    std::string out;
    out.reserve(v.size() * 2);
    for (uint8_t b : v) {
        out.push_back(HEX[b >> 4]);
        out.push_back(HEX[b & 0xF]);
    }
    return out;
}

// ----- Inlined HTML for the two browser pages -----
//
// Kept as raw string literals so the runtime binary is self-contained. The
// JS is intentionally vanilla — no build step, no bundler, just <script>.

const char* kReceiptHtml = R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"/>
<title>BoardGhost — Receipt</title>
<style>
  :root { color-scheme: dark; }
  body { background: #1a1a1a; color: #e6e6e6; font-family: ui-monospace, monospace; margin: 0; padding: 20px; }
  .tape { background: #f5efe6; color: #1a1a1a; max-width: 384px; margin: 0 auto; padding: 24px 18px;
          box-shadow: 0 4px 20px rgba(0,0,0,.5); font: 14px/1.5 "Courier New", monospace; }
  .tape .row     { white-space: pre-wrap; word-break: break-all; min-height: 1.2em; }
  .tape .center  { text-align: center; }
  .tape .right   { text-align: right; }
  .tape .left    { text-align: left; }
  .tape .bold    { font-weight: bold; }
  .tape .uline   { text-decoration: underline; }
  .tape .inverse { background: #1a1a1a; color: #f5efe6; padding: 0 2px; }
  .tape .dh      { font-size: 1.6em; line-height: 1.2; }
  .tape .dw      { letter-spacing: 0.2em; }
  .tape .dhw     { font-size: 1.6em; letter-spacing: 0.2em; line-height: 1.2; }
  .tape .qr      { display: inline-block; margin: 8px 0; padding: 8px;
                   background: #fff; border: 1px solid #ccc; }
  .tape .qr svg  { display: block; image-rendering: pixelated; }
  header { color: #888; font-size: 12px; text-align: center; margin-bottom: 12px; }
  footer { color: #555; font-size: 11px; text-align: center; margin-top: 12px; }
</style>
</head>
<body>
<header>BoardGhost printer tape — UART1 — auto-refreshing</header>
<div id="tape" class="tape"></div>
<footer><span id="byte-count">0</span> bytes captured · refreshing every 500ms</footer>
<!-- qrcode-generator pinned by SHA-384 SRI so a CDN compromise can't
     substitute an implementation that exfiltrates the payload. QR
     payloads (payment URIs, addresses) stay entirely in the browser. -->
<script src="https://cdn.jsdelivr.net/npm/qrcode-generator@1.4.4/qrcode.js"
        integrity="sha384-8FWZA6BGMXhsfO+BLtrJK0We6gg5o1JyO8xQm6peWDEUs17ACA5ziE/NIAkl9z2k"
        crossorigin="anonymous"></script>
<script>
// ESC/POS parser. We only care about the subset that affects the visual
// receipt output — formatting, justify, QR codes. Anything else is stripped.
function parse(hex) {
  const bytes = [];
  for (let i = 0; i + 1 < hex.length; i += 2) bytes.push(parseInt(hex.substr(i, 2), 16));
  const lines = [];
  let cur = { justify: "left", bold: false, uline: false, inverse: false, dh: false, dw: false, text: "" };
  function flush() {
    if (cur.text.length === 0) { lines.push(Object.assign({}, cur)); return; }
    lines.push(Object.assign({}, cur));
    cur.text = "";
  }
  let i = 0;
  while (i < bytes.length) {
    const b = bytes[i];
    if (b === 0x0A) { flush(); i++; continue; }
    if (b === 0x0D) { i++; continue; }  // CR — handled as part of CRLF
    if (b === 0x1B && i + 1 < bytes.length) {
      const op = bytes[i + 1];
      if (op === 0x40)                  { i += 2; flush(); cur.bold=cur.uline=cur.inverse=cur.dh=cur.dw=false; cur.justify="left"; continue; }
      if (op === 0x45 && i+2<bytes.length){ cur.bold = !!bytes[i+2]; i += 3; continue; }
      if (op === 0x2D && i+2<bytes.length){ cur.uline = !!bytes[i+2]; i += 3; continue; }
      if (op === 0x61 && i+2<bytes.length){ const j = bytes[i+2]; cur.justify = j===1?"center":j===2?"right":"left"; i += 3; continue; }
      if (op === 0x21 && i+2<bytes.length){ const m = bytes[i+2]; cur.dh = (m&0x10)!==0; cur.dw = (m&0x20)!==0; cur.bold = cur.bold || (m&0x08)!==0; i += 3; continue; }
      if (op === 0x47 && i+2<bytes.length){ i += 3; continue; }   // strike — ignored visually
      i += 2; continue;  // unknown ESC X — skip
    }
    if (b === 0x1D && i + 1 < bytes.length) {
      const op = bytes[i + 1];
      if (op === 0x42 && i+2<bytes.length){ cur.inverse = !!bytes[i+2]; i += 3; continue; }
      if (op === 0x21 && i+2<bytes.length){ const s = bytes[i+2]; cur.dh = (s&0x01)!==0; cur.dw = (s&0x10)!==0; i += 3; continue; }
      if (op === 0x28 && bytes[i+2] === 0x6B) {
        // QR command series: GS ( k pL pH cn fn (m) (d1..dk)
        // pL+pH*256 counts bytes from cn (i+5) to end-of-frame, so the
        // frame total is i..i+5+len. For store-data (fn=0x50/'P'), the
        // single m byte at i+7 is followed by payload bytes i+8..i+5+len.
        const pL = bytes[i+3], pH = bytes[i+4];
        const len = pL + 256 * pH;
        const fn = bytes[i+6];
        const end = i + 5 + len;
        if (fn === 0x50 /* store data */ && end <= bytes.length) {
          const data = bytes.slice(i + 8, end);
          const txt = data.map(c => String.fromCharCode(c)).join("");
          flush();
          lines.push({ justify: cur.justify, qr: txt });
        }
        i = end; continue;
      }
      i += 2; continue;
    }
    // Printable byte (or 8-bit ext). Append.
    if (b === 0x09) { cur.text += "\t"; }
    else if (b >= 0x20)              cur.text += String.fromCharCode(b);
    i++;
  }
  if (cur.text.length) flush();
  return lines;
}
function render(lines) {
  const tape = document.getElementById("tape");
  tape.innerHTML = "";
  for (const l of lines) {
    const row = document.createElement("div");
    row.className = "row " + (l.justify || "left");
    if (l.qr) {
      const wrap = document.createElement("span"); wrap.className = "qr";
      // Render the QR locally in the browser — never send the payload to
      // any third-party service. qrcode-generator is loaded with SRI pin
      // so a CDN compromise can't substitute a leaky implementation.
      try {
        const qr = qrcode(0 /* auto type number */, "M" /* error correction */);
        qr.addData(l.qr);
        qr.make();
        // createSvgTag returns an SVG string sized to fit; cellSize=4 gives
        // ~160px for a typical address-sized payload.
        const svg = qr.createSvgTag({ cellSize: 4, margin: 2 });
        const parser = new DOMParser();
        const doc = parser.parseFromString(svg, "image/svg+xml");
        const node = doc.documentElement;
        node.setAttribute("title", l.qr);
        wrap.appendChild(node);
      } catch (e) {
        const err = document.createElement("div");
        err.style.color = "#a31"; err.textContent = "QR render failed: " + e.message;
        wrap.appendChild(err);
      }
      const cap = document.createElement("div"); cap.style.fontSize = "10px"; cap.style.marginTop = "4px";
      cap.style.wordBreak = "break-all"; cap.textContent = l.qr;
      wrap.appendChild(cap);
      row.appendChild(wrap);
    } else {
      const cls = [
        l.bold ? "bold" : "",
        l.uline ? "uline" : "",
        l.inverse ? "inverse" : "",
        (l.dh && l.dw) ? "dhw" : l.dh ? "dh" : l.dw ? "dw" : "",
      ].filter(Boolean).join(" ");
      const span = document.createElement("span");
      if (cls) span.className = cls;
      span.textContent = l.text;
      row.appendChild(span);
    }
    tape.appendChild(row);
  }
}
async function poll() {
  try {
    const r = await fetch("/receipt/data");
    if (!r.ok) return;
    const j = await r.json();
    document.getElementById("byte-count").textContent = (j.hex.length / 2);
    render(parse(j.hex));
  } catch (e) {}
}
setInterval(poll, 500);
poll();
</script>
</body></html>)HTML";

const char* kScannerHtml = R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"/>
<title>BoardGhost — Scanner</title>
<style>
  :root { color-scheme: dark; }
  body { background: #1a1a1a; color: #e6e6e6; font-family: ui-monospace, monospace; margin: 0; padding: 20px; }
  .panel { max-width: 480px; margin: 0 auto; padding: 24px; background: #222; border-radius: 8px; }
  h1 { margin-top: 0; font-size: 16px; color: #ccc; }
  textarea, input[type=text] { width: 100%; box-sizing: border-box; background: #111; color: #e6e6e6;
                               border: 1px solid #444; border-radius: 4px; padding: 8px; font: 13px ui-monospace, monospace; }
  button { background: #2a6; color: #fff; border: none; padding: 10px 16px; border-radius: 4px;
           font-weight: bold; cursor: pointer; margin-top: 8px; }
  button:hover { background: #3b7; }
  button.secondary { background: #444; }
  .status { margin-top: 12px; padding: 8px 12px; border-radius: 4px; font-size: 13px; }
  .status.ok   { background: #1a3; color: #fff; }
  .status.err  { background: #a31; color: #fff; }
  .camera { margin-top: 20px; }
  .cam-wrap { position: relative; max-width: 480px; margin: 0 auto; }
  video { width: 100%; background: #000; border-radius: 4px; display: block; }
  .reticle { position: absolute; inset: 0; pointer-events: none;
             display: flex; align-items: center; justify-content: center; }
  .reticle .box { width: 60%; aspect-ratio: 1; border: 2px solid #2a6;
                  border-radius: 8px; box-shadow: 0 0 0 9999px rgba(0,0,0,.35); }
  .scanning .reticle .box { border-color: #fc3; animation: pulse 1s ease-in-out infinite; }
  @keyframes pulse { 50% { border-color: #fff; } }
  .upload-zone { margin-top: 12px; padding: 14px; border: 2px dashed #444; border-radius: 4px;
                 text-align: center; color: #888; font-size: 12px; cursor: pointer; }
  .upload-zone.dragover { border-color: #2a6; color: #ccc; }
  canvas { display: none; }
  footer { color: #555; font-size: 11px; text-align: center; margin-top: 16px; }
</style>
</head>
<body>
<div class="panel">
  <h1>Scanner input → UART2 queue</h1>
  <p style="font-size:13px;color:#aaa">Type or paste a barcode/QR payload, or point your USB camera at a QR code on screen. Either way, hits the same code path as <code>boardghost uart inject --queue</code> — fires on the sketch's next scanner-trigger click.</p>
  <textarea id="text" rows="3" placeholder="e.g. ckb1qzda…"></textarea>
  <div>
    <label style="font-size:12px;color:#888"><input type="checkbox" id="crlf" checked> append CR+LF (most scanners do)</label>
  </div>
  <button id="inject">Inject as scan</button>
  <button id="clear" class="secondary">Clear queue</button>
  <div id="status" class="status" style="display:none"></div>

  <div class="camera">
    <h1>Camera capture</h1>
    <p style="font-size:12px;color:#888">Show a QR code to your camera — auto-detects, fills the text box, injects. Hold it in the green box ~10-20cm away.</p>
    <button id="start-cam">Start camera</button>
    <button id="stop-cam" class="secondary">Stop</button>
    <div class="cam-wrap" id="cam-wrap">
      <video id="video" playsinline autoplay muted></video>
      <div class="reticle"><div class="box"></div></div>
    </div>
    <canvas id="canvas"></canvas>
    <div id="upload-zone" class="upload-zone">
      Or drop a QR image here (or click) — works when the camera can't focus
      <input type="file" id="file-input" accept="image/*" style="display:none">
    </div>
  </div>
</div>
<footer>BoardGhost scanner devtools · UART2 queue</footer>
<!-- SRI: pin jsQR@1.4.0 by content hash so a CDN compromise can't inject
     arbitrary JS into the scanner devtools page. Hash verified against
     the official jsdelivr release. -->
<script src="https://cdn.jsdelivr.net/npm/jsqr@1.4.0/dist/jsQR.js"
        integrity="sha384-b5Ya4Bq3qCyz39m2ISh+4DxjAIljdeFwK/BsXLuj9gugaNwAcj/ia15fxNZL9Nlx"
        crossorigin="anonymous"></script>
<script>
const $ = (id) => document.getElementById(id);
function setStatus(msg, ok) {
  const s = $("status"); s.textContent = msg; s.className = "status " + (ok ? "ok" : "err"); s.style.display = "block";
  setTimeout(() => { s.style.display = "none"; }, 4000);
}
// All POSTs carry an X-BoardGhost-Devtools header so a malicious cross-
// origin page can't drive injects via a simple <form> submission — the
// custom header forces a CORS preflight that we never satisfy.
const INJECT_HEADERS = { "Content-Type": "text/plain", "X-BoardGhost-Devtools": "1" };
async function inject(text) {
  if (!text) return setStatus("nothing to inject", false);
  const crlf = $("crlf").checked;
  const body = crlf ? text + "\r\n" : text;
  try {
    const r = await fetch("/scanner/inject", { method: "POST", body, headers: INJECT_HEADERS });
    if (r.ok) setStatus("Queued " + body.length + " bytes — click scanner UI on sketch to fire", true);
    else     setStatus("inject failed: " + (await r.text()), false);
  } catch (e) { setStatus("network error: " + e, false); }
}
$("inject").addEventListener("click", () => inject($("text").value.trim()));
$("clear").addEventListener("click", () =>
  fetch("/scanner/inject", { method: "POST", body: "", headers: INJECT_HEADERS })
    .then(() => setStatus("queue cleared", true)));

let stream = null, raf = null;
async function startCam() {
  try {
    // Ask for the highest resolution the host camera can give — jsQR
    // detection rate improves dramatically with pixel density. Falls back
    // to whatever's available if 1920x1080 isn't supported.
    stream = await navigator.mediaDevices.getUserMedia({
      video: {
        facingMode: { ideal: "environment" },
        width:  { ideal: 1920, min: 640 },
        height: { ideal: 1080, min: 480 },
        frameRate: { ideal: 30, min: 15 }
      }
    });
    $("video").srcObject = stream;
    $("cam-wrap").classList.add("scanning");
    raf = requestAnimationFrame(scan);
  } catch (e) { setStatus("camera failed: " + e.message, false); }
}
function stopCam() {
  if (raf) { cancelAnimationFrame(raf); raf = null; }
  if (stream) { stream.getTracks().forEach(t => t.stop()); stream = null; }
  $("video").srcObject = null;
  $("cam-wrap").classList.remove("scanning");
}
// Try jsQR on full frame, then on a center crop. The crop helps when the
// QR is centred in the reticle but small in frame — jsQR's locator runs
// faster on tighter regions and is less likely to misidentify finder
// patterns from background noise.
function tryDecode(ctx, w, h) {
  const full = ctx.getImageData(0, 0, w, h);
  let code = jsQR(full.data, w, h, { inversionAttempts: "attemptBoth" });
  if (code && code.data) return code.data;
  // Center crop: 60% of the smaller dimension, square.
  const size = Math.floor(Math.min(w, h) * 0.6);
  const x = Math.floor((w - size) / 2), y = Math.floor((h - size) / 2);
  const crop = ctx.getImageData(x, y, size, size);
  code = jsQR(crop.data, size, size, { inversionAttempts: "attemptBoth" });
  return code && code.data ? code.data : null;
}
function scan() {
  const v = $("video"), c = $("canvas");
  if (v.readyState === v.HAVE_ENOUGH_DATA) {
    c.width = v.videoWidth; c.height = v.videoHeight;
    const ctx = c.getContext("2d", { willReadFrequently: true });
    ctx.drawImage(v, 0, 0, c.width, c.height);
    const data = tryDecode(ctx, c.width, c.height);
    if (data) {
      $("text").value = data;
      inject(data);
      stopCam();
      return;
    }
  }
  raf = requestAnimationFrame(scan);
}
$("start-cam").addEventListener("click", startCam);
$("stop-cam").addEventListener("click", stopCam);

// File upload fallback — for when the camera just won't focus. Drop a PNG
// of a QR (screenshot from your phone, saved from a wallet) and we decode
// it the same way as a camera frame.
const uploadZone = $("upload-zone");
const fileInput = $("file-input");
function decodeImageFile(file) {
  if (!file || !file.type.startsWith("image/")) { setStatus("not an image file", false); return; }
  const url = URL.createObjectURL(file);
  const img = new Image();
  img.onload = () => {
    const c = $("canvas");
    c.width = img.naturalWidth; c.height = img.naturalHeight;
    const ctx = c.getContext("2d");
    ctx.drawImage(img, 0, 0);
    URL.revokeObjectURL(url);
    const data = tryDecode(ctx, c.width, c.height);
    if (data) {
      $("text").value = data;
      inject(data);
    } else {
      setStatus("no QR detected in image (try better lighting/contrast)", false);
    }
  };
  img.onerror = () => { setStatus("couldn't load image", false); URL.revokeObjectURL(url); };
  img.src = url;
}
uploadZone.addEventListener("click", () => fileInput.click());
fileInput.addEventListener("change", (e) => decodeImageFile(e.target.files && e.target.files[0]));
["dragenter", "dragover"].forEach(ev =>
  uploadZone.addEventListener(ev, (e) => { e.preventDefault(); uploadZone.classList.add("dragover"); }));
["dragleave", "drop"].forEach(ev =>
  uploadZone.addEventListener(ev, (e) => { e.preventDefault(); uploadZone.classList.remove("dragover"); }));
uploadZone.addEventListener("drop", (e) => {
  e.preventDefault();
  const file = e.dataTransfer.files && e.dataTransfer.files[0];
  decodeImageFile(file);
});
</script>
</body></html>)HTML";

void start_server() {
    if (disabled()) return;
    if (g_running.load()) return;
    uint16_t port = devtools_port();
    g_srv = std::make_unique<httplib::Server>();

    g_srv->Get("/receipt", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kReceiptHtml, "text/html");
    });
    g_srv->Get("/receipt/data", [](const httplib::Request&, httplib::Response& res) {
        std::vector<uint8_t> snap;
        {
            std::lock_guard<std::mutex> lk(g_printer.mtx);
            snap = g_printer.ring;
        }
        std::string body = "{\"hex\":\"" + to_hex(snap) + "\"}";
        res.set_content(body, "application/json");
    });
    g_srv->Get("/scanner", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kScannerHtml, "text/html");
    });
    g_srv->Post("/scanner/inject", [](const httplib::Request& req, httplib::Response& res) {
        // CSRF defense: require a custom header that a malicious cross-origin
        // page can't add via a simple form submission. Browsers will do a
        // CORS preflight (OPTIONS) for any request that carries a custom
        // header, and we never respond to that preflight with
        // Access-Control-Allow-* so the preflight fails and the real POST
        // never goes out. Same-origin requests from /scanner work because
        // they're not subject to CORS preflight rules.
        if (req.get_header_value("X-BoardGhost-Devtools") != "1") {
            res.status = 403;
            res.set_content("missing X-BoardGhost-Devtools header", "text/plain");
            return;
        }
        // Belt-and-braces Origin check. Reject anything that isn't our own
        // localhost binding.
        auto origin = req.get_header_value("Origin");
        if (!origin.empty() &&
            origin.find("http://127.0.0.1:") != 0 &&
            origin.find("http://localhost:")  != 0) {
            res.status = 403;
            res.set_content("bad origin", "text/plain");
            return;
        }
        auto qpath = project_state_dir() / "uart-2-queue.bin";
        std::error_code ec;
        fs::create_directories(qpath.parent_path(), ec);
        std::ofstream out(qpath, std::ios::binary | std::ios::trunc);
        if (!out) { res.status = 500; res.set_content("write failed", "text/plain"); return; }
        out.write(req.body.data(), req.body.size());
        res.set_content("ok", "text/plain");
    });
    g_srv->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            "<!DOCTYPE html><html><body style='background:#1a1a1a;color:#ccc;font:14px ui-monospace,monospace;padding:40px'>"
            "<h1>BoardGhost devtools</h1>"
            "<ul><li><a style='color:#6cf' href='/receipt'>/receipt</a> — live thermal printer tape</li>"
            "<li><a style='color:#6cf' href='/scanner'>/scanner</a> — QR/text scanner input</li></ul>"
            "</body></html>",
            "text/html");
    });

    // Bind to localhost only — devtools is dev-only and the POST endpoint
    // mutates sketch I/O. The sketch's own WebServer (sim_webserver.cpp)
    // intentionally binds 0.0.0.0 because it's meant to be LAN-reachable;
    // devtools has the opposite contract.
    if (!g_srv->bind_to_port("127.0.0.1", port)) {
        std::fprintf(stderr,
            "[boardghost] devtools: bind to port %u failed — receipt/scanner UIs unavailable\n",
            port);
        g_srv.reset();
        return;
    }
    g_running.store(true);
    g_thread = std::thread([]() {
        g_srv->listen_after_bind();
        g_running.store(false);
    });
    std::fprintf(stderr,
        "[boardghost] devtools: http://localhost:%u/  (printer /receipt, scanner /scanner)\n",
        port);
}

}  // namespace

extern "C" void boardghost_devtools_start(void) {
    start_server();  // idempotent; binds 127.0.0.1 only
}

extern "C" void boardghost_devtools_stop(void) {
    if (g_srv) g_srv->stop();
    if (g_thread.joinable()) g_thread.join();
    g_srv.reset();
    g_running.store(false);
}

extern "C" void boardghost_devtools_record_uart_out(int port_nr, const uint8_t* buf, size_t n) {
    if (port_nr != 1 || !buf || n == 0) return;  // only UART1 (printer) for now
    start_server();   // lazy init on first byte
    std::lock_guard<std::mutex> lk(g_printer.mtx);
    g_printer.ring.insert(g_printer.ring.end(), buf, buf + n);
    if (g_printer.ring.size() > kPrinterRingMax) {
        // Drop oldest bytes to stay in cap. ESC/POS parser handles partial
        // command sequences at the start gracefully (skips unknown leading
        // bytes until the next 0x1B / 0x1D / 0x0A).
        g_printer.ring.erase(g_printer.ring.begin(),
                             g_printer.ring.begin() + (g_printer.ring.size() - kPrinterRingMax));
    }
}
