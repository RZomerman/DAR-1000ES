#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

size_t sony_display_waveform(uint8_t *wave, size_t capacity,
                             const uint64_t words[26]);
size_t sony_display_waveform_brightness(uint8_t *wave, size_t capacity,
                                        const uint64_t words[26], bool dim);
