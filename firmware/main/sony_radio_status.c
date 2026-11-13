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

static unsigned genre_labels(const char *genre) {
  static const char *const music[] = {
      "music", "pop",    "rock",       "oldies",      "jazz",   "classical",
      "dance", "house",  "electronic", "electronica", "techno", "trance",
      "soul",  "funk",   "disco",      "country",     "folk",   "blues",
      "metal", "reggae", "hiphop",     "hip-hop",     "rap",    "r&b",
      "rnb",   "top40",  "top-40",     "alternative", "indie",  "muziek"};
  static const char *const speech[] = {
      "speech",     "talk",   "news",  "spoken", "spoken-word", "interview",
      "discussion", "sports", "sport", "nieuws", "praat"};
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
    for (size_t i = 0; i < sizeof(music) / sizeof(music[0]); ++i)
      has_music |= !strcmp(word, music[i]);
    for (size_t i = 0; i < sizeof(speech) / sizeof(speech[0]); ++i)
      has_speech |= !strcmp(word, speech[i]);
  }
  if (has_music == has_speech)
    return 0;
  return has_music ? 0x02 : 0x01;
}

sony_radio_status_t sony_radio_status(bool playing, unsigned channels,
                                      const char *bitrate, const char *genre) {
  sony_radio_status_t status = {.meter = 0x4000};
  if (!playing || (channels != 1 && channels != 2))
    return status;
  status.audio = 0x2000 | (channels == 1 ? 0x10 : 0) | genre_labels(genre);
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

void sony_radio_status_apply(uint64_t words[26],
                             const sony_radio_status_t *status) {
  words[0] = (words[0] & ~0x7FFFULL) | (status->meter & 0x7FFF);
  words[24] = (words[24] & ~0x2013ULL) | (status->audio & 0x2013);
  words[25] = (words[25] & ~1ULL) | (status->stereo & 1);
}

void sony_radio_status_apply_view(uint64_t words[26],
                                  const sony_radio_status_t *status,
                                  bool spectrum) {
  sony_radio_status_apply(words, status);
  words[24] &= ~0x80CULL;
  if (spectrum)
    words[24] |= 0x818;
}
