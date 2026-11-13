#include "sony_text.h"

#include <string.h>

#include "sony_text_font.inc"

static bool decode(const unsigned char **cursor, uint32_t *cp) {
  const unsigned char *p = *cursor;
  uint32_t value = *p++;
  unsigned count = 0;
  uint32_t minimum = 0;
  if (value >= 0xc2 && value <= 0xdf) {
    value &= 0x1f;
    count = 1;
    minimum = 0x80;
  } else if (value >= 0xe0 && value <= 0xef) {
    value &= 0x0f;
    count = 2;
    minimum = 0x800;
  } else if (value >= 0xf0 && value <= 0xf4) {
    value &= 7;
    count = 3;
    minimum = 0x10000;
  } else if (value >= 0x80) {
    return false;
  }
  while (count--) {
    if ((*p & 0xc0) != 0x80)
      return false;
    value = (value << 6) | (*p++ & 0x3f);
  }
  if (value < minimum || value > 0x10ffff ||
      (value >= 0xd800 && value <= 0xdfff))
    return false;
  *cursor = p;
  *cp = value;
  return true;
}

static bool mark(uint32_t cp) {
  for (size_t i = 0; i < sizeof(text_marks) / sizeof(text_marks[0]); ++i)
    if (cp >= text_marks[i].first && cp <= text_marks[i].last)
      return true;
  return cp >= 0x1f3fb && cp <= 0x1f3ff;
}

static uint64_t lookup(uint32_t cp, uint32_t accent, bool unsupported) {
  const uint64_t replacement = 0x002AEFBAA0ULL;
  if (unsupported)
    return replacement;
  for (size_t i = 0; i < sizeof(text_font) / sizeof(text_font[0]); ++i)
    if ((!accent && text_font[i].codepoint == cp) ||
        (accent && text_font[i].base == cp && text_font[i].mark == accent))
      return text_font[i].word;
  for (size_t i = 0;
       i < sizeof(text_transliterations) / sizeof(text_transliterations[0]);
       ++i)
    if (text_transliterations[i].codepoint == cp)
      return lookup(text_transliterations[i].base, 0, false);
  /* An unlisted Latin accent may reuse its captured/custom base glyph. */
  if (accent && ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')))
    return lookup(cp, 0, false);
  return replacement;
}

bool sony_text_parse(sony_text_t *text, const char *utf8, int64_t now_us) {
  if (!text || !utf8 || strlen(utf8) > SONY_TEXT_BYTES)
    return false;
  const unsigned char *p = (const unsigned char *)utf8;
  text->length = 0;
  while (*p) {
    uint32_t cp;
    if (!decode(&p, &cp) || text->length == SONY_TEXT_CAPACITY)
      return false;
    uint32_t accent = 0;
    unsigned accents = 0;
    bool unsupported = false;
    bool joined = false;
    bool regional = cp >= 0x1f1e6 && cp <= 0x1f1ff;
    while (*p) {
      const unsigned char *next = p;
      uint32_t suffix;
      if (!decode(&next, &suffix))
        return false;
      if (suffix == 0x200d) {
        joined = true;
        unsupported = true;
      } else if (joined) {
        joined = false;
      } else if (regional && suffix >= 0x1f1e6 && suffix <= 0x1f1ff) {
        regional = false;
        unsupported = true;
      } else if (mark(suffix)) {
        if (suffix != 0xfe0e && suffix != 0xfe0f) {
          ++accents;
          accent = suffix;
        }
      } else {
        break;
      }
      p = next;
    }
    if (accents > 1) {
      if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
        accent = 0;
      else
        unsupported = true;
    }
    text->glyphs[text->length++] = lookup(cp, accent, unsupported);
  }
  text->started_us = now_us;
  text->active = true;
  text->scroll = true;
  return true;
}

void sony_text_frame(const sony_text_t *text, int64_t now_us,
                     uint64_t words[26]) {
  sony_text_frame_width(text, now_us, words, 9);
}

void sony_text_frame_width(const sony_text_t *text, int64_t now_us,
                           uint64_t words[26], unsigned width) {
  if (!text->active || !width || width > 22)
    return;
  size_t offset = 0;
  if (text->scroll && text->length > width) {
    const size_t cycle = text->length + 4;
    const int64_t period = 1000000 + (int64_t)(cycle - 1) * 300000;
    int64_t elapsed = now_us > text->started_us ? now_us - text->started_us : 0;
    elapsed %= period;
    if (elapsed >= 1000000)
      offset = 1 + (size_t)((elapsed - 1000000) / 300000);
  }
  for (size_t i = 0; i < width; ++i) {
    size_t index = text->scroll && text->length > width
                       ? (offset + i) % (text->length + 4)
                       : i;
    words[22 - i] = index < text->length ? text->glyphs[index] : 0;
  }
}
