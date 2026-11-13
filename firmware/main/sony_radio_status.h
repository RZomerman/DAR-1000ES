#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint64_t meter;
  uint64_t audio;
  uint64_t stereo;
} sony_radio_status_t;

sony_radio_status_t sony_radio_status(bool playing, unsigned channels,
                                      const char *bitrate, const char *genre);
void sony_radio_status_apply(uint64_t words[26],
                             const sony_radio_status_t *status);
void sony_radio_status_apply_view(uint64_t words[26],
                                  const sony_radio_status_t *status,
                                  bool spectrum);
