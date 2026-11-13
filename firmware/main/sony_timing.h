#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SONY_TIMING_CAPACITY   524288U
#define SONY_TIMING_TICK_NS    125U
#define SONY_TIMING_BLANK_SLOT 26U

typedef struct {
  uint32_t setup_ns;
  uint32_t clock_low_ns;
  uint32_t clock_high_ns;
  uint32_t strobe_ns;
  uint32_t latch_guard_ns;
  uint32_t blank_before_ns;
  uint32_t blank_after_ns;
  uint32_t on_ns;
  uint32_t off_ns;
  uint32_t pulses;
  uint32_t hold;
  uint32_t slot;
  uint32_t pair;
  uint32_t alternate_ms;
  uint32_t alternate_blank_steps;
} sony_timing_t;

typedef struct {
  size_t slot_samples;
  size_t cycle_samples;
  size_t bytes;
  unsigned cycles;
} sony_timing_info_t;

sony_timing_t sony_timing_defaults(void);
const char *sony_timing_validate(const sony_timing_t *timing);
sony_timing_info_t sony_timing_info(const sony_timing_t *timing);
unsigned sony_timing_held_slot(const sony_timing_t *timing,
                               uint64_t elapsed_us);
/* Physical six-line states, with the same bit order as the capture fixtures. */
size_t sony_timing_generate(uint8_t *wave, size_t capacity,
                            const sony_timing_t *timing,
                            const uint64_t words[26], bool initialize);
/* Patch only changed SDATA spans in a completed, same-timing scan buffer. */
bool sony_timing_patch_data(uint8_t *wave, size_t bytes,
                            const sony_timing_t *timing,
                            const uint64_t before[26],
                            const uint64_t after[26], bool initialize);
/* DMA lane image for held mode: only bit 2 (CLEAR) may change. */
size_t sony_timing_generate_clear(uint8_t *wave, size_t capacity,
                                  const sony_timing_t *timing);
