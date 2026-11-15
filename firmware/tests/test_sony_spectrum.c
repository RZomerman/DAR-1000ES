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
  memset(words, 0, sizeof(words));
  uint32_t rms[] = {23198, 0}, peak[] = {23198, 0}, held[] = {0, 0};
  sony_vu_frame(rms, peak, held, false, words);
  for (unsigned i = 0; i < 22; ++i)
    assert(words[22 - i] == (i < 20 ? 0x7fff : 0));
  assert(!(words[24] & 0x51c0));
  rms[0] = peak[0] = 23199;
  rms[1] = peak[1] = 27571;
  held[0] = peak[0]; held[1] = peak[1];
  sony_vu_frame(rms, peak, held, false, words);
  assert(words[2] == (0x7fffULL | (0x7fffULL << 20)));
  assert(words[1] == (0x7fffULL << 20));
  assert((words[24] & 0x1c0) == 0xc0);
  sony_vu_frame(rms, peak, held, true, words);
  for (unsigned i = 1; i <= 22; ++i)
    assert(words[i] == 0x7ffffffffULL);
  assert((words[24] & 0x51c0) == 0x5000);
  rms[0] = peak[0] = held[0] = 0;
  rms[1] = peak[1] = held[1] = 23198;
  sony_vu_frame(rms, peak, held, true, words);
  for (unsigned i = 12; i <= 22; ++i) assert(!words[i]);
  for (unsigned i = 1; i <= 11; ++i)
    assert(words[i] == 0x7ffffffffULL);
  assert(!(words[24] & 0x51c0));
  rms[0] = 1000;
  peak[0] = held[0] = 30000;
  sony_vu_frame(rms, peak, held, false, words);
  assert((words[22] & 0x7fff) == 0x7fff);
  assert((words[1] & 0x7fff) == 0x6318);
  assert((words[24] & 0x1c0) == 0x80);
  sony_vu_frame(rms, peak, held, true, words);
  uint64_t marker_dots = 0;
  for (unsigned row = 0; row < 7; ++row)
    marker_dots |= 24ULL << (row * 5);
  assert(words[12] == 0x7ffffffffULL && words[22] == marker_dots);
  sony_vu_state_t vu;
  sony_vu_reset(&vu);
  int16_t *samples = calloc(4800 * 2, sizeof(*samples));
  assert(samples);
  for (unsigned i = 0; i < 4800; ++i) {
    samples[2 * i] = 1000;
    samples[2 * i + 1] = -2000;
  }
  assert(!sony_vu_analyze(&vu, samples, 4799, rms, peak, held));
  assert(sony_vu_analyze(&vu, samples + 4799 * 2, 1, rms, peak, held));
  assert(rms[0] == 1000 && rms[1] == 2000 &&
         peak[0] == 1000 && peak[1] == 2000);
  assert(sony_vu_analyze(&vu, samples, 4800, rms, peak, held));
  samples[0] = 32767;
  samples[1] = -32768;
  assert(sony_vu_analyze(&vu, samples, 4800, rms, peak, held));
  assert(rms[0] > 1000 && rms[0] < 1100 && rms[1] > 2000 && rms[1] < 2100);
  assert(peak[0] == 32767 && peak[1] == 32768);
  sony_vu_reset(&vu);
  for (unsigned i = 0; i < 4800; ++i) {
    samples[2 * i] = 12000;
    samples[2 * i + 1] = 6000;
  }
  for (unsigned i = 0; i < 3; ++i)
    assert(sony_vu_analyze(&vu, samples, 4800, rms, peak, held));
  assert(held[0] == 12000 && held[1] == 6000);
  for (unsigned i = 0; i < 4800; ++i) samples[2 * i] = 0;
  for (unsigned i = 0; i < 2; ++i) {
    assert(sony_vu_analyze(&vu, samples, 4800, rms, peak, held));
    assert(held[0] == 12000 && held[1] == 6000);
  }
  assert(sony_vu_analyze(&vu, samples, 4800, rms, peak, held));
  assert(held[0] == 9528 && held[1] == 6000 && rms[0] == 0);
  assert(sony_vu_analyze(&vu, samples, 4800, rms, peak, held));
  assert(held[0] == 7565 && held[1] == 6000);
  memset(samples, 0, 4800 * 2 * sizeof(*samples));
  for (unsigned i = 0; i < 100; ++i) {
    assert(sony_vu_analyze(&vu, samples, 4800, rms, peak, held));
    assert(held[0] >= rms[0] && held[1] >= rms[1]);
  }
  assert(!held[0] && !held[1]);
  free(samples);
  const int64_t times[] = {0, 299999, 300000, 599999, 600000, 700000,
                           800000, 900000, 1000000, 1499999, 1500000};
  const unsigned slots[] = {2, 2, 6, 6, 10, 12, 13, 14, 0, 0, 2};
  words[0] = 0x123;
  words[24] = 0x456;
  for (unsigned i = 0; i < sizeof(times) / sizeof(times[0]); ++i) {
    sony_search_frame(times[i], words);
    for (unsigned slot = 1; slot <= 22; ++slot)
      assert(words[slot] == (slot == slots[i] ? 0x7ffffffffULL : 0));
    assert(words[0] == 0x123 && words[24] == 0x456);
  }
  free(dsp);
  free(pcm);
  return 0;
}
