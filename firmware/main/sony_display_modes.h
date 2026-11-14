#pragma once

#include "sony_timing.h"

typedef enum {
  SONY_DISPLAY_NOW_PLAYING,
  SONY_DISPLAY_STREAM_SOURCE,
  SONY_DISPLAY_AUDIO_FORMAT,
  SONY_DISPLAY_BITRATE,
  SONY_DISPLAY_STATION_GENRE,
  SONY_DISPLAY_STREAM_URL,
  SONY_DISPLAY_EQUALIZER,
  SONY_DISPLAY_ESP_IP,
  SONY_DISPLAY_MODE_COUNT
} sony_display_mode_t;

typedef struct {
  const char *title;
  const char *station;
  const char *source;
  const char *codec;
  const char *bitrate;
  const char *genre;
  const char *url;
  const char *ip;
  uint32_t sample_rate;
  unsigned channels;
  bool receiving;
  bool failed;
} sony_display_metadata_t;

const char *sony_display_mode_id(sony_display_mode_t mode);
const char *sony_display_mode_label(sony_display_mode_t mode);
bool sony_display_mode_parse(const char *id, sony_display_mode_t *mode);
void sony_display_mode_text(sony_display_mode_t mode, char *text,
                            size_t capacity,
                            const sony_display_metadata_t *metadata);
int sony_display_dim_level(const sony_timing_t *timing);
bool sony_display_dim_next(sony_timing_t *timing);
