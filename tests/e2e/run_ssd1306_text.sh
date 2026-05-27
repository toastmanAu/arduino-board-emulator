#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."
export SDL_VIDEODRIVER=dummy
export BOARDGHOST_BOARDS="$PWD/runtime/boards"
export BOARDGHOST_RUNTIME="$PWD/runtime"

OUT=$(cargo run -q -p boardghost-cli -- \
        run examples/ssd1306_text \
        --board ssd1306_uno_sim 2>&1)

echo "$OUT" | grep -q "ssd1306_text started" || { echo "FAIL: missing init log"; echo "--- OUT ---"; echo "$OUT"; exit 1; }
echo "$OUT" | grep -q "frame 0"              || { echo "FAIL: missing frame 0"; exit 1; }
echo "$OUT" | grep -q "frame 4"              || { echo "FAIL: missing frame 4"; exit 1; }
echo "$OUT" | grep -q "done"                 || { echo "FAIL: missing done"; exit 1; }
echo "PASS: ssd1306_text"
