#include "sony_timing.h"

#include <string.h>

enum { SCLK = 1, SDATA = 2, CLEAR = 4, STB = 8, GDATA = 16, GCLK = 32 };

sony_timing_t sony_timing_defaults(void) {
  return (sony_timing_t){.setup_ns = 250,
                         .clock_low_ns = 500,
                         .clock_high_ns = 500,
                         .strobe_ns = 3000,
                         .latch_guard_ns = 2000,
                         .blank_before_ns = 2000,
                         .blank_after_ns = 2000,
                         .on_ns = 50000,
                         .off_ns = 190750,
                         .pulses = 1};
}

static size_t ticks(uint32_t ns) {
  return ns / SONY_TIMING_TICK_NS;
}

static size_t shift_samples(const sony_timing_t *t) {
  return ticks(t->setup_ns + t->clock_low_ns + t->clock_high_ns);
}

sony_timing_info_t sony_timing_info(const sony_timing_t *t) {
  size_t latch = ticks(t->latch_guard_ns + t->strobe_ns + t->blank_after_ns);
  size_t transfer = ticks(t->blank_before_ns) +
                    (t->pair ? 80 : 41) * shift_samples(t) + latch;
  size_t pulse = ticks(t->on_ns + t->off_ns);
  sony_timing_info_t info = {0};
  info.slot_samples = transfer + t->pulses * pulse;
  info.cycle_samples = t->hold ? pulse : (t->pair ? 2 : 26) * info.slot_samples;
  if (!info.cycle_samples)
    return info;
  size_t prefix = ticks(t->blank_before_ns) + 80 * shift_samples(t) + latch;
  /* Full scans need enough queued time to survive audio/resampler bursts. */
  size_t budget = t->hold || t->pair ? 80000 : SONY_TIMING_CAPACITY - prefix - 63;
  info.cycles = (unsigned)(budget / info.cycle_samples);
  if (!info.cycles)
    info.cycles = 1;
  if (info.cycles > 8)
    info.cycles = 8;
  if (t->hold && t->alternate_ms)
    info.cycles = 1;
  info.bytes = (prefix + info.cycles * info.cycle_samples + 63) & ~(size_t)63;
  return info;
}

const char *sony_timing_validate(const sony_timing_t *t) {
  if (!t)
    return "Missing timing";
  const uint32_t short_times[] = {
      t->setup_ns,       t->clock_low_ns,    t->clock_high_ns, t->strobe_ns,
      t->latch_guard_ns, t->blank_before_ns, t->blank_after_ns};
  for (size_t i = 0; i < sizeof(short_times) / sizeof(short_times[0]); ++i)
    if (short_times[i] < 250 || short_times[i] > 20000 ||
        short_times[i] % SONY_TIMING_TICK_NS)
      return "Edge times must be 250..20000 ns, in 125 ns steps";
  if (t->blank_before_ns < 2000 || t->blank_after_ns < 2000)
    return "Blank guards must be at least 2000 ns (output propagation margin)";
  if (t->on_ns < 250 || t->on_ns > 280000 || t->off_ns < 2000 ||
      t->off_ns > 5000000 || t->on_ns % SONY_TIMING_TICK_NS ||
      t->off_ns % SONY_TIMING_TICK_NS)
    return "ON must be 250..280000 ns; OFF 2000..5000000 ns; 125 ns steps";
  if (!t->pulses || t->pulses > 16 || t->hold > 1 || t->slot > 25)
    return "Pulses must be 1..16, hold 0/1 and slot 0..25";
  if (t->pair > 1 || (t->pair && (t->hold || !t->slot)))
    return "Pair must be 0/1; pair scan needs hold=0 and slot=1..25";
  if (t->alternate_ms &&
      (!t->hold || t->pair || t->alternate_ms < 5 || t->alternate_ms > 1000))
    return "Alternation needs hold=1, pair=0, slot=1..25 and 5..1000 ms dwell";
  if (t->alternate_blank_steps > 20 ||
      (t->alternate_blank_steps && !t->alternate_ms))
    return "Blank steps need local alternation; use 0..20 steps";
  if (t->pair) {
    for (size_t i = 0; i < sizeof(short_times) / sizeof(short_times[0]); ++i)
      if (short_times[i] % 1000)
        return "GPIO pair edge times must be whole microseconds";
    if (t->on_ns % 1000 || t->off_ns % 1000 || t->on_ns > 50000)
      return "GPIO pair ON/OFF must be whole microseconds; ON <= 50000 ns";
  }
  if (t->hold && (uint64_t)t->on_ns * 25 > t->off_ns)
    return "Held-grid duty must not exceed 1/26; OFF must be >= 25 x ON";
  if (t->pair && (uint64_t)t->pulses * t->on_ns * 26 >
                     sony_timing_info(t).cycle_samples * SONY_TIMING_TICK_NS)
    return "Pair scan duty per grid must not exceed 1/26";
  if (sony_timing_info(t).bytes > SONY_TIMING_CAPACITY)
    return "Frame exceeds 65 ms DMA capacity; reduce times or pulse count";
  return NULL;
}

unsigned sony_timing_held_slot(const sony_timing_t *t, uint64_t elapsed_us) {
  if (!t->hold || !t->alternate_ms || !t->slot)
    return t->slot;
  unsigned phase = (unsigned)((elapsed_us / (t->alternate_ms * 1000ULL)) %
                              (2ULL + t->alternate_blank_steps));
  return phase < 2 ? t->slot - phase : SONY_TIMING_BLANK_SLOT;
}

typedef struct {
  uint8_t *wave;
  size_t pos;
  uint8_t state;
} writer_t;

static void emit(writer_t *w, size_t count) {
  memset(w->wave + w->pos, w->state, count);
  w->pos += count;
}

static void shift(writer_t *w, const sony_timing_t *t, uint8_t clocks,
                  bool segment, bool grid) {
  w->state = (w->state & ~(SDATA | GDATA)) | (segment ? SDATA : 0) |
             (grid ? GDATA : 0);
  emit(w, ticks(t->setup_ns));
  w->state &= ~clocks;
  emit(w, ticks(t->clock_low_ns));
  w->state |= clocks;
  emit(w, ticks(t->clock_high_ns));
}

static void latch(writer_t *w, const sony_timing_t *t) {
  emit(w, ticks(t->latch_guard_ns));
  w->state |= STB;
  emit(w, ticks(t->strobe_ns));
  w->state &= ~STB;
  emit(w, ticks(t->blank_after_ns));
}

static void pwm(writer_t *w, const sony_timing_t *t) {
  w->state |= CLEAR;
  emit(w, ticks(t->on_ns));
  w->state &= ~CLEAR;
  emit(w, ticks(t->off_ns));
}

size_t sony_timing_generate(uint8_t *wave, size_t capacity,
                            const sony_timing_t *t, const uint64_t words[26],
                            bool initialize) {
  if (!wave || !words || sony_timing_validate(t))
    return 0;
  sony_timing_info_t info = sony_timing_info(t);
  if (capacity < info.bytes)
    return 0;
  writer_t w = {.wave = wave, .state = SCLK | GCLK};
  if (initialize) {
    emit(&w, ticks(t->blank_before_ns));
    for (int bit = 39; bit >= 0; --bit)
      shift(&w, t, SCLK, t->hold && ((words[t->slot] >> bit) & 1), false);
    /* Simultaneous clock edges caused CLEAR/DATA spikes at the translator.
     * Initialize the registers separately, still blanked with the latch held.
     */
    for (int bit = 39; bit >= 0; --bit)
      shift(&w, t, GCLK, false, t->hold && (unsigned)bit == t->slot);
    latch(&w, t);
  }
  for (unsigned cycle = 0; cycle < info.cycles; ++cycle) {
    if (t->hold) {
      pwm(&w, t);
      continue;
    }
    for (unsigned index = 0; index < (t->pair ? 2U : 26U); ++index) {
      unsigned slot = t->pair ? t->slot - index : index;
      emit(&w, ticks(t->blank_before_ns));
      for (int bit = 39; bit >= 0; --bit)
        shift(&w, t, SCLK, (words[slot] >> bit) & 1, false);
      if (t->pair) {
        for (int bit = 39; bit >= 0; --bit)
          shift(&w, t, GCLK, false, (unsigned)bit == slot);
      } else {
        shift(&w, t, GCLK, false, slot == 0);
      }
      latch(&w, t);
      for (unsigned pulse = 0; pulse < t->pulses; ++pulse)
        pwm(&w, t);
    }
  }
  /* Always pad blanked, with latch held and clocks HIGH. */
  size_t bytes = (w.pos + 63) & ~(size_t)63;
  w.state = SCLK | GCLK;
  emit(&w, bytes - w.pos);
  return bytes;
}

bool sony_timing_patch_data(uint8_t *wave, size_t bytes,
                            const sony_timing_t *t,
                            const uint64_t before[26],
                            const uint64_t after[26], bool initialize) {
  if (!wave || !before || !after || sony_timing_validate(t) || t->hold)
    return false;
  sony_timing_info_t info = sony_timing_info(t);
  size_t shift = shift_samples(t);
  size_t prefix = initialize
                      ? ticks(t->blank_before_ns + t->latch_guard_ns +
                              t->strobe_ns + t->blank_after_ns) + 80 * shift
                      : 0;
  size_t needed = (prefix + info.cycles * info.cycle_samples + 63) & ~(size_t)63;
  if (bytes < needed)
    return false;
  for (unsigned cycle = 0; cycle < info.cycles; ++cycle)
    for (unsigned index = 0; index < (t->pair ? 2U : 26U); ++index) {
      unsigned slot = t->pair ? t->slot - index : index;
      uint64_t changed = before[slot] ^ after[slot];
      size_t start = prefix + cycle * info.cycle_samples +
                     index * info.slot_samples + ticks(t->blank_before_ns);
      for (unsigned bit = 0; bit < 40; ++bit)
        if (changed & (1ULL << (39 - bit)))
          for (size_t sample = 0; sample < shift; ++sample)
            wave[start + bit * shift + sample] ^= SDATA;
    }
  return true;
}

size_t sony_timing_generate_clear(uint8_t *wave, size_t capacity,
                                  const sony_timing_t *t) {
  if (!wave || sony_timing_validate(t) || !t->hold)
    return 0;
  sony_timing_info_t info = sony_timing_info(t);
  size_t samples = info.cycles * info.cycle_samples;
  size_t bytes = (samples + 63) & ~(size_t)63;
  if (capacity < bytes)
    return 0;
  writer_t w = {.wave = wave};
  for (unsigned cycle = 0; cycle < info.cycles; ++cycle)
    pwm(&w, t);
  emit(&w, bytes - w.pos);
  return bytes;
}
