#include "sony_display_frame.h"

#define DISPLAY_SLOT_COUNT   (8 * 26)
#define DISPLAY_SLOT_SAMPLES 1230

size_t sony_display_waveform(uint8_t *wave, size_t capacity,
                             const uint64_t words[26]) {
  return sony_display_waveform_brightness(wave, capacity, words, true);
}

size_t sony_display_waveform_brightness(uint8_t *wave, size_t capacity,
                                        const uint64_t words[26], bool dim) {
  if (!wave || !words)
    return 0;
  size_t samples = 0;
  unsigned latches = 0;
  for (size_t i = 1; i < capacity; ++i) {
    if (!(wave[i - 1] & 8) && (wave[i] & 8) && ++latches == 8 * 26) {
      samples = i;
      break;
    }
  }
  if (!samples)
    return 0;
  unsigned clocks = 0;
  uint8_t data = 0;
  size_t latch = 0;
  for (size_t i = 0; i < samples; ++i) {
    uint8_t state = wave[i];
    if (i && !(wave[i - 1] & 8) && (state & 8))
      latch = i;
    if (i && (wave[i - 1] & 1) && !(state & 1)) {
      uint64_t word = words[(clocks / 40 + 1) % 26];
      data = ((word >> (39 - clocks % 40)) & 1) << 1;
      ++clocks;
    }
    /* Scale the measured 201 us dim interval with the slot timing below. */
    if (dim && i - latch < 1608)
      state &= ~0x04;
    wave[i] = (state & ~0x02) | data;
  }
  if (clocks != DISPLAY_SLOT_COUNT * 40)
    return 0;

  size_t source_start = 0;
  size_t output_samples = 0;
  for (unsigned slot = 0; slot < DISPLAY_SLOT_COUNT; ++slot) {
    size_t source_end = source_start + 1;
    if (slot + 1 == DISPLAY_SLOT_COUNT) {
      source_end = samples;
    } else {
      while (source_end < samples &&
             ((wave[source_end - 1] & 8) || !(wave[source_end] & 8)))
        ++source_end;
      if (source_end == samples)
        return 0;
    }

    size_t source_length = source_end - source_start;
    if (source_length <= DISPLAY_SLOT_SAMPLES)
      return 0;
    for (size_t sample = 0; sample < DISPLAY_SLOT_SAMPLES; ++sample) {
      size_t source_sample =
          source_start + sample * source_length / DISPLAY_SLOT_SAMPLES;
      wave[output_samples + sample] = wave[source_sample];
    }
    output_samples += DISPLAY_SLOT_SAMPLES;
    source_start = source_end;
  }
  return output_samples;
}
