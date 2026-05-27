#!/usr/bin/env bash
# Headless build + run of examples/hello_serial. Asserts expected output.
set -euo pipefail

cd "$(dirname "$0")/../.."
export SDL_VIDEODRIVER=dummy
export BOARDGHOST_BOARDS="$PWD/runtime/boards"
export BOARDGHOST_RUNTIME="$PWD/runtime"

OUT=$(cargo run -q -p boardghost-cli -- \
        run examples/hello_serial \
        --board ssd1306_uno_sim 2>&1)

echo "$OUT" | grep -q "Hello from BoardGhost"   || { echo "FAIL: missing hello"; echo "--- OUT ---"; echo "$OUT"; exit 1; }
echo "$OUT" | grep -q "tick 0"                  || { echo "FAIL: missing tick 0"; echo "--- OUT ---"; echo "$OUT"; exit 1; }
echo "$OUT" | grep -q "tick 4"                  || { echo "FAIL: missing tick 4"; echo "--- OUT ---"; echo "$OUT"; exit 1; }
echo "$OUT" | grep -q "done"                    || { echo "FAIL: missing done"; echo "--- OUT ---"; echo "$OUT"; exit 1; }
echo "PASS: hello_serial"
