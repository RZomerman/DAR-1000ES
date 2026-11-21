#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint64_t meter;
  uint64_t audio;
  uint64_t stereo;
  bool searching, playing;
  unsigned preset;
  uint32_t generation;
} sony_radio_status_t;

sony_radio_status_t sony_radio_status(bool playing, unsigned channels,
                                      const char *bitrate, const char *genre);
unsigned sony_radio_genre_labels(const char *genre);
void sony_radio_controller_indicators(uint64_t words[26],
                                     const sony_radio_status_t *status,
                                     bool browsing, bool memory_visible,
                                     bool controller_ready, bool audio_connected,
                                     bool airplay);
void sony_radio_status_apply(uint64_t words[26],
                             const sony_radio_status_t *status);
void sony_radio_status_apply_view(uint64_t words[26],
                                  const sony_radio_status_t *status,
                                  bool equalizer);
