#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."

# Build via boardghost (should fire the LGFX codemod).
cargo run -p boardghost-cli --quiet -- build \
    --board st7789_esp32s3_sim \
    examples/lgfx_codemod_smoke 2>&1 | tee /tmp/lgfx_codemod_smoke.build.log

if ! grep -q "LGFX codemod: rewriting" /tmp/lgfx_codemod_smoke.build.log; then
    echo "❌ codemod did not fire on lgfx_codemod_smoke"
    tail -20 /tmp/lgfx_codemod_smoke.build.log
    exit 1
fi

# Run and check output.
TMPDIR=$(mktemp -d)
cargo run -p boardghost-cli --quiet -- run \
    --board st7789_esp32s3_sim \
    --screenshot "$TMPDIR/shot.png" \
    examples/lgfx_codemod_smoke \
    > "$TMPDIR/out.log" 2>&1 || true

if grep -q "lgfx_codemod_smoke: done" "$TMPDIR/out.log"; then
    echo "✅ lgfx_codemod_smoke E2E passed"
    rm -rf "$TMPDIR"
    exit 0
fi

echo "❌ lgfx_codemod_smoke did not reach end:"
tail -40 "$TMPDIR/out.log"
exit 1
