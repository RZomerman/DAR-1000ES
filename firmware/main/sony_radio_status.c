#include "sony_radio_status.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static unsigned bitrate_kbps(const char *text) {
  if (!text)
    return 0;
  const unsigned char *p = (const unsigned char *)text;
  while (isspace(*p))
    ++p;
  if (!isdigit(*p))
    return 0;
  unsigned value = 0;
  while (isdigit(*p)) {
    if (value > 100000)
      return 0;
    value = value * 10 + (*p++ - '0');
  }
  while (isspace(*p))
    ++p;
  return *p ? 0 : value;
}

unsigned sony_radio_genre_labels(const char *genre) {
  static const char *const music[] = {
      "music", "pop",    "rock",       "oldies",      "jazz",   "classical",
      "dance", "house",  "electronic", "electronica", "techno", "trance",
      "soul",  "funk",   "disco",      "country",     "folk",   "blues",
      "metal", "reggae", "hiphop",     "hip-hop",     "rap",    "r&b",
      "rnb",   "top40",  "top-40",     "alternative", "indie",  "muziek",
      "ambient", "easy", "instrumental", "gospel", "punk", "salsa", "latin",
      "k-pop", "schlager", "hits", "lounge", "chillout", "hardstyle"};
  static const char *const speech[] = {
      "speech",     "talk",   "news",  "spoken", "spoken-word", "interview",
      "discussion", "sports", "sport", "nieuws", "praat", "politics",
      "interviews", "comedy", "documentary", "education", "traffic"};
  bool has_music = false, has_speech = false, previous_top = false;
  const unsigned char *p = (const unsigned char *)(genre ? genre : "");
  while (*p) {
    if (!isalnum(*p)) {
      ++p;
      continue;
    }
    char word[40];
    size_t length = 0;
    bool too_long = false;
    while (isalnum(*p) || *p == '-' || *p == '&') {
      if (length + 1 < sizeof(word))
        word[length++] = (char)tolower(*p);
      else
        too_long = true;
      ++p;
    }
    word[length] = 0;
    if (too_long) {
      previous_top = false;
      continue;
    }
    if (previous_top && !strcmp(word, "40"))
      has_music = true;
    previous_top = !strcmp(word, "top");
    if (!strcmp(word, "mixed")) has_music = has_speech = true;
    for (size_t i = 0; i < sizeof(music) / sizeof(music[0]); ++i)
      has_music |= !strcmp(word, music[i]);
    for (size_t i = 0; i < sizeof(speech) / sizeof(speech[0]); ++i)
      has_speech |= !strcmp(word, speech[i]);
  }
  return (has_music ? 0x02 : 0) | (has_speech ? 0x01 : 0);
}

sony_radio_status_t sony_radio_status(bool playing, unsigned channels,
                                      const char *bitrate, const char *genre) {
  sony_radio_status_t status = {.meter = 0x4000, .playing = playing};
  if (playing) status.audio = sony_radio_genre_labels(genre);
  if (!playing || (channels != 1 && channels != 2))
    return status;
  status.audio |= 0x2000 | (channels == 1 ? 0x10 : 0);
  status.stereo = channels == 2 ? 1 : 0;
  unsigned rate = bitrate_kbps(bitrate);
  if (rate) {
    if (rate > 320)
      rate = 320;
    unsigned bars = (rate * 14 + 160) / 320;
    if (!bars)
      bars = 1;
    status.meter = (1ULL << bars) - 1;
  }
  return status;
}

void sony_radio_controller_indicators(uint64_t words[26],
                                     const sony_radio_status_t *status,
                                     bool browsing, bool memory_visible,
                                     bool controller_ready, bool audio_connected,
                                     bool airplay) {
  uint64_t preset = !airplay && status->preset >= 1 && status->preset <= 16
                        ? 1ULL << (status->preset + 5) : 0;
  words[23] = (words[23] & ~0x3FFFCFULL) | preset |
              (controller_ready ? 0x01 : 0) | (audio_connected ? 0x02 : 0) |
              (browsing ? 0x04 : 0) | (status->playing ? 0 : 0x08);
  words[24] = (words[24] & ~0x603ULL) | (status->audio & 0x03);
  words[25] = (words[25] & ~0x02ULL) | (memory_visible ? 0x02 : 0);
  if (airplay) {
    words[24] |= 0x5000;  // Both tuning triangles.
    words[25] = (words[25] & ~0xFFFFFCULL) | 0x1DC;  // Red a,b,c,e,f,g.
  }
}
void sony_radio_status_apply(uint64_t words[26],
                             const sony_radio_status_t *status) {
  words[0] = (words[0] & ~0x7FFFULL) | (status->meter & 0x7FFF);
  words[24] = (words[24] & ~0x2013ULL) | (status->audio & 0x2013);
  words[25] = (words[25] & ~1ULL) | (status->stereo & 1);
}

void sony_radio_status_apply_view(uint64_t words[26],
                                  const sony_radio_status_t *status,
                                  bool equalizer) {
  sony_radio_status_apply(words, status);
  words[24] &= ~0x80CULL;
  if (equalizer)
    words[0] &= ~0x3FFFULL;
}
