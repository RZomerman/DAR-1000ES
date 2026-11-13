#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SONY_TEXT_CAPACITY 256
#define SONY_TEXT_BYTES    1024

typedef struct {
  uint64_t glyphs[SONY_TEXT_CAPACITY];
  size_t length;
  int64_t started_us;
  bool active;
  bool scroll;
} sony_text_t;

bool sony_text_parse(sony_text_t *text, const char *utf8, int64_t now_us);
void sony_text_frame(const sony_text_t *text, int64_t now_us,
                     uint64_t words[26]);
void sony_text_frame_width(const sony_text_t *text, int64_t now_us,
                           uint64_t words[26], unsigned width);
