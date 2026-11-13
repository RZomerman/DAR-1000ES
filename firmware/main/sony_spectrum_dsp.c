#include "sony_spectrum_dsp.h"

#include <math.h>

const unsigned sony_spectrum_edges[SONY_SPECTRUM_BANDS + 1] = {
    20, 37, 70, 132, 247, 462, 866, 1622, 3040, 5696, 10673, 20000};

void sony_spectrum_dsp_reset(sony_spectrum_dsp_t *dsp) {
  for (unsigned i = 0; i < SONY_SPECTRUM_COLUMNS; ++i)
    dsp->envelope[i] = -80;
}

void sony_spectrum_dsp_init(sony_spectrum_dsp_t *dsp) {
  const float tau = 6.283185307179586f;
  for (unsigned i = 0; i < SONY_SPECTRUM_SIZE; ++i)
    dsp->window[i] = 0.5f - 0.5f * cosf(tau * i / SONY_SPECTRUM_SIZE);
  for (unsigned i = 0; i < SONY_SPECTRUM_SIZE / 2; ++i) {
    dsp->twiddle[2 * i] = cosf(tau * i / SONY_SPECTRUM_SIZE);
    dsp->twiddle[2 * i + 1] = -sinf(tau * i / SONY_SPECTRUM_SIZE);
  }
  sony_spectrum_dsp_reset(dsp);
}

static void transform(sony_spectrum_dsp_t *dsp) {
  float *data = dsp->data;
  for (unsigned i = 1, j = 0; i < SONY_SPECTRUM_SIZE; ++i) {
    unsigned bit = SONY_SPECTRUM_SIZE / 2;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      float re = data[2 * i], im = data[2 * i + 1];
      data[2 * i] = data[2 * j];
      data[2 * i + 1] = data[2 * j + 1];
      data[2 * j] = re;
      data[2 * j + 1] = im;
    }
  }
  for (unsigned length = 2; length <= SONY_SPECTRUM_SIZE; length <<= 1) {
    unsigned half = length / 2, step = SONY_SPECTRUM_SIZE / length;
    for (unsigned base = 0; base < SONY_SPECTRUM_SIZE; base += length) {
      for (unsigned j = 0; j < half; ++j) {
        unsigned a = 2 * (base + j), b = 2 * (base + j + half);
        float wr = dsp->twiddle[2 * j * step];
        float wi = dsp->twiddle[2 * j * step + 1];
        float re = data[b] * wr - data[b + 1] * wi;
        float im = data[b] * wi + data[b + 1] * wr;
        data[b] = data[a] - re;
        data[b + 1] = data[a + 1] - im;
        data[a] += re;
        data[a + 1] += im;
      }
    }
  }
}

void sony_spectrum_analyze(sony_spectrum_dsp_t *dsp, const int16_t *stereo,
                           size_t first,
                           uint8_t levels[SONY_SPECTRUM_COLUMNS]) {
  for (unsigned i = 0; i < SONY_SPECTRUM_SIZE; ++i) {
    size_t index = (first + i) & (SONY_SPECTRUM_SIZE - 1);
    float scale = dsp->window[i] / 32768.0f;
    dsp->data[2 * i] = stereo[2 * index] * scale;
    dsp->data[2 * i + 1] = stereo[2 * index + 1] * scale;
  }
  transform(dsp);
  float power[SONY_SPECTRUM_COLUMNS] = {0};
  unsigned band = 0;
  for (unsigned k = 1; k < SONY_SPECTRUM_SIZE / 2; ++k) {
    float hz = (float)k * SONY_SPECTRUM_RATE / SONY_SPECTRUM_SIZE;
    while (band < SONY_SPECTRUM_BANDS && hz >= sony_spectrum_edges[band + 1])
      ++band;
    if (band == SONY_SPECTRUM_BANDS)
      break;
    if (hz < sony_spectrum_edges[0])
      continue;
    unsigned a = 2 * k, b = 2 * (SONY_SPECTRUM_SIZE - k);
    /* One complex FFT packs two real channels; separate their conjugates. */
    float lr = (dsp->data[a] + dsp->data[b]) * 0.5f;
    float li = (dsp->data[a + 1] - dsp->data[b + 1]) * 0.5f;
    float rr = (dsp->data[a + 1] + dsp->data[b + 1]) * 0.5f;
    float ri = (dsp->data[b] - dsp->data[a]) * 0.5f;
    power[band] += lr * lr + li * li;
    power[SONY_SPECTRUM_BANDS + band] += rr * rr + ri * ri;
  }
  /* One-sided power, corrected for the periodic Hann window's 3/8 gain. */
  const float normalization =
      16.0f / (3.0f * SONY_SPECTRUM_SIZE * SONY_SPECTRUM_SIZE);
  for (unsigned i = 0; i < SONY_SPECTRUM_COLUMNS; ++i) {
    float db = 10 * log10f(fmaxf(power[i] * normalization, 1e-12f));
    float previous = dsp->envelope[i];
    dsp->envelope[i] = db > previous ? db : fmaxf(db, previous - 8);
    float height = ceilf((dsp->envelope[i] + 60) * 7 / 60);
    levels[i] = height <= 0 ? 0 : height >= 7 ? 7 : (uint8_t)height;
  }
}

void sony_spectrum_frame(const uint8_t levels[SONY_SPECTRUM_COLUMNS],
                         uint64_t words[26]) {
  for (unsigned i = 0; i < SONY_SPECTRUM_COLUMNS; ++i) {
    unsigned rows = levels[i] > 7 ? 7 : levels[i];
    words[22 - i] = ((1ULL << (rows * 5)) - 1) << ((7 - rows) * 5);
  }
}
