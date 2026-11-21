#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  SONY_STATION_ID_MAX = 36,
  SONY_STATION_NAME_MAX = 63,
  SONY_STATION_URL_MAX = 511,
  SONY_STATION_WIRE_MAX = 12 + SONY_STATION_ID_MAX +
                          SONY_STATION_NAME_MAX + SONY_STATION_URL_MAX + 2,
};

typedef struct {
  uint32_t generation;
  char id[SONY_STATION_ID_MAX + 1];
  char name[SONY_STATION_NAME_MAX + 1];
  char url[SONY_STATION_URL_MAX + 1];
  uint32_t genre_labels; /* Controller-local; not encoded on UART. */
} sony_station_command_t;

typedef struct {
  uint8_t bytes[SONY_STATION_WIRE_MAX];
  size_t used;
  uint32_t bad_frames;
} sony_station_parser_t;

size_t sony_station_encode(const sony_station_command_t *station,
                           uint8_t *output, size_t capacity);
bool sony_station_decode(const uint8_t *frame, size_t size,
                         sony_station_command_t *station);
bool sony_station_feed(sony_station_parser_t *parser, uint8_t byte,
                       sony_station_command_t *station);
