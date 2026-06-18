#pragma once
#include <stddef.h>
#include <stdint.h>

// Mirror-facing entry points into the SDL audio backend (sim_audio.cpp). The
// LEDC tone shims (boardghost_ledc_*) live elsewhere; these two exist so the
// /mirror/audio HTTP stream can tap the mixed output.

#ifdef __cplusplus
extern "C" {
#endif

// Open the audio device up front (idempotent) so the mirror stream gets a
// steady sample cadence even before the sketch plays its first tone. No-op
// when BOARDGHOST_SOUND=off.
void boardghost_audio_ensure_started(void);

// Drain up to `max` mixed S16 mono samples (44.1 kHz) from the tap ring into
// dst. Returns the count copied (0 when empty or audio is disabled). Single
// consumer only.
size_t boardghost_audio_drain(int16_t* dst, size_t max);

#ifdef __cplusplus
}
#endif
