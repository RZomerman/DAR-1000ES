#include "../main/sony_spectrum_dsp.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void tone(int16_t *pcm, unsigned left, unsigned right, float left_gain,
                 float right_gain) {
  for (unsigned i = 0; i < SONY_SPECTRUM_SIZE; ++i) {
    float phase = 6.283185307179586f * i / SONY_SPECTRUM_SIZE;
    pcm[2 * i] = (int16_t)(32767 * left_gain * sinf(phase * left));
    pcm[2 * i + 1] = (int16_t)(32767 * right_gain * sinf(phase * right));
  }
}

int main(void) {
  sony_spectrum_dsp_t *dsp = malloc(sizeof(*dsp));
  int16_t *pcm = calloc(SONY_SPECTRUM_SIZE * 2, sizeof(*pcm));
  assert(dsp && pcm);
  sony_spectrum_dsp_init(dsp);
  uint8_t levels[22];
  sony_spectrum_analyze(dsp, pcm, 0, levels);
  for (unsigned i = 0; i < 22; ++i)
    assert(!levels[i]);

  tone(pcm, 85, 683, 0.8f, 0.125f);
  sony_spectrum_analyze(dsp, pcm, 0, levels);
  assert(levels[6] == 7 && levels[11 + 9] == 5);
  for (unsigned i = 0; i < 22; ++i)
    if (i != 6 && i != 20)
      assert(!levels[i]);
  sony_spectrum_dsp_reset(dsp);
  sony_spectrum_analyze(dsp, pcm, 317, levels);
  assert(levels[6] == 7 && levels[20] == 5);
  tone(pcm, 683, 85, 0.125f, 0.8f);
  sony_spectrum_dsp_reset(dsp);
  sony_spectrum_analyze(dsp, pcm, 0, levels);
  assert(levels[9] == 5 && levels[17] == 7);
  for (unsigned i = 0; i < 22; ++i)
    if (i != 9 && i != 17)
      assert(!levels[i]);

  for (unsigned band = 0; band < 11; ++band) {
    float hz =
        sqrtf((float)sony_spectrum_edges[band] * sony_spectrum_edges[band + 1]);
    unsigned bin =
        (unsigned)roundf(hz * SONY_SPECTRUM_SIZE / SONY_SPECTRUM_RATE);
    tone(pcm, bin, 0, 0.8f, 0);
    sony_spectrum_dsp_reset(dsp);
    sony_spectrum_analyze(dsp, pcm, 0, levels);
    assert(levels[band] >= 6);
    for (unsigned i = 0; i < 11; ++i) {
      assert(levels[i] <= levels[band]);
      assert(!levels[11 + i]);
    }
  }

  tone(pcm, 85, 85, 0.5f, 0.5f);
  sony_spectrum_dsp_reset(dsp);
  sony_spectrum_analyze(dsp, pcm, 0, levels);
  for (unsigned i = 0; i < 11; ++i)
    assert(levels[i] == levels[11 + i]);
  assert(levels[6] == 6);
  memset(pcm, 0, SONY_SPECTRUM_SIZE * 4);
  sony_spectrum_analyze(dsp, pcm, 0, levels);
  sony_spectrum_analyze(dsp, pcm, 0, levels);
  assert(levels[6] > 0 && levels[6] < 6);
  for (unsigned frame = 0; frame < 10; ++frame)
    sony_spectrum_analyze(dsp, pcm, 0, levels);
  for (unsigned i = 0; i < 22; ++i)
    assert(!levels[i]);
  for (unsigned i = 0; i < SONY_SPECTRUM_SIZE * 2; ++i)
    pcm[i] = 10000;
  sony_spectrum_dsp_reset(dsp);
  sony_spectrum_analyze(dsp, pcm, 0, levels);
  for (unsigned i = 0; i < 22; ++i)
    assert(!levels[i]);

  uint64_t words[26];
  for (unsigned i = 0; i < 26; ++i)
    words[i] = 0xABC;
  memset(levels, 0, sizeof(levels));
  levels[1] = 1;
  levels[21] = 8;
  sony_spectrum_frame(levels, words);
  assert(words[22] == 0 && words[21] == 0x7C0000000ULL);
  assert(words[1] == 0x7FFFFFFFFULL);
  assert(words[0] == 0xABC && words[23] == 0xABC && words[24] == 0xABC &&
         words[25] == 0xABC);
  free(dsp);
  free(pcm);
  return 0;
}
