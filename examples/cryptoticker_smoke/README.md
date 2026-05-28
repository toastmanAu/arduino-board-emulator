# cryptoticker_smoke

Smoke test sketch for the M2.C library auto-discovery path. Exercises:
- WiFi shim (M2.B)
- HTTPClient shim (M2.B)
- ArduinoJson via auto-discovery (M2.C)
- ArduinoWebsockets via header-only shim (M2.C)

## Run

```bash
arduino-cli lib install ArduinoJson
boardghost run --board st7789_esp32s3_sim examples/cryptoticker_smoke
```

Auto-exits after ~200 loop iterations so it works in headless CI.
