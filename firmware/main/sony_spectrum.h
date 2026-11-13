#pragma once

#include "esp_err.h"
#include "sony_spectrum_dsp.h"
#include <stdbool.h>

typedef struct {
  bool initialized;
  bool enabled;
  uint32_t updates;
  uint32_t dropped_frames;
  uint32_t last_fft_us;
  uint8_t levels[SONY_SPECTRUM_COLUMNS];
} sony_spectrum_status_t;

esp_err_t sony_spectrum_init(void);
void sony_spectrum_enable(bool enabled);
/* Playback-task tap: never waits for space or performs FFT work. */
void sony_spectrum_write(const int16_t *stereo, size_t frames,
                         uint32_t generation);
void sony_spectrum_get_status(sony_spectrum_status_t *status);
