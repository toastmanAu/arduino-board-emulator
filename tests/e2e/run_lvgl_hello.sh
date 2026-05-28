#!/usr/bin/env bash
# Headless build + run of LVGL hello world on ILI9488.
# M1 doesn't inject touch in headless mode — we verify init + frames + clean exit.
set -euo pipefail

cd "$(dirname "$0")/../.."
export SDL_VIDEODRIVER=dummy
export BOARDGHOST_BOARDS="$PWD/runtime/boards"
export BOARDGHOST_RUNTIME="$PWD/runtime"
export BOARDGHOST_FRAME_LIMIT=200

OUT=$(cargo run -q -p boardghost-cli -- \
        run examples/lvgl_hello_ili9488 \
        --board ili9488_esp32s3_sim 2>&1)

echo "$OUT" | grep -q "lvgl_hello_ili9488 started" || { echo "FAIL: missing init"; echo "--- OUT ---"; echo "$OUT" | tail -50; exit 1; }
echo "$OUT" | grep -q "frame 50"                   || { echo "FAIL: missing frame 50"; echo "--- OUT ---"; echo "$OUT" | tail -50; exit 1; }
echo "$OUT" | grep -q "frame 200" || echo "$OUT" | grep -q "frame 150" || { echo "FAIL: not enough frames"; exit 1; }
echo "$OUT" | grep -q "done"                       || { echo "FAIL: missing done"; exit 1; }
echo "PASS: lvgl_hello_ili9488"
