#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SONY_UART_FRAME_SIZE 14
enum {
  SONY_UART_PING = 1, SONY_UART_PONG, SONY_UART_MAP, SONY_UART_ACK,
  SONY_UART_AUDIO_STATE, SONY_UART_EQ0, SONY_UART_EQ1, SONY_UART_EQ2,
  SONY_UART_RMS, SONY_UART_PEAK, SONY_UART_HELD,
  SONY_UART_STARTUP,
  SONY_UART_TITLE0 = 16, SONY_UART_TITLE_LAST = 31,
  SONY_UART_METADATA0 = 32
};
enum { SONY_STARTUP_SOUND_CANCEL = 0, SONY_STARTUP_SOUND_PLAY = 1 };

typedef struct {
  char codec[4], bitrate[32], genre[128], ip[16], url[512];
} sony_uart_metadata_t;

enum {
  SONY_UART_METADATA_PARTS = sizeof(sony_uart_metadata_t) / 4,
  SONY_UART_METADATA_LAST = SONY_UART_METADATA0 + SONY_UART_METADATA_PARTS - 1
};

typedef struct {
  uint8_t type;
  uint32_t sequence;
  uint32_t value;
} sony_uart_message_t;

typedef struct {
  uint8_t bytes[SONY_UART_FRAME_SIZE];
  size_t used;
  uint32_t bad_frames;
} sony_uart_parser_t;

void sony_uart_encode(const sony_uart_message_t *message,
                      uint8_t output[SONY_UART_FRAME_SIZE]);
bool sony_uart_feed(sony_uart_parser_t *parser, uint8_t byte,
                    sony_uart_message_t *message);
bool sony_uart_metadata_feed(sony_uart_metadata_t *staging, unsigned *next,
                             const sony_uart_message_t *message);
