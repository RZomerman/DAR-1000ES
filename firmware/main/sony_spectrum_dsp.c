#include "sony_spectrum_dsp.h"

#include <math.h>
#include <string.h>

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

void sony_vu_reset(sony_vu_state_t *state) {
  memset(state, 0, sizeof(*state));
}

bool sony_vu_analyze(sony_vu_state_t *state, const int16_t *stereo, size_t frames,
                     uint32_t rms[2], uint32_t peak[2], uint32_t held[2]) {
  bool updated = false;
  for (size_t i = 0; i < frames; ++i) {
    for (unsigned channel = 0; channel < 2; ++channel) {
      int32_t sample = stereo[2 * i + channel];
      uint32_t magnitude = sample < 0 ? (uint32_t)-sample : (uint32_t)sample;
      state->pending[channel] += (uint64_t)magnitude * magnitude;
      if (magnitude > state->peak[channel]) state->peak[channel] = magnitude;
    }
    if (++state->frames == SONY_SPECTRUM_RATE / 10) {
      if (state->count < 3) ++state->count;
      for (unsigned channel = 0; channel < 2; ++channel) {
        state->totals[channel] -= state->buckets[state->cursor][channel];
        state->buckets[state->cursor][channel] = state->pending[channel];
        state->totals[channel] += state->pending[channel];
        state->pending[channel] = 0;
        rms[channel] = (uint32_t)sqrtf((float)state->totals[channel] /
                                      (state->count * (SONY_SPECTRUM_RATE / 10)));
        peak[channel] = state->peak[channel];
        state->peak[channel] = 0;
        if (rms[channel] >= state->held[channel]) {
          state->held[channel] = rms[channel];
          state->hold_ticks[channel] = 3;
        } else if (!state->hold_ticks[channel] ||
                   --state->hold_ticks[channel] == 0) {
          state->held[channel] = state->held[channel] * 794 / 1000;
          if (state->held[channel] < rms[channel])
            state->held[channel] = rms[channel];
        }
        held[channel] = state->held[channel];
      }
      state->cursor = (state->cursor + 1) % 3;
      state->frames = 0;
      updated = true;
    }
  }
  return updated;
}

void sony_vu_frame(const uint32_t rms[2], const uint32_t peak[2],
                   const uint32_t held[2], bool center, uint64_t words[26]) {
  static const uint16_t thresholds[] = {
      292, 368, 463, 583, 734, 924, 1163, 1464, 1843, 2320,
      2920, 3677, 4629, 5827, 7336, 9235, 11627, 14637, 18427, 23198,
      23199, 27571};
  unsigned level[2] = {0, 0}, marker[2] = {0, 0};
  for (unsigned channel = 0; channel < 2; ++channel) {
    while (level[channel] < 22 && rms[channel] >= thresholds[level[channel]])
      ++level[channel];
    while (marker[channel] < 22 && held[channel] >= thresholds[marker[channel]])
      ++marker[channel];
  }
  words[24] &= ~0x51c0ULL;
  if (center) {
    for (unsigned channel = 0; channel < 2; ++channel) {
      unsigned normal = level[channel] > 20 ? 20 : level[channel];
      unsigned count = (normal * 11 + 19) / 20;
      unsigned peak_level = marker[channel] > 20 ? 20 : marker[channel];
      unsigned peak_count = (peak_level * 11 + 19) / 20;
      for (unsigned i = 0; i < 11; ++i) {
        unsigned slot = channel ? 11 - i : 12 + i;
        uint64_t dots = i < count ? 31 :
                        peak_count && i + 1 == peak_count ? 24 : 0;
        words[slot] = 0;
        for (unsigned row = 0; row < 7; ++row)
          words[slot] |= dots << (row * 5);
      }
      if (peak[channel] > 23198)
        words[24] |= channel ? 0x1000 : 0x4000;
    }
  } else {
    for (unsigned i = 0; i < 22; ++i) {
      uint64_t top = i < level[0] ? 31 :
                     marker[0] && i + 1 == marker[0] ? 24 : 0;
      uint64_t bottom = i < level[1] ? 31 :
                        marker[1] && i + 1 == marker[1] ? 24 : 0;
      words[22 - i] = 0;
      for (unsigned row = 0; row < 3; ++row)
        words[22 - i] |= (top << (row * 5)) |
                         (bottom << ((row + 4) * 5));
    }
    if (peak[0] > 23198) words[24] |= 0x80;
    if (peak[1] > 23198) words[24] |= 0x40;
  }
}

void sony_search_frame(int64_t elapsed_us, uint64_t words[26]) {
  static const unsigned grids[] = {2, 6, 10, 12, 13, 14};
  for (unsigned slot = 1; slot <= 22; ++slot)
    words[slot] = 0;
  uint32_t phase = (uint32_t)((elapsed_us < 0 ? 0 : elapsed_us) % 1500000);
  unsigned step = phase < 300000 ? 0 : phase < 600000 ? 1 :
                  phase < 1000000 ? 2 + (phase - 600000) / 100000 : 6;
  if (step < 6)
    words[grids[step]] = 0x7ffffffffULL;
}
