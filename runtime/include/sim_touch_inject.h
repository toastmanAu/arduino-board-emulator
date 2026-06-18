#pragma once

// Runtime touch injection — the act half of the agentic loop. The mirror's
// POST /mirror/touch enqueues a tap here; Panel_sdl_bg::getTouchRaw consumes it
// on the sketch thread and emits a short press, exactly as if a finger touched
// the panel. Reuses the same rotation/de-rotation the scripted-touch and
// live-mouse paths use, so a screen-space tap lands where the agent aimed.

#ifdef __cplusplus
extern "C" {
#endif

// Enqueue a tap. screen_space != 0 → coords are final screen pixels
// (de-rotated for the panel so lcd.getTouch() sees them unchanged); 0 → coords
// are raw, pre-rotation (the escape hatch, like BOARDGHOST_SIM_TOUCHES).
// Thread-safe: called from the HTTP thread, consumed on the sketch thread.
void boardghost_inject_touch(int x, int y, int screen_space);

#ifdef __cplusplus
}

// Consumer side (sketch thread). Returns true and fills the out-params while an
// injected tap is within its press window; returns false once it expires.
bool boardghost_pending_touch(int& x, int& y, bool& screen_space);
#endif
