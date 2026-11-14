#include "../main/sony_display_modes.h"
#include "../main/sony_text.h"

#include <assert.h>
#include <string.h>

int main(void) {
  sony_display_metadata_t metadata = {
      .title = "Artist - A song title longer than twenty-two characters",
      .station = "Example FM", .source = "Radio", .codec = "MP3",
      .bitrate = "192", .genre = "Mixed", .url = "https://example.com/live.mp3",
      .ip = "172.16.5.207", .sample_rate = 44100, .channels = 2,
      .receiving = true};
  char text[768];
  const char *expected[] = {
      metadata.title, "Radio - Example FM", "MP3 44.1 kHz STEREO",
      "192 kbps", "Genre: Mixed", "URL: https://example.com/live.mp3",
      "", "ESP IP: 172.16.5.207"};
  for (unsigned mode = 0; mode < SONY_DISPLAY_MODE_COUNT; ++mode) {
    sony_display_mode_text((sony_display_mode_t)mode, text, sizeof(text), &metadata);
    assert(!strcmp(text, expected[mode]));
    sony_text_t parsed;
    assert(sony_text_parse(&parsed, text, 123));
  }
  metadata.receiving = false;
  metadata.failed = true;
  sony_display_mode_text(SONY_DISPLAY_NOW_PLAYING, text, sizeof(text), &metadata);
  assert(!strcmp(text, "RECONNECTING - Example FM"));
  metadata.channels = 1;
  metadata.codec = "AAC";
  metadata.sample_rate = 48000;
  sony_display_mode_text(SONY_DISPLAY_AUDIO_FORMAT, text, sizeof(text), &metadata);
  assert(!strcmp(text, "AAC 48.0 kHz MONO"));
  metadata.sample_rate = 0;
  sony_display_mode_text(SONY_DISPLAY_AUDIO_FORMAT, text, sizeof(text), &metadata);
  assert(!strcmp(text, "Audio format unavailable"));
  metadata.bitrate = metadata.genre = metadata.url = metadata.ip = "";
  const sony_display_mode_t missing[] = {
      SONY_DISPLAY_BITRATE, SONY_DISPLAY_STATION_GENRE,
      SONY_DISPLAY_STREAM_URL, SONY_DISPLAY_ESP_IP};
  const char *fallback[] = {
      "Bitrate unavailable", "Genre: Unknown", "URL: Unavailable", "ESP IP: Disconnected"};
  for (unsigned i = 0; i < 4; ++i) {
    sony_display_mode_text(missing[i], text, sizeof(text), &metadata);
    assert(!strcmp(text, fallback[i]));
  }
  char url[512];
  memset(url, 'a', sizeof(url) - 1);
  url[sizeof(url) - 1] = 0;
  metadata.url = url;
  sony_display_mode_text(SONY_DISPLAY_STREAM_URL, text, sizeof(text), &metadata);
  sony_text_t parsed;
  assert(sony_text_parse(&parsed, text, 0) && parsed.length == 200);
  assert(!strncmp(text, "URL: ", 5));
  metadata.genre = "Caf\xc3\xa9 - \xe2\x99\xab";
  sony_display_mode_text(SONY_DISPLAY_STATION_GENRE, text, sizeof(text), &metadata);
  assert(sony_text_parse(&parsed, text, 0));
  assert(strstr(text, "\xc3\xa9"));
  char small[10];
  sony_display_mode_text(SONY_DISPLAY_STATION_GENRE, small, sizeof(small), &metadata);
  assert(sony_text_parse(&parsed, small, 0));

  sony_display_mode_t mode;
  assert(sony_display_mode_parse("spectrum", &mode) && mode == SONY_DISPLAY_EQUALIZER);
  assert(!sony_display_mode_parse("unknown", &mode));
  assert(!sony_display_mode_parse(NULL, &mode));

  sony_timing_t timing = {
      .setup_ns=500, .clock_low_ns=1000, .clock_high_ns=1000,
      .strobe_ns=5000, .latch_guard_ns=10000,
      .blank_before_ns=10000, .blank_after_ns=10000,
      .on_ns=58500, .off_ns=104000, .pulses=1};
  sony_timing_t original = timing;
  size_t slot_samples = sony_timing_info(&timing).slot_samples;
  for (unsigned i = 0; i < 6; ++i) {
    assert(sony_display_dim_next(&timing));
    assert(sony_display_dim_level(&timing) == (int)(i % 3));
    assert(sony_timing_info(&timing).slot_samples == slot_samples);
    assert(timing.on_ns == 19500 * (i % 3 + 1));
    assert(timing.on_ns + timing.off_ns == 162500);
    sony_timing_t edges = timing;
    edges.on_ns = original.on_ns;
    edges.off_ns = original.off_ns;
    assert(!memcmp(&edges, &original, sizeof(edges)));
  }
  timing.on_ns = 78000;
  timing.off_ns = 84500;
  assert(sony_display_dim_level(&timing) == -1);
  assert(sony_display_dim_next(&timing) && timing.on_ns == 19500);
  for (unsigned i = 0; i < 4; ++i) {
    sony_timing_t invalid = original;
    if (i == 0) invalid.hold = 1;
    if (i == 1) { invalid.pair = 1; invalid.slot = 1; }
    if (i == 2) invalid.pulses = 2;
    if (i == 3) invalid.off_ns += 125;
    sony_timing_t before = invalid;
    assert(!sony_display_dim_next(&invalid));
    assert(!memcmp(&invalid, &before, sizeof(invalid)));
  }
  return 0;
}
