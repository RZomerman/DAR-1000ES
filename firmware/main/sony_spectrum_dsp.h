#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
  SONY_SPECTRUM_SIZE = 4096,
  SONY_SPECTRUM_BANDS = 11,
  SONY_SPECTRUM_COLUMNS = 22,
  SONY_SPECTRUM_RATE = 48000
};

typedef struct {
  float data[2 * SONY_SPECTRUM_SIZE];
  float window[SONY_SPECTRUM_SIZE];
  float twiddle[SONY_SPECTRUM_SIZE];
  float envelope[SONY_SPECTRUM_COLUMNS];
} sony_spectrum_dsp_t;

extern const unsigned sony_spectrum_edges[SONY_SPECTRUM_BANDS + 1];

void sony_spectrum_dsp_init(sony_spectrum_dsp_t *dsp);
void sony_spectrum_dsp_reset(sony_spectrum_dsp_t *dsp);
void sony_spectrum_analyze(sony_spectrum_dsp_t *dsp, const int16_t *stereo,
                           size_t first, uint8_t levels[SONY_SPECTRUM_COLUMNS]);
void sony_spectrum_frame(const uint8_t levels[SONY_SPECTRUM_COLUMNS],
                         uint64_t words[26]);
