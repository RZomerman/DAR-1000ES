#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "sony_timing.h"

typedef struct sony_scan_engine sony_scan_engine_t;
esp_err_t sony_scan_open(sony_scan_engine_t **engine);
esp_err_t sony_scan_open_timed(sony_scan_engine_t **engine, bool held);
/* Hold CLEAR LOW, drain queued PWM and force a fresh preload on resume. */
esp_err_t sony_scan_blank(sony_scan_engine_t *engine);
esp_err_t sony_scan_submit_timed(sony_scan_engine_t *engine,
                                 const uint64_t words[26],
                                 const sony_timing_t *timing);
esp_err_t sony_scan_submit(sony_scan_engine_t *engine, const uint64_t words[26],
                           bool dim);
esp_err_t sony_scan_close(sony_scan_engine_t *engine);

esp_err_t sony_capture_replay(bool frequency, bool mono_test,
                              uint8_t *final_state, uint32_t *frames);
esp_err_t sony_display_chunk(const uint64_t words[26], uint8_t *final_state,
                             uint32_t *frames);
esp_err_t sony_display_chunk_brightness(const uint64_t words[26], bool dim,
                                        uint8_t *final_state, uint32_t *frames);
