#pragma once

#include <stddef.h>
#include <stdbool.h>
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

typedef struct {
  uint64_t buckets[3][2], totals[2], pending[2];
  uint32_t peak[2], held[2];
  unsigned hold_ticks[2];
  unsigned frames, cursor, count;
} sony_vu_state_t;

extern const unsigned sony_spectrum_edges[SONY_SPECTRUM_BANDS + 1];

void sony_spectrum_dsp_init(sony_spectrum_dsp_t *dsp);
void sony_spectrum_dsp_reset(sony_spectrum_dsp_t *dsp);
void sony_spectrum_analyze(sony_spectrum_dsp_t *dsp, const int16_t *stereo,
                           size_t first, uint8_t levels[SONY_SPECTRUM_COLUMNS]);
void sony_spectrum_frame(const uint8_t levels[SONY_SPECTRUM_COLUMNS],
                         uint64_t words[26]);
void sony_vu_reset(sony_vu_state_t *state);
bool sony_vu_analyze(sony_vu_state_t *state, const int16_t *stereo, size_t frames,
                     uint32_t rms[2], uint32_t peak[2], uint32_t held[2]);
void sony_vu_frame(const uint32_t rms[2], const uint32_t peak[2],
                   const uint32_t held[2], bool center, uint64_t words[26]);
void sony_search_frame(int64_t elapsed_us, uint64_t words[26]);
