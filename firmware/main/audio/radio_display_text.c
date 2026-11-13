#include "radio_display_text.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

void radio_display_text(char *output, size_t capacity, const char *title,
                        const char *station, bool receiving, bool failed) {
  if (!capacity)
    return;
  char text[768];
  if (!receiving)
    snprintf(text, sizeof(text), "%s - %s",
             failed ? "RECONNECTING" : "CONNECTING", station);
  else
    snprintf(text, sizeof(text), "%s", *title ? title : station);
  size_t used = 0, glyphs = 0;
  const unsigned char *p = (const unsigned char *)text;
  while (*p && glyphs++ < 200) {
    size_t count = *p < 0x80                  ? 1
                   : *p >= 0xc2 && *p <= 0xdf ? 2
                   : *p >= 0xe0 && *p <= 0xef ? 3
                   : *p >= 0xf0 && *p <= 0xf4 ? 4
                                              : 0;
    uint32_t cp = count ? *p & (0x7f >> (count == 1 ? 0 : count)) : 0;
    bool valid = count != 0;
    for (size_t i = 1; valid && i < count; ++i) {
      if ((p[i] & 0xc0) != 0x80)
        valid = false;
      else
        cp = (cp << 6) | (p[i] & 0x3f);
    }
    valid = valid && (count < 2 || cp >= 0x80) && (count < 3 || cp >= 0x800) &&
            (count < 4 || cp >= 0x10000) && !(cp >= 0xd800 && cp <= 0xdfff) &&
            cp <= 0x10ffff;
    if (!valid) {
      /* ICY stations may send Latin-1 instead of UTF-8. */
      if (used + 2 >= capacity)
        break;
      output[used++] = (char)(0xc0 | (*p >> 6));
      output[used++] = (char)(0x80 | (*p++ & 0x3f));
    } else {
      if (used + count >= capacity)
        break;
      if (count == 1 && (*p < 32 || *p == 127))
        output[used++] = ' ';
      else {
        memcpy(output + used, p, count);
        used += count;
      }
      p += count;
    }
  }
  output[used] = 0;
}
