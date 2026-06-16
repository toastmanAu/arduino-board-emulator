#pragma once
#include <cstdint>
#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle. Called from sim_main.cpp; tests don't link sim_main.
void sim_runtime_init(int argc, char** argv);
void sim_runtime_shutdown(void);

// Event pump. Called once per loop() iteration in sim_main.
void sim_pump_events(void);

// Quit signal (window close, Ctrl-C). Drives the main loop.
int  sim_should_quit(void);

// Diagnostic logging — goes to stderr to keep stdout clean for Serial.
void sim_log(const char* msg);

// Emit a GPIO-state log line on stderr with the `[gpio] ` prefix.
// Format: "[gpio] <op> <pin> <value-or-mode-name>". The launcher routes
// these to a dedicated gpio-log event for the inspector UI.
void sim_log_gpio_mode(uint8_t pin, uint8_t mode);
void sim_log_gpio_write(uint8_t pin, uint8_t value);
void sim_log_gpio_pwm(uint8_t pin, int value);

// Forward declaration of an opaque LGFX_Device pointer — the caller passes
// their active panel. Returns 0 on success, non-zero on error.
//
// NOTE: declared as `void*` in the C-facing header so that consumers can
// pass any LovyanGFX device without dragging the full LovyanGFX include
// chain into C-only translation units. The implementation casts back.
int sim_screenshot(const char* path, void* lgfx_device);

// User code calls this once after creating its LGFX device to register it
// as the screenshot target. Pass NULL on shutdown to unregister.
void sim_set_active_display(void* lgfx_device);

// Returns the currently-registered active display (an lgfx::LGFX_Device*),
// or NULL if none. Used by the capture/mirror path.
void* sim_get_active_display(void);

#ifdef __cplusplus
}
#endif
