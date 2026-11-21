#include "sony_startup.h"
#include "sony_text.h"

#include <string.h>

enum {
  FILL = 1250, TITLE = 2150, STARS = 3000, PHRASE = 4250, DISSOLVE = 5250,
};

static uint32_t mix(uint32_t n) {
  n = (n ^ (n >> 16)) * UINT32_C(0x7feb352d);
  return n ^ (n >> 15);
}

void sony_startup_frame(unsigned ms, uint64_t frame[26]) {
  static const char title[] = "SONY DAR-1000ES";
  static const char phrase[] = "Elevated Standard";
  memset(frame, 0, 26 * sizeof(*frame));
  if (ms >= SONY_STARTUP_ART_MS)
    return;
  for (unsigned cell = 0; cell < 22; ++cell) {
    uint64_t word = 0;
    if (ms < STARS) {
      uint64_t glyph = ms >= FILL && cell >= 3 && cell < 18
                           ? sony_text_ascii_word(title[cell - 3]) : 0;
      for (unsigned col = 0; col < 5; ++col) {
        int lag = mix(5 * cell + col + 1) % 500;
        if (ms < FILL) {
          int tick = ((int)ms - lag) / 45;
          if ((int)ms < lag)
            continue;
          int landed = tick / 2;
          int falling = tick % 7;
          for (int row = 0; row < 7; ++row)
            if (row >= 7 - landed || (row == falling && landed < 7))
              word |= UINT64_C(1) << (row * 5 + col);
        } else {
          int shifted = ((int)ms - FILL - lag / 2) / 80;
          for (int row = 0; row < 7; ++row) {
            uint64_t bit = UINT64_C(1) << (row * 5 + col);
            if (row >= shifted || (glyph & bit))
              word |= bit;
          }
        }
      }
    } else {
      unsigned age = ms - STARS;
      unsigned fade = ms >= DISSOLVE ? ms - DISSOLVE : 0;
      uint64_t glyph = cell >= 1 && cell < 18
                           ? sony_text_ascii_word(phrase[cell - 1]) : 0;
      uint32_t seed = mix(cell + 23 * (ms / 75) + 1);
      if (ms < PHRASE) {
        unsigned reveal = cell == 1 || cell == 10 ? 150 : 260 + 53 * cell;
        if (age >= reveal)
          word = glyph;
        if (seed % (PHRASE - STARS) >= age)
          word |= UINT64_C(1) << (seed % 35);
      } else if (ms < DISSOLVE) {
        word = glyph;
      } else {
        for (unsigned bit = 0; bit < 35; ++bit)
          if ((glyph & (UINT64_C(1) << bit)) &&
              mix(cell * 35 + bit + 1) % 450 >= fade)
            word |= UINT64_C(1) << bit;
        if (seed % 1000 >= fade)
          word |= UINT64_C(1) << (seed % 35);
      }
    }
    frame[22 - cell] = word;
  }
}
