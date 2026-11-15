#include "sony_display_modes.h"
#include "audio/radio_display_text.h"

#include <stdio.h>
#include <string.h>

static const char *const ids[] = {
    "now_playing", "stream_source", "audio_format", "bitrate",
    "station_genre", "stream_url", "esp_ip", "equalizer", "vu_stereo", "vu_center"};
static const char *const labels[] = {
    "Now Playing", "Stream Source", "Audio Format", "BitRate",
    "Station Genre", "StreamURL", "ESP IP address", "Equalizer",
    "Stereo VU", "Center-out VU"};

bool sony_display_mode_graphical(sony_display_mode_t mode) {
  return mode == SONY_DISPLAY_EQUALIZER || mode == SONY_DISPLAY_VU_STEREO ||
         mode == SONY_DISPLAY_VU_CENTER;
}

const char *sony_display_mode_id(sony_display_mode_t mode) {
  return (unsigned)mode < SONY_DISPLAY_MODE_COUNT ? ids[mode] : "manual";
}

const char *sony_display_mode_label(sony_display_mode_t mode) {
  return (unsigned)mode < SONY_DISPLAY_MODE_COUNT ? labels[mode] : "Manual";
}

bool sony_display_mode_parse(const char *id, sony_display_mode_t *mode) {
  if (!id || !mode)
    return false;
  if (!strcmp(id, "spectrum")) {
    *mode = SONY_DISPLAY_EQUALIZER;
    return true;
  }
  for (unsigned i = 0; i < SONY_DISPLAY_MODE_COUNT; ++i)
    if (!strcmp(id, ids[i])) {
      *mode = (sony_display_mode_t)i;
      return true;
    }
  return false;
}

static const char *available(const char *value, const char *fallback) {
  return value && *value ? value : fallback;
}

void sony_display_mode_text(sony_display_mode_t mode, char *text,
                            size_t capacity,
                            const sony_display_metadata_t *metadata) {
  if (!text || !capacity || !metadata)
    return;
  if (mode == SONY_DISPLAY_NOW_PLAYING) {
    radio_display_text(text, capacity, available(metadata->title, ""),
                       available(metadata->station, "Radio"),
                       metadata->receiving, metadata->failed);
    return;
  }
  char raw[768];
  switch (mode) {
  case SONY_DISPLAY_STREAM_SOURCE:
    snprintf(raw, sizeof(raw), "%s - %s",
             available(metadata->source, "Radio"),
             available(metadata->station, "Unknown stream"));
    break;
  case SONY_DISPLAY_AUDIO_FORMAT:
    if (metadata->sample_rate && metadata->channels)
      snprintf(raw, sizeof(raw), "%s %lu.%lu kHz %s",
               available(metadata->codec, "Unknown codec"),
               (unsigned long)(metadata->sample_rate / 1000),
               (unsigned long)((metadata->sample_rate % 1000) / 100),
               metadata->channels == 1 ? "MONO" : "STEREO");
    else
      snprintf(raw, sizeof(raw), "Audio format unavailable");
    break;
  case SONY_DISPLAY_BITRATE:
    if (metadata->bitrate && *metadata->bitrate)
      snprintf(raw, sizeof(raw), "%s kbps", metadata->bitrate);
    else
      snprintf(raw, sizeof(raw), "Bitrate unavailable");
    break;
  case SONY_DISPLAY_STATION_GENRE:
    snprintf(raw, sizeof(raw), "Genre: %s",
             available(metadata->genre, "Unknown"));
    break;
  case SONY_DISPLAY_STREAM_URL:
    snprintf(raw, sizeof(raw), "URL: %s",
             available(metadata->url, "Unavailable"));
    break;
  case SONY_DISPLAY_ESP_IP:
    snprintf(raw, sizeof(raw), "ESP IP: %s",
             available(metadata->ip, "Disconnected"));
    break;
  default:
    raw[0] = 0;
    break;
  }
  radio_display_text(text, capacity, raw, "", true, false);
}

static uint64_t dim_on_ns(const sony_timing_t *timing, unsigned level) {
  static const unsigned duty[] = {120, 140, 160, 100, 75};
  uint64_t slot_ticks = sony_timing_info(timing).slot_samples;
  return (slot_ticks * 26 * duty[level] + 5000) / 10000 * SONY_TIMING_TICK_NS;
}

int sony_display_dim_level(const sony_timing_t *timing) {
  if (!timing || timing->hold || timing->pair || timing->alternate_ms ||
      timing->pulses != 1 || sony_timing_validate(timing))
    return -1;
  for (unsigned level = 0; level < 5; ++level)
    if (timing->on_ns == dim_on_ns(timing, level))
      return (int)level;
  return -1;
}

bool sony_display_dim_next(sony_timing_t *timing) {
  if (!timing || timing->hold || timing->pair || timing->alternate_ms ||
      timing->pulses != 1 || sony_timing_validate(timing))
    return false;
  int level = sony_display_dim_level(timing);
  unsigned next = level < 0 ? 0 : (unsigned)(level + 1) % 5;
  uint64_t visible_ns = (uint64_t)timing->on_ns + timing->off_ns;
  uint64_t on_ns = dim_on_ns(timing, next);
  if (!on_ns || on_ns > visible_ns)
    return false;
  sony_timing_t updated = *timing;
  updated.on_ns = (uint32_t)on_ns;
  updated.off_ns = (uint32_t)(visible_ns - on_ns);
  if (sony_timing_validate(&updated))
    return false;
  *timing = updated;
  return true;
}
